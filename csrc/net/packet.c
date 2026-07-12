#include "packet.h"
#include "checksum.h"

#include <string.h>

/* Byte-order helpers are written against a runtime endianness probe rather
 * than a compiler macro so the file has no platform dependencies at all.
 * The probe folds to a constant under any optimizer, so the fast path pays
 * nothing for it. */
static int rewsr_is_little_endian(void) {
    uint16_t x = 1;
    return *(const uint8_t *)&x == 1;
}

uint16_t rewsr_host_to_net16(uint16_t v) {
    if (!rewsr_is_little_endian()) {
        return v;
    }
    return (uint16_t)((v << 8) | (v >> 8));
}

uint16_t rewsr_net_to_host16(uint16_t v) { return rewsr_host_to_net16(v); }

uint32_t rewsr_host_to_net32(uint32_t v) {
    if (!rewsr_is_little_endian()) {
        return v;
    }
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}

uint32_t rewsr_net_to_host32(uint32_t v) { return rewsr_host_to_net32(v); }

int rewsr_build_udp_ipv4(uint8_t *out, size_t out_cap,
                         const uint8_t src_mac[REWSR_ETH_ALEN],
                         const uint8_t dst_mac[REWSR_ETH_ALEN],
                         uint32_t src_ip, uint32_t dst_ip,
                         uint16_t src_port, uint16_t dst_port,
                         uint16_t ip_id,
                         const uint8_t *payload, size_t payload_len) {
    size_t total = REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN + payload_len;
    if (total > out_cap) {
        return -1;
    }

    struct rewsr_eth_hdr *eth = (struct rewsr_eth_hdr *)out;
    memcpy(eth->dst, dst_mac, REWSR_ETH_ALEN);
    memcpy(eth->src, src_mac, REWSR_ETH_ALEN);
    eth->ethertype = rewsr_host_to_net16(REWSR_ETH_P_IP);

    struct rewsr_ipv4_hdr *ip =
        (struct rewsr_ipv4_hdr *)(out + REWSR_ETH_HLEN);
    ip->ver_ihl = 0x45; /* IPv4, 5 32-bit words of header (no options) */
    ip->tos = 0;
    ip->tot_len =
        rewsr_host_to_net16((uint16_t)(REWSR_IPV4_HLEN + REWSR_UDP_HLEN + payload_len));
    ip->id = rewsr_host_to_net16(ip_id);
    ip->frag_off = rewsr_host_to_net16(0x4000); /* Don't Fragment */
    ip->ttl = 64;
    ip->proto = REWSR_IPPROTO_UDP;
    ip->check = 0;
    ip->saddr = rewsr_host_to_net32(src_ip);
    ip->daddr = rewsr_host_to_net32(dst_ip);
    /* rewsr_cksum returns the value as a host integer; the field is
     * network order like every other multi-byte field, so serialize it
     * big-endian. On a big-endian host host_to_net16 is a no-op. */
    ip->check = rewsr_host_to_net16(rewsr_cksum(ip, REWSR_IPV4_HLEN));

    struct rewsr_udp_hdr *udp =
        (struct rewsr_udp_hdr *)(out + REWSR_ETH_HLEN + REWSR_IPV4_HLEN);
    uint16_t udp_len = (uint16_t)(REWSR_UDP_HLEN + payload_len);
    udp->source = rewsr_host_to_net16(src_port);
    udp->dest = rewsr_host_to_net16(dst_port);
    udp->len = rewsr_host_to_net16(udp_len);
    udp->check = 0;

    uint8_t *pl = out + REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN;
    if (payload_len > 0 && payload != NULL) {
        memcpy(pl, payload, payload_len);
    }

    /* UDP checksum spans the IPv4 pseudo-header, the UDP header, and the
     * payload. A computed value of zero is transmitted as 0xffff because
     * an on-wire zero means "no checksum" for UDP. */
    uint32_t sum = rewsr_cksum_ipv4_pseudo(ip->saddr, ip->daddr,
                                           REWSR_IPPROTO_UDP, udp_len);
    sum = rewsr_cksum_partial(udp, REWSR_UDP_HLEN, sum);
    sum = rewsr_cksum_partial(pl, payload_len, sum);
    uint16_t folded = (uint16_t)(~rewsr_cksum_fold(sum) & 0xffff);
    udp->check = rewsr_host_to_net16(folded == 0 ? 0xffff : folded);

    return (int)total;
}

int rewsr_parse_udp_ipv4(const uint8_t *frame, size_t frame_len,
                         struct rewsr_udp_datagram *out) {
    if (frame_len < REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN) {
        return REWSR_PARSE_SHORT;
    }

    const struct rewsr_eth_hdr *eth = (const struct rewsr_eth_hdr *)frame;
    if (rewsr_net_to_host16(eth->ethertype) != REWSR_ETH_P_IP) {
        return REWSR_PARSE_NOT_IPV4;
    }

    const struct rewsr_ipv4_hdr *ip =
        (const struct rewsr_ipv4_hdr *)(frame + REWSR_ETH_HLEN);
    uint8_t version = ip->ver_ihl >> 4;
    uint8_t ihl_words = ip->ver_ihl & 0x0f;
    if (version != 4 || ihl_words < 5) {
        return REWSR_PARSE_BAD_IHL;
    }
    size_t ip_hlen = (size_t)ihl_words * 4;

    uint16_t tot_len = rewsr_net_to_host16(ip->tot_len);
    if ((size_t)REWSR_ETH_HLEN + tot_len > frame_len || tot_len < ip_hlen) {
        return REWSR_PARSE_BAD_LEN;
    }

    if (ip->proto != REWSR_IPPROTO_UDP) {
        return REWSR_PARSE_NOT_UDP;
    }

    const struct rewsr_udp_hdr *udp =
        (const struct rewsr_udp_hdr *)(frame + REWSR_ETH_HLEN + ip_hlen);
    uint16_t udp_len = rewsr_net_to_host16(udp->len);
    if (udp_len < REWSR_UDP_HLEN || (size_t)ip_hlen + udp_len > tot_len) {
        return REWSR_PARSE_BAD_LEN;
    }

    if (out != NULL) {
        memcpy(out->dst_mac, eth->dst, REWSR_ETH_ALEN);
        memcpy(out->src_mac, eth->src, REWSR_ETH_ALEN);
        out->src_ip = rewsr_net_to_host32(ip->saddr);
        out->dst_ip = rewsr_net_to_host32(ip->daddr);
        out->src_port = rewsr_net_to_host16(udp->source);
        out->dst_port = rewsr_net_to_host16(udp->dest);
        out->payload = (const uint8_t *)udp + REWSR_UDP_HLEN;
        out->payload_len = (size_t)(udp_len - REWSR_UDP_HLEN);
    }
    return REWSR_PARSE_OK;
}

int rewsr_verify_ipv4_checksum(const uint8_t *frame, size_t frame_len) {
    if (frame_len < REWSR_ETH_HLEN + REWSR_IPV4_HLEN) {
        return 0;
    }
    const struct rewsr_ipv4_hdr *ip =
        (const struct rewsr_ipv4_hdr *)(frame + REWSR_ETH_HLEN);
    size_t ip_hlen = (size_t)(ip->ver_ihl & 0x0f) * 4;
    if (frame_len < REWSR_ETH_HLEN + ip_hlen) {
        return 0;
    }
    /* A correct header sums (including its own check field) to 0xffff,
     * whose complement folds to zero. */
    return rewsr_cksum(ip, ip_hlen) == 0;
}

int rewsr_verify_udp_checksum(const uint8_t *frame, size_t frame_len) {
    struct rewsr_udp_datagram dg;
    if (rewsr_parse_udp_ipv4(frame, frame_len, &dg) != REWSR_PARSE_OK) {
        return 0;
    }
    const struct rewsr_ipv4_hdr *ip =
        (const struct rewsr_ipv4_hdr *)(frame + REWSR_ETH_HLEN);
    size_t ip_hlen = (size_t)(ip->ver_ihl & 0x0f) * 4;
    const struct rewsr_udp_hdr *udp =
        (const struct rewsr_udp_hdr *)(frame + REWSR_ETH_HLEN + ip_hlen);

    if (udp->check == 0) {
        /* Zero means the sender did not compute a checksum, which IPv4
         * allows; treat as valid rather than forcing a recompute. */
        return 1;
    }

    uint16_t udp_len = rewsr_net_to_host16(udp->len);
    uint32_t sum = rewsr_cksum_ipv4_pseudo(ip->saddr, ip->daddr,
                                           REWSR_IPPROTO_UDP, udp_len);
    sum = rewsr_cksum_partial(udp, udp_len, sum);
    return rewsr_cksum_fold(sum) == 0xffff;
}
