#ifndef REWSR_NET_PKTGEN_H
#define REWSR_NET_PKTGEN_H

#include <stddef.h>
#include <stdint.h>

#include "packet.h"

/* pktgen is the send-side fast path. Building a full Ethernet/IPv4/UDP
 * frame from scratch per packet, as rewsr_build_udp_ipv4 does, recomputes
 * two checksums over the whole frame every time. A load generator or a
 * data-plane replicator sends millions of nearly identical frames that
 * differ only in an IPv4 id, a sequence number in the payload, and a
 * couple of derived checksum bytes. pktgen precomputes the invariant part
 * once and updates only what changes, using the incremental checksum, so
 * the per-packet cost drops to a handful of stores.
 *
 * The generator owns a template frame. rewsr_pktgen_next stamps the
 * template into a caller buffer with the next id and sequence and fixes
 * the checksums incrementally. It is single-threaded state; give each
 * sender thread its own generator. */

struct rewsr_pktgen {
    uint8_t tmpl[REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN + 2048];
    size_t frame_len;    /* total template length including payload */
    size_t payload_len;  /* payload bytes */

    uint16_t base_ip_id; /* id stamped at construction */
    uint16_t ip_id;      /* next id to stamp, incremented per packet */
    uint64_t seq;        /* next sequence number, written into payload[0..7] */

    /* Cached checksums of the template so per-packet updates can be
     * incremental rather than full recomputes. */
    uint16_t ip_check_base;
    uint16_t udp_check_base;
};

/* rewsr_pktgen_init builds the template frame once. payload_len must be at
 * least 8 (the sequence number occupies the first 8 payload bytes) and at
 * most 2048. The payload after the sequence field is filled with a fixed
 * pattern so every generated frame has a valid, checksummed body. Returns
 * 0 on success, -1 on a bad argument. */
int rewsr_pktgen_init(struct rewsr_pktgen *g,
                      const uint8_t src_mac[REWSR_ETH_ALEN],
                      const uint8_t dst_mac[REWSR_ETH_ALEN],
                      uint32_t src_ip, uint32_t dst_ip,
                      uint16_t src_port, uint16_t dst_port,
                      size_t payload_len);

/* rewsr_pktgen_next writes the next frame into out (which must hold at
 * least g->frame_len bytes), advancing the id and sequence. It fixes the
 * IPv4 id and header checksum and the payload sequence and UDP checksum
 * incrementally. Returns the frame length, or -1 if out_cap is too small.
 * The generated frame passes rewsr_verify_ipv4_checksum and
 * rewsr_verify_udp_checksum. */
int rewsr_pktgen_next(struct rewsr_pktgen *g, uint8_t *out, size_t out_cap);

/* rewsr_pktgen_frame_len returns the length of every frame this generator
 * produces. */
size_t rewsr_pktgen_frame_len(const struct rewsr_pktgen *g);

/* rewsr_pktgen_read_seq extracts the sequence number a generated frame
 * carries, for a receiver checking ordering and loss. frame must point at
 * the Ethernet header. Returns 0 on success, negative on a parse failure. */
int rewsr_pktgen_read_seq(const uint8_t *frame, size_t frame_len,
                          uint64_t *seq_out);

#endif /* REWSR_NET_PKTGEN_H */
