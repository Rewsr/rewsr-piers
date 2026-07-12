#ifndef REWSR_NET_VLAN_H
#define REWSR_NET_VLAN_H

#include <stddef.h>
#include <stdint.h>

#include "packet.h"

/* vlan inserts, removes, and reads 802.1Q VLAN tags on Ethernet frames. A
 * multi-tenant fabric often rides tenant traffic on VLANs beneath the
 * overlay, and the data plane has to tag on egress and untag on ingress
 * without going through the kernel's VLAN device. The tag is a 4-byte
 * shim inserted after the source MAC: a 2-byte TPID (0x8100) and a 2-byte
 * TCI holding a 3-bit priority, a drop-eligible bit, and a 12-bit VLAN id.
 * All operations are buffer edits with explicit lengths, tested anywhere. */

#define REWSR_ETH_P_8021Q 0x8100
#define REWSR_VLAN_TAG_LEN 4
#define REWSR_VLAN_VID_MASK 0x0fff
#define REWSR_VLAN_PCP_SHIFT 13
#define REWSR_VLAN_DEI_SHIFT 12

/* rewsr_vlan_tci packs a priority (0..7), drop-eligible bit, and 12-bit
 * VLAN id into the 16-bit Tag Control Information field (host order). */
uint16_t rewsr_vlan_tci(uint8_t pcp, int dei, uint16_t vid);

/* rewsr_vlan_vid / _pcp / _dei extract the fields from a TCI. */
uint16_t rewsr_vlan_vid(uint16_t tci);
uint8_t rewsr_vlan_pcp(uint16_t tci);
int rewsr_vlan_dei(uint16_t tci);

/* rewsr_vlan_insert adds an 802.1Q tag to the frame in buf (frame_len
 * bytes, buffer capacity cap). It shifts the payload after the 12-byte
 * MAC pair right by 4 and writes the TPID and TCI. Returns the new frame
 * length, or -1 if the buffer cannot hold four more bytes or the frame is
 * too short to be Ethernet. */
int rewsr_vlan_insert(uint8_t *buf, size_t frame_len, size_t cap,
                      uint16_t tci);

/* rewsr_vlan_strip removes an 802.1Q tag if present, shifting the payload
 * back left by 4. Returns the new frame length, or the unchanged length if
 * the frame was not tagged. If tci_out is non-NULL it receives the removed
 * tag's TCI (host order) when a tag was stripped. */
int rewsr_vlan_strip(uint8_t *buf, size_t frame_len, uint16_t *tci_out);

/* rewsr_vlan_is_tagged reports whether the frame carries an 802.1Q tag. */
int rewsr_vlan_is_tagged(const uint8_t *buf, size_t frame_len);

/* rewsr_vlan_read returns 0 and fills *tci_out if the frame is tagged, or
 * -1 if it is not. */
int rewsr_vlan_read(const uint8_t *buf, size_t frame_len, uint16_t *tci_out);

#endif /* REWSR_NET_VLAN_H */
