#include "worker.h"

#include "../net/packet.h"
#include "../sys/affinity.h"

#include <errno.h>
#include <string.h>

int rewsr_dp_classify(const uint8_t *frame, uint32_t frame_len,
                      uint32_t *out_payload) {
    struct rewsr_udp_datagram dg;
    if (rewsr_parse_udp_ipv4(frame, frame_len, &dg) != REWSR_PARSE_OK) {
        return 0;
    }
    /* A data frame must carry at least the 8-byte sequence header the
     * generator writes, and its UDP checksum must verify, or it is noise
     * on the wire rather than one of ours. */
    if (dg.payload_len < 8) {
        return 0;
    }
    if (!rewsr_verify_udp_checksum(frame, frame_len)) {
        return 0;
    }
    if (out_payload != NULL) {
        *out_payload = (uint32_t)dg.payload_len;
    }
    return 1;
}

int rewsr_dp_worker_init(struct rewsr_dp_worker *w,
                         const struct rewsr_dp_config *cfg) {
    memset(w, 0, sizeof *w);
    w->cfg = *cfg;

    struct rewsr_xsk_config xcfg;
    rewsr_xsk_config_default(&xcfg);

    int rc = rewsr_xsk_umem_create(&w->umem, &xcfg);
    if (rc != 0) {
        return rc;
    }
    rc = rewsr_xsk_socket_create(&w->xsk, &w->umem, cfg->ifname, cfg->queue_id,
                                 &xcfg);
    if (rc != 0) {
        rewsr_xsk_umem_destroy(&w->umem);
        return rc;
    }

    /* Prime the fill ring so the kernel can receive from the first cycle. */
    rewsr_xsk_fill_refill(&w->umem, xcfg.fill_size);

    if (cfg->role == REWSR_DP_ROLE_SOURCE || cfg->role == REWSR_DP_ROLE_ECHO) {
        uint32_t plen = cfg->payload_len < 8 ? 64 : cfg->payload_len;
        if (rewsr_pktgen_init(&w->gen, cfg->src_mac, cfg->dst_mac,
                              cfg->src_ip, cfg->dst_ip, cfg->src_port,
                              cfg->dst_port, plen) != 0) {
            rewsr_dp_worker_destroy(w);
            return -EINVAL;
        }
    }

    /* Pinning is best effort: a host that denies it still runs, just with
     * less predictable latency, so a failure here is not fatal. */
    if (cfg->cpu >= 0) {
        rewsr_affinity_pin(cfg->cpu);
    }
    return 0;
}

#if defined(__linux__)

/* tx_fill is the pktgen callback the TX burst calls to fill each frame. */
static uint32_t tx_fill(uint8_t *data, uint32_t cap, void *user) {
    struct rewsr_dp_worker *w = user;
    int n = rewsr_pktgen_next(&w->gen, data, cap);
    return n < 0 ? 0 : (uint32_t)n;
}

/* rx_count is the RX callback: classify the frame and bump the counters. */
static void rx_count(const uint8_t *data, uint32_t len, void *user) {
    struct rewsr_dp_worker *w = user;
    uint32_t payload;
    if (rewsr_dp_classify(data, len, &payload)) {
        w->stats.rx_packets++;
        w->stats.rx_bytes += len;
    } else {
        w->stats.parse_errors++;
    }
}

int rewsr_dp_worker_run(struct rewsr_dp_worker *w) {
    const uint32_t batch = w->cfg.batch ? w->cfg.batch : 64;

    while (!w->stop) {
        if (w->cfg.role == REWSR_DP_ROLE_SINK ||
            w->cfg.role == REWSR_DP_ROLE_ECHO) {
            uint32_t got = rewsr_xsk_rx_burst(&w->xsk, batch, rx_count, w);
            (void)got;
        }

        if (w->cfg.role == REWSR_DP_ROLE_SOURCE ||
            w->cfg.role == REWSR_DP_ROLE_ECHO) {
            uint32_t sent = rewsr_xsk_tx_burst(&w->xsk, batch, tx_fill, w);
            if (sent > 0) {
                w->stats.tx_packets += sent;
                w->stats.tx_bytes +=
                    (uint64_t)sent * rewsr_pktgen_frame_len(&w->gen);
                rewsr_xsk_kick(&w->xsk);
            } else {
                w->stats.tx_drops += batch;
            }
            rewsr_xsk_tx_complete(&w->umem, batch);
        }

        if (w->cfg.max_packets > 0 &&
            w->stats.rx_packets + w->stats.tx_packets >= w->cfg.max_packets) {
            break;
        }
    }
    return 0;
}

#else /* !__linux__ */

int rewsr_dp_worker_run(struct rewsr_dp_worker *w) {
    (void)w;
    return -ENOTSUP;
}

#endif /* __linux__ */

void rewsr_dp_worker_destroy(struct rewsr_dp_worker *w) {
    rewsr_xsk_socket_destroy(&w->xsk);
    rewsr_xsk_umem_destroy(&w->umem);
}
