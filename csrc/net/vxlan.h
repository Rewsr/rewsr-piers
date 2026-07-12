#ifndef REWSR_NET_VXLAN_H
#define REWSR_NET_VXLAN_H

#include <stddef.h>
#include <stdint.h>

#include "packet.h"

/* vxlan encapsulates and decapsulates the overlay frames a multi-tenant
 * fabric carries between hosts. An inner Ethernet frame (a tenant's L2
 * packet) is wrapped in a VXLAN header carrying its 24-bit segment id, then
 * an outer UDP/IPv4/Ethernet header addressed host to host. This is the
 * standard RFC 7348 encapsulation; doing it in the data plane lets the
 * fabric move tenant traffic without the kernel's vxlan device in the path.
 *
 * The whole thing is buffer work with no I/O, so it builds and is tested on
 * any platform. */

#define REWSR_VXLAN_HLEN 8
#define REWSR_VXLAN_PORT 4789
#define REWSR_VXLAN_FLAG_VNI 0x08 /* the "I" flag: VNI field is valid */

/* Total bytes added to an inner frame by encapsulation: outer Ethernet,
 * IPv4, UDP, and the VXLAN header. */
#define REWSR_VXLAN_OVERHEAD \
    (REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN + REWSR_VXLAN_HLEN)

/* rewsr_vxlan_hdr is the 8-byte VXLAN header: a flags byte, three reserved
 * bytes, a 24-bit VNI, and a final reserved byte. */
#pragma pack(push, 1)
struct rewsr_vxlan_hdr {
    uint8_t flags;
    uint8_t reserved1[3];
    uint8_t vni[3]; /* 24-bit network id, big endian */
    uint8_t reserved2;
};
#pragma pack(pop)

/* rewsr_vxlan_tunnel describes the outer addressing for an encapsulated
 * frame: the host-to-host MACs and IPs and the source UDP port (which is
 * usually a hash of the inner flow for ECMP spreading, supplied by the
 * caller). */
struct rewsr_vxlan_tunnel {
    uint8_t outer_src_mac[REWSR_ETH_ALEN];
    uint8_t outer_dst_mac[REWSR_ETH_ALEN];
    uint32_t outer_src_ip; /* host order */
    uint32_t outer_dst_ip; /* host order */
    uint16_t src_port;     /* entropy port for ECMP */
    uint32_t vni;          /* 24-bit segment id */
};

/* rewsr_vxlan_encap wraps inner_frame (a complete inner Ethernet frame) in
 * the tunnel's outer headers and writes the result to out. Returns the
 * total encapsulated length, or -1 if out_cap is too small or the VNI does
 * not fit in 24 bits. The outer IPv4 and UDP checksums are filled; the UDP
 * checksum may be zero, which VXLAN permits. */
int rewsr_vxlan_encap(uint8_t *out, size_t out_cap,
                      const struct rewsr_vxlan_tunnel *tun,
                      const uint8_t *inner_frame, size_t inner_len,
                      uint16_t ip_id);

/* rewsr_vxlan_decap validates an encapsulated frame and returns, without
 * copying, a pointer to the inner Ethernet frame and its length, plus the
 * decoded VNI. Returns 0 on success or a negative rewsr_parse_err. It
 * checks that the outer frame is UDP to the VXLAN port and that the VXLAN
 * flags mark the VNI valid. */
int rewsr_vxlan_decap(const uint8_t *frame, size_t frame_len,
                      const uint8_t **inner_out, size_t *inner_len_out,
                      uint32_t *vni_out);

/* rewsr_vxlan_read_vni extracts the 24-bit VNI from a VXLAN header's vni
 * field (big endian). */
uint32_t rewsr_vxlan_read_vni(const uint8_t vni[3]);

/* rewsr_vxlan_write_vni writes a 24-bit VNI big endian into a header field. */
void rewsr_vxlan_write_vni(uint8_t vni[3], uint32_t value);

#endif /* REWSR_NET_VXLAN_H */
