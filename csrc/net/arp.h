#ifndef REWSR_NET_ARP_H
#define REWSR_NET_ARP_H

#include <stddef.h>
#include <stdint.h>

#include "packet.h"

/* arp builds and parses the ARP frames the AF_XDP data plane needs to
 * resolve a next-hop MAC before it can send, and to answer resolution for
 * its own address so peers can reach it. Building the whole path in
 * userspace means the tier1 sender is not dependent on the kernel's ARP
 * cache for a raw-XDP flow. The frame construction and parsing are pure and
 * unit tested; nothing here does I/O. */

#define REWSR_ARP_HTYPE_ETHERNET 1
#define REWSR_ARP_PTYPE_IPV4 0x0800
#define REWSR_ARP_OP_REQUEST 1
#define REWSR_ARP_OP_REPLY 2
#define REWSR_ETH_P_ARP 0x0806
#define REWSR_ARP_FRAME_LEN (REWSR_ETH_HLEN + 28)

/* rewsr_arp is a parsed, host-order view of an ARP message. */
struct rewsr_arp {
    uint16_t oper;
    uint8_t sender_mac[REWSR_ETH_ALEN];
    uint32_t sender_ip; /* host order */
    uint8_t target_mac[REWSR_ETH_ALEN];
    uint32_t target_ip; /* host order */
};

/* rewsr_arp_build_request writes a broadcast ARP request asking "who has
 * target_ip, tell sender_ip" into out (which must hold REWSR_ARP_FRAME_LEN
 * bytes). Returns the frame length or -1 if out_cap is too small. */
int rewsr_arp_build_request(uint8_t *out, size_t out_cap,
                            const uint8_t sender_mac[REWSR_ETH_ALEN],
                            uint32_t sender_ip, uint32_t target_ip);

/* rewsr_arp_build_reply writes a unicast ARP reply telling target that
 * sender_ip is at sender_mac. Returns the frame length or -1. */
int rewsr_arp_build_reply(uint8_t *out, size_t out_cap,
                          const uint8_t sender_mac[REWSR_ETH_ALEN],
                          uint32_t sender_ip,
                          const uint8_t target_mac[REWSR_ETH_ALEN],
                          uint32_t target_ip);

/* rewsr_arp_parse validates and parses an ARP-over-Ethernet frame into out.
 * Returns 0 on success, or a negative rewsr_parse_err if the frame is too
 * short, not ARP, or not Ethernet/IPv4 ARP. */
int rewsr_arp_parse(const uint8_t *frame, size_t frame_len,
                    struct rewsr_arp *out);

/* rewsr_arp_is_request_for returns 1 if frame is an ARP request for ip,
 * the quick test the data plane uses to decide whether to answer. */
int rewsr_arp_is_request_for(const uint8_t *frame, size_t frame_len,
                             uint32_t ip);

#endif /* REWSR_NET_ARP_H */
