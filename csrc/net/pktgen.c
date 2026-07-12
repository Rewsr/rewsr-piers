#include "pktgen.h"
#include "checksum.h"

#include <string.h>

/* Offsets of the fields pktgen rewrites per packet, measured from the
 * start of the frame. The IPv4 id and its header checksum, and the UDP
 * checksum, all sit at fixed positions because the template has no IP
 * options. */
#define IP_OFF REWSR_ETH_HLEN
#define IP_ID_OFF (IP_OFF + 4)
#define IP_CHECK_OFF (IP_OFF + 10)
#define UDP_OFF (REWSR_ETH_HLEN + REWSR_IPV4_HLEN)
#define UDP_CHECK_OFF (UDP_OFF + 6)
#define PAYLOAD_OFF (REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN)

int rewsr_pktgen_init(struct rewsr_pktgen *g,
                      const uint8_t src_mac[REWSR_ETH_ALEN],
                      const uint8_t dst_mac[REWSR_ETH_ALEN],
                      uint32_t src_ip, uint32_t dst_ip,
                      uint16_t src_port, uint16_t dst_port,
                      size_t payload_len) {
    if (payload_len < 8 || payload_len > 2048) {
        return -1;
    }

    memset(g, 0, sizeof *g);
    g->payload_len = payload_len;
    g->base_ip_id = 0x1000;
    g->ip_id = g->base_ip_id;
    g->seq = 0;

    /* Fill the payload after the 8-byte sequence field with a fixed,
     * recognizable pattern so a receiver can sanity-check the body and so
     * the template checksum is meaningful. */
    uint8_t payload[2048];
    memset(payload, 0, 8); /* sequence starts at 0 in the template */
    for (size_t i = 8; i < payload_len; i++) {
        payload[i] = (uint8_t)(0xa0 ^ (i & 0xff));
    }

    int n = rewsr_build_udp_ipv4(g->tmpl, sizeof g->tmpl, src_mac, dst_mac,
                                 src_ip, dst_ip, src_port, dst_port,
                                 g->base_ip_id, payload, payload_len);
    if (n < 0) {
        return -1;
    }
    g->frame_len = (size_t)n;

    /* Cache the template's on-wire checksums as host-order values for the
     * incremental updates. Reading the two big-endian bytes as (hi<<8)|lo
     * already yields host order, so no further byte-swap is applied. */
    g->ip_check_base =
        (uint16_t)((g->tmpl[IP_CHECK_OFF] << 8) | g->tmpl[IP_CHECK_OFF + 1]);
    g->udp_check_base =
        (uint16_t)((g->tmpl[UDP_CHECK_OFF] << 8) | g->tmpl[UDP_CHECK_OFF + 1]);
    return 0;
}

/* store_be16 writes a host-order 16-bit value big-endian at p. */
static void store_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

/* load_be16 reads a big-endian 16-bit value at p into host order. */
static uint16_t load_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

int rewsr_pktgen_next(struct rewsr_pktgen *g, uint8_t *out, size_t out_cap) {
    if (out_cap < g->frame_len) {
        return -1;
    }
    memcpy(out, g->tmpl, g->frame_len);

    uint16_t old_id = g->base_ip_id;
    uint16_t new_id = g->ip_id++;

    /* IPv4 id and its header checksum. The id is the only header field
     * that changed from the template, so the header checksum moves by
     * exactly the incremental delta of that one 16-bit word. */
    store_be16(out + IP_ID_OFF, new_id);
    uint16_t ip_check =
        rewsr_cksum_update(g->ip_check_base, old_id, new_id);
    store_be16(out + IP_CHECK_OFF, ip_check);

    /* Sequence number in the first 8 payload bytes, big-endian, and the
     * UDP checksum delta for those four 16-bit words. The template's
     * sequence is zero, so old words are zero. */
    uint64_t seq = g->seq++;
    uint16_t udp_check = g->udp_check_base;
    for (int w = 0; w < 4; w++) {
        uint16_t old_word = load_be16(out + PAYLOAD_OFF + w * 2);
        uint16_t new_word =
            (uint16_t)((seq >> (48 - w * 16)) & 0xffff);
        store_be16(out + PAYLOAD_OFF + w * 2, new_word);
        udp_check = rewsr_cksum_update(udp_check, old_word, new_word);
    }
    store_be16(out + UDP_CHECK_OFF, udp_check);

    return (int)g->frame_len;
}

size_t rewsr_pktgen_frame_len(const struct rewsr_pktgen *g) {
    return g->frame_len;
}

int rewsr_pktgen_read_seq(const uint8_t *frame, size_t frame_len,
                          uint64_t *seq_out) {
    struct rewsr_udp_datagram dg;
    int rc = rewsr_parse_udp_ipv4(frame, frame_len, &dg);
    if (rc != REWSR_PARSE_OK) {
        return rc;
    }
    if (dg.payload_len < 8) {
        return REWSR_PARSE_BAD_LEN;
    }
    uint64_t seq = 0;
    for (int i = 0; i < 8; i++) {
        seq = (seq << 8) | dg.payload[i];
    }
    if (seq_out != NULL) {
        *seq_out = seq;
    }
    return 0;
}
