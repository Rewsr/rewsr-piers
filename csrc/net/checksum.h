#ifndef REWSR_NET_CHECKSUM_H
#define REWSR_NET_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

/* checksum implements the Internet checksum (RFC 1071): the 16-bit one's
 * complement of the one's complement sum of the data taken 16 bits at a
 * time. Every header this fabric emits (IPv4, UDP, ICMP) carries one, and
 * the data-plane fast path recomputes them per packet, so the routine has
 * to be both correct at the bit level and cheap. Nothing here touches the
 * network or any Linux header, so it builds and is tested on every
 * platform. */

/* rewsr_cksum_partial accumulates the one's complement 32-bit sum of len
 * bytes into an existing running sum. Feeding the returned value back in
 * as seed lets a caller checksum a scattered header and payload without a
 * temporary contiguous buffer (the IPv4 pseudo-header case). The result
 * is NOT folded to 16 bits; call rewsr_cksum_fold to finish.
 *
 * Composition rule: seeding one partial sum into the next is only valid
 * when every segment except the last has even length. An odd-length
 * segment places its final byte in the high half of a 16-bit word, so a
 * following segment would start in the wrong half. Callers that chain
 * segments (the UDP checksum path) therefore keep any odd-length data
 * last. */
uint32_t rewsr_cksum_partial(const void *data, size_t len, uint32_t seed);

/* rewsr_cksum_fold collapses a 32-bit accumulator down to a folded 16-bit
 * value by adding the carries back in until none remain. */
uint16_t rewsr_cksum_fold(uint32_t sum);

/* rewsr_cksum computes the finished Internet checksum of a single buffer:
 * the value that goes on the wire, already one's-complemented, so a
 * receiver that sums the header including this field gets 0xffff. */
uint16_t rewsr_cksum(const void *data, size_t len);

/* rewsr_cksum_ipv4_pseudo returns the partial sum (unfolded) of the IPv4
 * pseudo-header used by TCP and UDP checksums: source and destination
 * address, the protocol number, and the L4 length. The caller folds this
 * together with the L4 header and payload sums. Addresses are passed in
 * network byte order, as they sit in the packet. */
uint32_t rewsr_cksum_ipv4_pseudo(uint32_t src_be, uint32_t dst_be,
                                 uint8_t proto, uint16_t l4_len);

/* rewsr_cksum_update recomputes a checksum incrementally after a 16-bit
 * field changes from old to new, per RFC 1624. This is what lets the fast
 * path rewrite a UDP port or an IP address without rescanning the whole
 * packet. old_check is the current on-wire checksum; the return is the new
 * on-wire checksum. */
uint16_t rewsr_cksum_update(uint16_t old_check, uint16_t old_field,
                            uint16_t new_field);

#endif /* REWSR_NET_CHECKSUM_H */
