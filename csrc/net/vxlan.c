#include "vxlan.h"
#include "checksum.h"

#include <string.h>

uint32_t rewsr_vxlan_read_vni(const uint8_t vni[3]) {
    return ((uint32_t)vni[0] << 16) | ((uint32_t)vni[1] << 8) |
           (uint32_t)vni[2];
}

void rewsr_vxlan_write_vni(uint8_t vni[3], uint32_t value) {
    vni[0] = (uint8_t)((value >> 16) & 0xff);
    vni[1] = (uint8_t)((value >> 8) & 0xff);
    vni[2] = (uint8_t)(value & 0xff);
}

int rewsr_vxlan_encap(uint8_t *out, size_t out_cap,
                      const struct rewsr_vxlan_tunnel *tun,
                      const uint8_t *inner_frame, size_t inner_len,
                      uint16_t ip_id) {
    if (tun->vni > 0xffffff) {
        return -1;
    }
    size_t total = REWSR_VXLAN_OVERHEAD + inner_len;
    if (total > out_cap) {
        return -1;
    }

    /* Outer Ethernet. */
    struct rewsr_eth_hdr *eth = (struct rewsr_eth_hdr *)out;
    memcpy(eth->dst, tun->outer_dst_mac, REWSR_ETH_ALEN);
    memcpy(eth->src, tun->outer_src_mac, REWSR_ETH_ALEN);
    eth->ethertype = rewsr_host_to_net16(REWSR_ETH_P_IP);

    /* Outer IPv4. The total length covers everything after the Ethernet
     * header: IP, UDP, VXLAN, and the inner frame. */
    struct rewsr_ipv4_hdr *ip =
        (struct rewsr_ipv4_hdr *)(out + REWSR_ETH_HLEN);
    uint16_t ip_total =
        (uint16_t)(REWSR_IPV4_HLEN + REWSR_UDP_HLEN + REWSR_VXLAN_HLEN +
                   inner_len);
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->tot_len = rewsr_host_to_net16(ip_total);
    ip->id = rewsr_host_to_net16(ip_id);
    ip->frag_off = rewsr_host_to_net16(0x4000); /* Don't Fragment */
    ip->ttl = 64;
    ip->proto = REWSR_IPPROTO_UDP;
    ip->check = 0;
    ip->saddr = rewsr_host_to_net32(tun->outer_src_ip);
    ip->daddr = rewsr_host_to_net32(tun->outer_dst_ip);
    ip->check = rewsr_host_to_net16(rewsr_cksum(ip, REWSR_IPV4_HLEN));

    /* Outer UDP to the VXLAN port. */
    struct rewsr_udp_hdr *udp =
        (struct rewsr_udp_hdr *)(out + REWSR_ETH_HLEN + REWSR_IPV4_HLEN);
    uint16_t udp_len =
        (uint16_t)(REWSR_UDP_HLEN + REWSR_VXLAN_HLEN + inner_len);
    udp->source = rewsr_host_to_net16(tun->src_port);
    udp->dest = rewsr_host_to_net16(REWSR_VXLAN_PORT);
    udp->len = rewsr_host_to_net16(udp_len);
    /* VXLAN traffic commonly ships with a zero outer UDP checksum; leaving
     * it zero is standard and saves a full-payload scan on the hot path. */
    udp->check = 0;

    /* VXLAN header. */
    struct rewsr_vxlan_hdr *vx =
        (struct rewsr_vxlan_hdr *)(out + REWSR_ETH_HLEN + REWSR_IPV4_HLEN +
                                   REWSR_UDP_HLEN);
    memset(vx, 0, sizeof *vx);
    vx->flags = REWSR_VXLAN_FLAG_VNI;
    rewsr_vxlan_write_vni(vx->vni, tun->vni);

    /* Inner frame, copied verbatim. */
    uint8_t *inner = out + REWSR_VXLAN_OVERHEAD;
    if (inner_len > 0 && inner_frame != NULL) {
        memcpy(inner, inner_frame, inner_len);
    }

    return (int)total;
}

int rewsr_vxlan_decap(const uint8_t *frame, size_t frame_len,
                      const uint8_t **inner_out, size_t *inner_len_out,
                      uint32_t *vni_out) {
    /* The outer frame must be a well-formed UDP/IPv4 datagram to the VXLAN
     * port, so the generic parser does the header validation and bounds
     * checking before we look at the VXLAN header. */
    struct rewsr_udp_datagram dg;
    int rc = rewsr_parse_udp_ipv4(frame, frame_len, &dg);
    if (rc != REWSR_PARSE_OK) {
        return rc;
    }
    if (dg.dst_port != REWSR_VXLAN_PORT) {
        return REWSR_PARSE_NOT_UDP;
    }
    if (dg.payload_len < REWSR_VXLAN_HLEN) {
        return REWSR_PARSE_BAD_LEN;
    }

    const struct rewsr_vxlan_hdr *vx =
        (const struct rewsr_vxlan_hdr *)dg.payload;
    /* The I flag must be set for the VNI to be meaningful; a header without
     * it is not a VNI-bearing VXLAN packet we can forward. */
    if ((vx->flags & REWSR_VXLAN_FLAG_VNI) == 0) {
        return REWSR_PARSE_BAD_IHL;
    }

    if (vni_out != NULL) {
        *vni_out = rewsr_vxlan_read_vni(vx->vni);
    }
    if (inner_out != NULL) {
        *inner_out = dg.payload + REWSR_VXLAN_HLEN;
    }
    if (inner_len_out != NULL) {
        *inner_len_out = dg.payload_len - REWSR_VXLAN_HLEN;
    }
    return REWSR_PARSE_OK;
}
