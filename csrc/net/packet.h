#ifndef REWSR_NET_PACKET_H
#define REWSR_NET_PACKET_H

#include <stddef.h>
#include <stdint.h>

/* packet builds and parses the Ethernet / IPv4 / UDP headers the tier0
 * and AF_XDP data planes put on the wire. It operates entirely on caller
 * buffers with explicit lengths and never allocates, so it is safe to
 * call from the fast path and portable enough to unit test anywhere.
 *
 * All multi-byte header fields are stored and returned in network byte
 * order; the helpers below convert to and from host order at the edges so
 * the rest of the code never open-codes a byte swap. */

#define REWSR_ETH_ALEN 6
#define REWSR_ETH_HLEN 14
#define REWSR_IPV4_HLEN 20
#define REWSR_UDP_HLEN 8
#define REWSR_ETH_P_IP 0x0800
#define REWSR_IPPROTO_UDP 17

/* rewsr_eth_hdr mirrors the on-wire Ethernet II header. It is declared
 * packed so it can overlay a raw frame buffer directly. */
#pragma pack(push, 1)
struct rewsr_eth_hdr {
    uint8_t dst[REWSR_ETH_ALEN];
    uint8_t src[REWSR_ETH_ALEN];
    uint16_t ethertype; /* network order */
};

struct rewsr_ipv4_hdr {
    uint8_t ver_ihl;    /* version in high nibble, header length in low */
    uint8_t tos;
    uint16_t tot_len;   /* network order */
    uint16_t id;        /* network order */
    uint16_t frag_off;  /* network order */
    uint8_t ttl;
    uint8_t proto;
    uint16_t check;     /* network order */
    uint32_t saddr;     /* network order */
    uint32_t daddr;     /* network order */
};

struct rewsr_udp_hdr {
    uint16_t source; /* network order */
    uint16_t dest;   /* network order */
    uint16_t len;    /* network order */
    uint16_t check;  /* network order */
};
#pragma pack(pop)

/* rewsr_udp_datagram is a parsed, host-order view of a UDP-over-IPv4
 * frame. Addresses and ports are host order here for the convenience of
 * callers inspecting a received packet; the raw headers keep network
 * order. payload points into the original frame buffer, it is not copied. */
struct rewsr_udp_datagram {
    uint8_t src_mac[REWSR_ETH_ALEN];
    uint8_t dst_mac[REWSR_ETH_ALEN];
    uint32_t src_ip; /* host order */
    uint32_t dst_ip; /* host order */
    uint16_t src_port;
    uint16_t dst_port;
    const uint8_t *payload;
    size_t payload_len;
};

/* rewsr_host_to_net16 / rewsr_net_to_host16 and the 32-bit forms convert
 * between host and network byte order without pulling in <arpa/inet.h>,
 * so the packet layer stays free of platform networking headers and is
 * usable from the AF_XDP path which must not depend on libc networking. */
uint16_t rewsr_host_to_net16(uint16_t v);
uint16_t rewsr_net_to_host16(uint16_t v);
uint32_t rewsr_host_to_net32(uint32_t v);
uint32_t rewsr_net_to_host32(uint32_t v);

/* rewsr_build_udp_ipv4 writes a complete Ethernet+IPv4+UDP frame carrying
 * payload into out, filling both the IPv4 and UDP checksums. Addresses and
 * ports are given in host order and converted internally. Returns the
 * total frame length, or -1 if out_cap is too small. ip_id is the value
 * for the IPv4 identification field, which the caller usually increments
 * per packet. */
int rewsr_build_udp_ipv4(uint8_t *out, size_t out_cap,
                         const uint8_t src_mac[REWSR_ETH_ALEN],
                         const uint8_t dst_mac[REWSR_ETH_ALEN],
                         uint32_t src_ip, uint32_t dst_ip,
                         uint16_t src_port, uint16_t dst_port,
                         uint16_t ip_id,
                         const uint8_t *payload, size_t payload_len);

/* rewsr_parse_udp_ipv4 validates and parses a received frame into out.
 * It checks the EtherType, IPv4 version and header length, protocol, and
 * that the declared lengths fit within frame_len. It returns 0 on success,
 * or a negative rewsr_parse_err on the first failed check, so a caller can
 * tell "not UDP for us" from "malformed". Checksums are NOT verified here;
 * use rewsr_verify_ipv4_checksum / rewsr_verify_udp_checksum for that. */
int rewsr_parse_udp_ipv4(const uint8_t *frame, size_t frame_len,
                         struct rewsr_udp_datagram *out);

/* rewsr_verify_ipv4_checksum returns 1 if the IPv4 header checksum in the
 * frame is correct, 0 otherwise. frame must point at the Ethernet header. */
int rewsr_verify_ipv4_checksum(const uint8_t *frame, size_t frame_len);

/* rewsr_verify_udp_checksum returns 1 if the UDP checksum is correct (or
 * zero, which IPv4 permits to mean "not computed"), 0 otherwise. */
int rewsr_verify_udp_checksum(const uint8_t *frame, size_t frame_len);

/* Parse error codes, all negative so 0 stays "ok". */
enum rewsr_parse_err {
    REWSR_PARSE_OK = 0,
    REWSR_PARSE_SHORT = -1,       /* frame shorter than the headers require */
    REWSR_PARSE_NOT_IPV4 = -2,    /* EtherType is not IPv4 */
    REWSR_PARSE_BAD_IHL = -3,     /* IPv4 version/IHL invalid */
    REWSR_PARSE_NOT_UDP = -4,     /* IPv4 protocol is not UDP */
    REWSR_PARSE_BAD_LEN = -5      /* a declared length overruns the frame */
};

#endif /* REWSR_NET_PACKET_H */
