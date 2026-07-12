#ifndef REWSR_DATAPLANE_WORKER_H
#define REWSR_DATAPLANE_WORKER_H

#include <stdint.h>

#include "stats.h"
#include "../net/pktgen.h"
#include "../xdp/xsk.h"

/* worker is the tier1 data-plane loop: it owns one AF_XDP socket bound to
 * one interface queue, pinned to one CPU, and runs a receive-transmit cycle
 * driving the packet generator on the TX side and validating frames on the
 * RX side. It is the piece that turns all the lower-level modules (xsk
 * rings, frame pool, pktgen, checksum, affinity) into a running fabric
 * endpoint.
 *
 * The run loop uses AF_XDP and so is Linux only; rewsr_dp_worker_run returns
 * -ENOTSUP off Linux. The setup, teardown, and stats accounting are shared,
 * and the frame-handling decision logic (rewsr_dp_classify) is factored out
 * as pure code so it can be unit tested without a socket. */

/* rewsr_dp_role selects what a worker does with the queue. */
enum rewsr_dp_role {
    REWSR_DP_ROLE_SOURCE = 0, /* generate and transmit */
    REWSR_DP_ROLE_SINK = 1,   /* receive and validate */
    REWSR_DP_ROLE_ECHO = 2    /* receive, then transmit back */
};

struct rewsr_dp_config {
    char ifname[16];
    uint32_t queue_id;
    int cpu;                 /* CPU to pin to, or -1 for none */
    enum rewsr_dp_role role;
    uint32_t batch;          /* frames per RX/TX burst */
    uint32_t payload_len;    /* SOURCE frame payload size */
    uint8_t src_mac[6];
    uint8_t dst_mac[6];
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint64_t max_packets;    /* stop after this many, 0 for unlimited */
};

struct rewsr_dp_worker {
    struct rewsr_dp_config cfg;
    struct rewsr_xsk_umem umem;
    struct rewsr_xsk_socket xsk;
    struct rewsr_pktgen gen;
    struct rewsr_dp_stats stats;
    volatile int stop; /* set by another thread to end the loop */
};

/* rewsr_dp_classify decides what to do with a received frame of the given
 * length: whether it is a valid data frame the sink should count, and its
 * payload length. Returns 1 for a countable data frame (out_payload set),
 * 0 for a frame to ignore. This is the pure kernel of the RX path, tested
 * against crafted frames without a socket. */
int rewsr_dp_classify(const uint8_t *frame, uint32_t frame_len,
                      uint32_t *out_payload);

/* rewsr_dp_worker_init sets up the UMEM, socket, generator, and affinity
 * for cfg. Returns 0 or a negative errno (including the AF_XDP unsupported
 * case). */
int rewsr_dp_worker_init(struct rewsr_dp_worker *w,
                         const struct rewsr_dp_config *cfg);

/* rewsr_dp_worker_run drives the loop until stop is set or max_packets is
 * reached, accumulating into w->stats. Linux only. */
int rewsr_dp_worker_run(struct rewsr_dp_worker *w);

/* rewsr_dp_worker_destroy tears everything down. */
void rewsr_dp_worker_destroy(struct rewsr_dp_worker *w);

#endif /* REWSR_DATAPLANE_WORKER_H */
