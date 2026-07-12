#ifndef REWSR_XDP_XSK_H
#define REWSR_XDP_XSK_H

#include <stddef.h>
#include <stdint.h>

#include "../ring/xsk_ring.h"
#include "frame_pool.h"

/* xsk is the AF_XDP data-plane socket: a UMEM (the shared packet buffer)
 * plus the four rings that move frame descriptors between userspace and the
 * kernel. This is what tier1 of the ladder actually runs on a host that
 * supports it. The setup follows the standard AF_XDP dance: register the
 * UMEM, size and map the fill/completion rings, size and map the RX/TX
 * rings, then bind the socket to one interface queue.
 *
 * The code compiles on any POSIX host so it can be type-checked off Linux,
 * but the socket options it sets are AF_XDP specific, so create() returns a
 * negative errno on a kernel without AF_XDP. Callers detect that and fall
 * back to the tier0 UDP path. */

/* rewsr_xsk_config sizes the socket. Defaults (applied for zero fields by
 * create) are 4096 frames of 2048 bytes and 2048-entry rings. */
struct rewsr_xsk_config {
    uint32_t num_frames;
    uint32_t frame_size;
    uint32_t fill_size;
    uint32_t comp_size;
    uint32_t rx_size;
    uint32_t tx_size;
    uint32_t bind_flags; /* REWSR_XDP_COPY, REWSR_XDP_ZEROCOPY, ... */
};

struct rewsr_xsk_umem {
    void *area;          /* mmap'd packet buffer */
    size_t area_size;
    uint32_t frame_size;
    uint32_t num_frames;
    int fd;              /* socket that owns the UMEM registration */

    struct rewsr_xsk_ring fill;
    struct rewsr_xsk_ring comp;
    void *fill_map;
    size_t fill_map_size;
    void *comp_map;
    size_t comp_map_size;

    struct rewsr_frame_pool pool;
    uint64_t *pool_stack;
};

struct rewsr_xsk_socket {
    int fd;
    uint32_t ifindex;
    uint32_t queue_id;
    struct rewsr_xsk_umem *umem;

    struct rewsr_xsk_ring rx;
    struct rewsr_xsk_ring tx;
    void *rx_map;
    size_t rx_map_size;
    void *tx_map;
    size_t tx_map_size;

    /* Whether the socket owns its UMEM fd (created it) or shares one. */
    int owns_umem;
};

/* rewsr_xsk_config_default fills cfg with the standard sizes. */
void rewsr_xsk_config_default(struct rewsr_xsk_config *cfg);

/* rewsr_xsk_umem_create allocates and registers a UMEM and maps its fill
 * and completion rings. On success umem is fully initialized and every
 * frame is free in its pool. Returns 0, or a negative errno on failure
 * (including -ENOTSUP-equivalent on a non-AF_XDP kernel). */
int rewsr_xsk_umem_create(struct rewsr_xsk_umem *umem,
                          const struct rewsr_xsk_config *cfg);

/* rewsr_xsk_umem_destroy unmaps the rings and UMEM and closes the fd. */
void rewsr_xsk_umem_destroy(struct rewsr_xsk_umem *umem);

/* rewsr_xsk_socket_create creates an AF_XDP socket on umem, maps its RX and
 * TX rings, and binds it to ifname's queue_id. ifname is resolved to an
 * ifindex internally. Returns 0 or a negative errno. */
int rewsr_xsk_socket_create(struct rewsr_xsk_socket *xsk,
                            struct rewsr_xsk_umem *umem, const char *ifname,
                            uint32_t queue_id,
                            const struct rewsr_xsk_config *cfg);

/* rewsr_xsk_socket_destroy unmaps the RX/TX rings and closes the fd. */
void rewsr_xsk_socket_destroy(struct rewsr_xsk_socket *xsk);

/* rewsr_xsk_fill_refill posts up to nb free frames onto the fill ring so
 * the kernel has somewhere to put received packets. Returns the number
 * posted. Call it at startup and after consuming RX descriptors. */
uint32_t rewsr_xsk_fill_refill(struct rewsr_xsk_umem *umem, uint32_t nb);

/* rewsr_xsk_rx_burst receives up to nb frames. For each, cb is called with
 * the frame data pointer and length; after cb returns the frame is
 * recycled to the pool. Returns the number of frames received. */
typedef void (*rewsr_xsk_rx_cb)(const uint8_t *data, uint32_t len,
                                void *user);
uint32_t rewsr_xsk_rx_burst(struct rewsr_xsk_socket *xsk, uint32_t nb,
                            rewsr_xsk_rx_cb cb, void *user);

/* rewsr_xsk_tx_burst transmits nb frames built by cb. cb receives a frame
 * buffer to fill and returns the number of bytes written, or 0 to skip.
 * Returns the number of frames queued. The caller must ensure the TX ring
 * and completion ring are serviced; rewsr_xsk_tx_complete reclaims sent
 * frames. */
typedef uint32_t (*rewsr_xsk_tx_cb)(uint8_t *data, uint32_t cap, void *user);
uint32_t rewsr_xsk_tx_burst(struct rewsr_xsk_socket *xsk, uint32_t nb,
                            rewsr_xsk_tx_cb cb, void *user);

/* rewsr_xsk_tx_complete reclaims up to nb completed TX frames from the
 * completion ring back into the pool and returns how many were reclaimed. */
uint32_t rewsr_xsk_tx_complete(struct rewsr_xsk_umem *umem, uint32_t nb);

/* rewsr_xsk_kick wakes the kernel to process the TX ring when the socket
 * was created with need-wakeup semantics. Returns 0 or a negative errno. */
int rewsr_xsk_kick(struct rewsr_xsk_socket *xsk);

#endif /* REWSR_XDP_XSK_H */
