#include "arp.h"

#include <string.h>

/* The ARP payload sits right after the 14-byte Ethernet header. For
 * Ethernet/IPv4 it is 28 bytes: 2 htype, 2 ptype, 1 hlen, 1 plen, 2 oper,
 * then sender MAC (6), sender IP (4), target MAC (6), target IP (4). */
#define ARP_OFF REWSR_ETH_HLEN
#define ARP_HTYPE (ARP_OFF + 0)
#define ARP_PTYPE (ARP_OFF + 2)
#define ARP_HLEN (ARP_OFF + 4)
#define ARP_PLEN (ARP_OFF + 5)
#define ARP_OPER (ARP_OFF + 6)
#define ARP_SHA (ARP_OFF + 8)
#define ARP_SPA (ARP_OFF + 14)
#define ARP_THA (ARP_OFF + 18)
#define ARP_TPA (ARP_OFF + 24)

static const uint8_t BROADCAST_MAC[REWSR_ETH_ALEN] = {0xff, 0xff, 0xff,
                                                      0xff, 0xff, 0xff};

static void store_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

static uint16_t load_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* build_common fills the invariant part of an ARP frame: the Ethernet
 * header and the fixed ARP fields (htype/ptype/hlen/plen/oper and the
 * sender addresses). The two builders differ only in destination MAC and
 * target addresses. */
static void build_common(uint8_t *out, const uint8_t dst_mac[REWSR_ETH_ALEN],
                         const uint8_t sender_mac[REWSR_ETH_ALEN],
                         uint32_t sender_ip, uint16_t oper) {
    struct rewsr_eth_hdr *eth = (struct rewsr_eth_hdr *)out;
    memcpy(eth->dst, dst_mac, REWSR_ETH_ALEN);
    memcpy(eth->src, sender_mac, REWSR_ETH_ALEN);
    eth->ethertype = rewsr_host_to_net16(REWSR_ETH_P_ARP);

    store_be16(out + ARP_HTYPE, REWSR_ARP_HTYPE_ETHERNET);
    store_be16(out + ARP_PTYPE, REWSR_ARP_PTYPE_IPV4);
    out[ARP_HLEN] = REWSR_ETH_ALEN;
    out[ARP_PLEN] = 4;
    store_be16(out + ARP_OPER, oper);
    memcpy(out + ARP_SHA, sender_mac, REWSR_ETH_ALEN);
    store_be32(out + ARP_SPA, sender_ip);
}

int rewsr_arp_build_request(uint8_t *out, size_t out_cap,
                            const uint8_t sender_mac[REWSR_ETH_ALEN],
                            uint32_t sender_ip, uint32_t target_ip) {
    if (out_cap < REWSR_ARP_FRAME_LEN) {
        return -1;
    }
    /* A request is broadcast at layer 2 and leaves the target MAC zero
     * since that is exactly what is being asked for. */
    build_common(out, BROADCAST_MAC, sender_mac, sender_ip,
                 REWSR_ARP_OP_REQUEST);
    memset(out + ARP_THA, 0, REWSR_ETH_ALEN);
    store_be32(out + ARP_TPA, target_ip);
    return REWSR_ARP_FRAME_LEN;
}

int rewsr_arp_build_reply(uint8_t *out, size_t out_cap,
                          const uint8_t sender_mac[REWSR_ETH_ALEN],
                          uint32_t sender_ip,
                          const uint8_t target_mac[REWSR_ETH_ALEN],
                          uint32_t target_ip) {
    if (out_cap < REWSR_ARP_FRAME_LEN) {
        return -1;
    }
    build_common(out, target_mac, sender_mac, sender_ip, REWSR_ARP_OP_REPLY);
    memcpy(out + ARP_THA, target_mac, REWSR_ETH_ALEN);
    store_be32(out + ARP_TPA, target_ip);
    return REWSR_ARP_FRAME_LEN;
}

int rewsr_arp_parse(const uint8_t *frame, size_t frame_len,
                    struct rewsr_arp *out) {
    if (frame_len < REWSR_ARP_FRAME_LEN) {
        return REWSR_PARSE_SHORT;
    }
    const struct rewsr_eth_hdr *eth = (const struct rewsr_eth_hdr *)frame;
    if (rewsr_net_to_host16(eth->ethertype) != REWSR_ETH_P_ARP) {
        return REWSR_PARSE_NOT_IPV4; /* not ARP: reuse the ethertype-mismatch code */
    }
    /* Only Ethernet/IPv4 ARP is handled; anything else is rejected rather
     * than misparsed. */
    if (load_be16(frame + ARP_HTYPE) != REWSR_ARP_HTYPE_ETHERNET ||
        load_be16(frame + ARP_PTYPE) != REWSR_ARP_PTYPE_IPV4 ||
        frame[ARP_HLEN] != REWSR_ETH_ALEN || frame[ARP_PLEN] != 4) {
        return REWSR_PARSE_BAD_IHL;
    }

    if (out != NULL) {
        out->oper = load_be16(frame + ARP_OPER);
        memcpy(out->sender_mac, frame + ARP_SHA, REWSR_ETH_ALEN);
        out->sender_ip = load_be32(frame + ARP_SPA);
        memcpy(out->target_mac, frame + ARP_THA, REWSR_ETH_ALEN);
        out->target_ip = load_be32(frame + ARP_TPA);
    }
    return REWSR_PARSE_OK;
}

int rewsr_arp_is_request_for(const uint8_t *frame, size_t frame_len,
                             uint32_t ip) {
    struct rewsr_arp arp;
    if (rewsr_arp_parse(frame, frame_len, &arp) != REWSR_PARSE_OK) {
        return 0;
    }
    return arp.oper == REWSR_ARP_OP_REQUEST && arp.target_ip == ip;
}
