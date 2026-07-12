#include "xsk.h"
#include "rewsr_xdp_uapi.h"

#include <errno.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

/* MAP_ANONYMOUS and MAP_POPULATE are spelled differently or absent off
 * Linux. Normalize them so the UMEM allocation compiles everywhere; the
 * socket calls below are what actually gate this to a real AF_XDP kernel. */
#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#else
#define MAP_ANONYMOUS 0
#endif
#endif
#ifndef MAP_POPULATE
#define MAP_POPULATE 0
#endif

/* neg_errno returns a negative errno, defaulting to a generic failure when
 * errno was not set, so callers always get a nonzero negative on error. */
static int neg_errno(void) { return errno != 0 ? -errno : -1; }

void rewsr_xsk_config_default(struct rewsr_xsk_config *cfg) {
    cfg->num_frames = 4096;
    cfg->frame_size = 2048;
    cfg->fill_size = 2048;
    cfg->comp_size = 2048;
    cfg->rx_size = 2048;
    cfg->tx_size = 2048;
    cfg->bind_flags = 0;
}

/* apply_defaults fills any zero field of cfg from the standard config, so a
 * caller can set only what it cares about. */
static void apply_defaults(struct rewsr_xsk_config *out,
                           const struct rewsr_xsk_config *in) {
    struct rewsr_xsk_config def;
    rewsr_xsk_config_default(&def);
    out->num_frames = in && in->num_frames ? in->num_frames : def.num_frames;
    out->frame_size = in && in->frame_size ? in->frame_size : def.frame_size;
    out->fill_size = in && in->fill_size ? in->fill_size : def.fill_size;
    out->comp_size = in && in->comp_size ? in->comp_size : def.comp_size;
    out->rx_size = in && in->rx_size ? in->rx_size : def.rx_size;
    out->tx_size = in && in->tx_size ? in->tx_size : def.tx_size;
    out->bind_flags = in ? in->bind_flags : 0;
}

/* map_ring mmaps one ring region on the socket fd at pgoff and wires a
 * rewsr_xsk_ring over it using the offsets the kernel reported. entry_size
 * is the descriptor size (8 for fill/completion addresses, sizeof(xdp_desc)
 * for RX/TX). On success *map and *map_size receive the mapping so it can
 * be unmapped later. Returns 0 or a negative errno. */
static int map_ring(int fd, uint64_t pgoff,
                    const struct rewsr_xdp_ring_offset *off, uint32_t entries,
                    size_t entry_size, struct rewsr_xsk_ring *ring,
                    void **map, size_t *map_size) {
    size_t size = (size_t)off->desc + (size_t)entries * entry_size;
    void *area = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, fd, (off_t)pgoff);
    if (area == MAP_FAILED) {
        return neg_errno();
    }
    uint32_t *producer = (uint32_t *)((uint8_t *)area + off->producer);
    uint32_t *consumer = (uint32_t *)((uint8_t *)area + off->consumer);
    uint8_t *desc = (uint8_t *)area + off->desc;
    if (rewsr_xsk_ring_init(ring, producer, consumer, desc, entries,
                            entry_size) != 0) {
        munmap(area, size);
        return -EINVAL;
    }
    *map = area;
    *map_size = size;
    return 0;
}

int rewsr_xsk_umem_create(struct rewsr_xsk_umem *umem,
                          const struct rewsr_xsk_config *cfg_in) {
    struct rewsr_xsk_config cfg;
    apply_defaults(&cfg, cfg_in);
    memset(umem, 0, sizeof *umem);
    umem->fd = -1;

    umem->frame_size = cfg.frame_size;
    umem->num_frames = cfg.num_frames;
    umem->area_size = (size_t)cfg.num_frames * cfg.frame_size;

    /* The UMEM is a page-aligned anonymous mapping the kernel will DMA
     * into, so it is mapped shared and pre-faulted. */
    umem->area = mmap(NULL, umem->area_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (umem->area == MAP_FAILED) {
        umem->area = NULL;
        return neg_errno();
    }

    umem->fd = socket(REWSR_AF_XDP, SOCK_RAW, 0);
    if (umem->fd < 0) {
        int rc = neg_errno();
        rewsr_xsk_umem_destroy(umem);
        return rc;
    }

    struct rewsr_xdp_umem_reg reg;
    memset(&reg, 0, sizeof reg);
    reg.addr = (uint64_t)(uintptr_t)umem->area;
    reg.len = umem->area_size;
    reg.chunk_size = cfg.frame_size;
    reg.headroom = 0;
    reg.flags = 0;
    if (setsockopt(umem->fd, REWSR_SOL_XDP, REWSR_XDP_UMEM_REG, &reg,
                   sizeof reg) != 0) {
        int rc = neg_errno();
        rewsr_xsk_umem_destroy(umem);
        return rc;
    }

    if (setsockopt(umem->fd, REWSR_SOL_XDP, REWSR_XDP_UMEM_FILL_RING,
                   &cfg.fill_size, sizeof cfg.fill_size) != 0 ||
        setsockopt(umem->fd, REWSR_SOL_XDP, REWSR_XDP_UMEM_COMPLETION_RING,
                   &cfg.comp_size, sizeof cfg.comp_size) != 0) {
        int rc = neg_errno();
        rewsr_xsk_umem_destroy(umem);
        return rc;
    }

    struct rewsr_xdp_mmap_offsets offsets;
    socklen_t optlen = sizeof offsets;
    if (getsockopt(umem->fd, REWSR_SOL_XDP, REWSR_XDP_MMAP_OFFSETS, &offsets,
                   &optlen) != 0) {
        int rc = neg_errno();
        rewsr_xsk_umem_destroy(umem);
        return rc;
    }

    int rc = map_ring(umem->fd, REWSR_XDP_UMEM_PGOFF_FILL_RING, &offsets.fr,
                      cfg.fill_size, sizeof(uint64_t), &umem->fill,
                      &umem->fill_map, &umem->fill_map_size);
    if (rc != 0) {
        rewsr_xsk_umem_destroy(umem);
        return rc;
    }
    rc = map_ring(umem->fd, REWSR_XDP_UMEM_PGOFF_COMPLETION_RING, &offsets.cr,
                  cfg.comp_size, sizeof(uint64_t), &umem->comp,
                  &umem->comp_map, &umem->comp_map_size);
    if (rc != 0) {
        rewsr_xsk_umem_destroy(umem);
        return rc;
    }

    /* Seed the free-frame pool with every chunk of the UMEM. */
    umem->pool_stack = calloc(cfg.num_frames, sizeof(uint64_t));
    if (umem->pool_stack == NULL) {
        rewsr_xsk_umem_destroy(umem);
        return -ENOMEM;
    }
    if (rewsr_frame_pool_init(&umem->pool, umem->pool_stack, cfg.num_frames,
                              cfg.frame_size) != 0) {
        rewsr_xsk_umem_destroy(umem);
        return -EINVAL;
    }
    return 0;
}

void rewsr_xsk_umem_destroy(struct rewsr_xsk_umem *umem) {
    if (umem->fill_map != NULL) {
        munmap(umem->fill_map, umem->fill_map_size);
        umem->fill_map = NULL;
    }
    if (umem->comp_map != NULL) {
        munmap(umem->comp_map, umem->comp_map_size);
        umem->comp_map = NULL;
    }
    if (umem->fd >= 0) {
        close(umem->fd);
        umem->fd = -1;
    }
    if (umem->area != NULL) {
        munmap(umem->area, umem->area_size);
        umem->area = NULL;
    }
    free(umem->pool_stack);
    umem->pool_stack = NULL;
}

int rewsr_xsk_socket_create(struct rewsr_xsk_socket *xsk,
                            struct rewsr_xsk_umem *umem, const char *ifname,
                            uint32_t queue_id,
                            const struct rewsr_xsk_config *cfg_in) {
    struct rewsr_xsk_config cfg;
    apply_defaults(&cfg, cfg_in);
    memset(xsk, 0, sizeof *xsk);
    xsk->fd = -1;
    xsk->umem = umem;

    uint32_t ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        return -ENODEV;
    }
    xsk->ifindex = ifindex;
    xsk->queue_id = queue_id;

    /* A single-socket-per-UMEM setup reuses the UMEM's fd, which is the
     * common case; a shared-UMEM multi-queue setup would open a new fd and
     * pass the UMEM fd via XDP_SHARED_UMEM. This path handles the former. */
    xsk->fd = umem->fd;
    xsk->owns_umem = 0;

    if (setsockopt(xsk->fd, REWSR_SOL_XDP, REWSR_XDP_RX_RING, &cfg.rx_size,
                   sizeof cfg.rx_size) != 0 ||
        setsockopt(xsk->fd, REWSR_SOL_XDP, REWSR_XDP_TX_RING, &cfg.tx_size,
                   sizeof cfg.tx_size) != 0) {
        return neg_errno();
    }

    struct rewsr_xdp_mmap_offsets offsets;
    socklen_t optlen = sizeof offsets;
    if (getsockopt(xsk->fd, REWSR_SOL_XDP, REWSR_XDP_MMAP_OFFSETS, &offsets,
                   &optlen) != 0) {
        return neg_errno();
    }

    int rc = map_ring(xsk->fd, REWSR_XDP_PGOFF_RX_RING, &offsets.rx,
                      cfg.rx_size, sizeof(struct rewsr_xdp_desc), &xsk->rx,
                      &xsk->rx_map, &xsk->rx_map_size);
    if (rc != 0) {
        return rc;
    }
    rc = map_ring(xsk->fd, REWSR_XDP_PGOFF_TX_RING, &offsets.tx, cfg.tx_size,
                  sizeof(struct rewsr_xdp_desc), &xsk->tx, &xsk->tx_map,
                  &xsk->tx_map_size);
    if (rc != 0) {
        rewsr_xsk_socket_destroy(xsk);
        return rc;
    }

    struct rewsr_sockaddr_xdp sxdp;
    memset(&sxdp, 0, sizeof sxdp);
    sxdp.sxdp_family = REWSR_AF_XDP;
    sxdp.sxdp_ifindex = ifindex;
    sxdp.sxdp_queue_id = queue_id;
    sxdp.sxdp_flags = (uint16_t)cfg.bind_flags;
    if (bind(xsk->fd, (struct sockaddr *)&sxdp, sizeof sxdp) != 0) {
        int brc = neg_errno();
        rewsr_xsk_socket_destroy(xsk);
        return brc;
    }
    return 0;
}

void rewsr_xsk_socket_destroy(struct rewsr_xsk_socket *xsk) {
    if (xsk->rx_map != NULL) {
        munmap(xsk->rx_map, xsk->rx_map_size);
        xsk->rx_map = NULL;
    }
    if (xsk->tx_map != NULL) {
        munmap(xsk->tx_map, xsk->tx_map_size);
        xsk->tx_map = NULL;
    }
    if (xsk->owns_umem && xsk->fd >= 0) {
        close(xsk->fd);
    }
    xsk->fd = -1;
}

uint32_t rewsr_xsk_fill_refill(struct rewsr_xsk_umem *umem, uint32_t nb) {
    uint32_t free_slots = rewsr_xsk_prod_free(&umem->fill);
    if (nb > free_slots) {
        nb = free_slots;
    }
    uint32_t idx;
    uint32_t reserved = rewsr_xsk_prod_reserve(&umem->fill, nb, &idx);
    uint32_t posted = 0;
    for (uint32_t i = 0; i < reserved; i++) {
        uint64_t addr;
        if (!rewsr_frame_pool_alloc(&umem->pool, &addr)) {
            break;
        }
        uint64_t *slot = rewsr_xsk_ring_desc(&umem->fill, idx + i);
        *slot = addr;
        posted++;
    }
    /* Only submit what was actually backed by a free frame. */
    rewsr_xsk_prod_submit(&umem->fill, posted);
    return posted;
}

uint32_t rewsr_xsk_rx_burst(struct rewsr_xsk_socket *xsk, uint32_t nb,
                            rewsr_xsk_rx_cb cb, void *user) {
    uint32_t idx;
    uint32_t got = rewsr_xsk_cons_peek(&xsk->rx, nb, &idx);
    for (uint32_t i = 0; i < got; i++) {
        struct rewsr_xdp_desc *d = rewsr_xsk_ring_desc(&xsk->rx, idx + i);
        const uint8_t *data =
            (const uint8_t *)xsk->umem->area + d->addr;
        if (cb != NULL) {
            cb(data, d->len, user);
        }
        rewsr_frame_pool_free(&xsk->umem->pool, d->addr);
    }
    rewsr_xsk_cons_release(&xsk->rx, got);
    /* Immediately top the fill ring back up so the kernel keeps receiving. */
    rewsr_xsk_fill_refill(xsk->umem, got);
    return got;
}

uint32_t rewsr_xsk_tx_burst(struct rewsr_xsk_socket *xsk, uint32_t nb,
                            rewsr_xsk_tx_cb cb, void *user) {
    uint32_t idx;
    uint32_t reserved = rewsr_xsk_prod_reserve(&xsk->tx, nb, &idx);
    uint32_t queued = 0;
    for (uint32_t i = 0; i < reserved; i++) {
        uint64_t addr;
        if (!rewsr_frame_pool_alloc(&xsk->umem->pool, &addr)) {
            break;
        }
        uint8_t *buf = (uint8_t *)xsk->umem->area + addr;
        uint32_t len = cb ? cb(buf, xsk->umem->frame_size, user) : 0;
        if (len == 0) {
            rewsr_frame_pool_free(&xsk->umem->pool, addr);
            continue;
        }
        struct rewsr_xdp_desc *d = rewsr_xsk_ring_desc(&xsk->tx, idx + queued);
        d->addr = addr;
        d->len = len;
        d->options = 0;
        queued++;
    }
    rewsr_xsk_prod_submit(&xsk->tx, queued);
    return queued;
}

uint32_t rewsr_xsk_tx_complete(struct rewsr_xsk_umem *umem, uint32_t nb) {
    uint32_t idx;
    uint32_t got = rewsr_xsk_cons_peek(&umem->comp, nb, &idx);
    for (uint32_t i = 0; i < got; i++) {
        uint64_t *slot = rewsr_xsk_ring_desc(&umem->comp, idx + i);
        rewsr_frame_pool_free(&umem->pool, *slot);
    }
    rewsr_xsk_cons_release(&umem->comp, got);
    return got;
}

int rewsr_xsk_kick(struct rewsr_xsk_socket *xsk) {
    /* A zero-length sendto on the socket asks the kernel to service the TX
     * ring. MSG_DONTWAIT keeps the fast path from blocking; EBUSY and
     * EAGAIN mean the kernel is already draining and are not real errors. */
    ssize_t r = sendto(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, 0);
    if (r < 0 && errno != ENOBUFS && errno != EAGAIN && errno != EBUSY) {
        return neg_errno();
    }
    return 0;
}
