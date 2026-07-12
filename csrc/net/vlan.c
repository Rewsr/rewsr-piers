#include "vlan.h"

#include <string.h>

/* The TPID sits at the same offset an untagged frame's EtherType does,
 * right after the 12 bytes of destination and source MAC. */
#define VLAN_TPID_OFF 12
#define VLAN_TCI_OFF 14

uint16_t rewsr_vlan_tci(uint8_t pcp, int dei, uint16_t vid) {
    uint16_t tci = (uint16_t)((pcp & 0x7) << REWSR_VLAN_PCP_SHIFT);
    if (dei) {
        tci |= (uint16_t)(1 << REWSR_VLAN_DEI_SHIFT);
    }
    tci |= (uint16_t)(vid & REWSR_VLAN_VID_MASK);
    return tci;
}

uint16_t rewsr_vlan_vid(uint16_t tci) { return tci & REWSR_VLAN_VID_MASK; }

uint8_t rewsr_vlan_pcp(uint16_t tci) {
    return (uint8_t)((tci >> REWSR_VLAN_PCP_SHIFT) & 0x7);
}

int rewsr_vlan_dei(uint16_t tci) {
    return (tci >> REWSR_VLAN_DEI_SHIFT) & 0x1;
}

int rewsr_vlan_is_tagged(const uint8_t *buf, size_t frame_len) {
    if (frame_len < REWSR_ETH_HLEN) {
        return 0;
    }
    uint16_t tpid = (uint16_t)((buf[VLAN_TPID_OFF] << 8) | buf[VLAN_TPID_OFF + 1]);
    return tpid == REWSR_ETH_P_8021Q;
}

int rewsr_vlan_read(const uint8_t *buf, size_t frame_len, uint16_t *tci_out) {
    if (!rewsr_vlan_is_tagged(buf, frame_len) ||
        frame_len < REWSR_ETH_HLEN + REWSR_VLAN_TAG_LEN) {
        return -1;
    }
    if (tci_out != NULL) {
        *tci_out =
            (uint16_t)((buf[VLAN_TCI_OFF] << 8) | buf[VLAN_TCI_OFF + 1]);
    }
    return 0;
}

int rewsr_vlan_insert(uint8_t *buf, size_t frame_len, size_t cap,
                      uint16_t tci) {
    if (frame_len < REWSR_ETH_HLEN || frame_len + REWSR_VLAN_TAG_LEN > cap) {
        return -1;
    }

    /* Everything from the original EtherType onward moves right by four
     * bytes to make room for the tag. memmove because the ranges overlap. */
    size_t tail = frame_len - VLAN_TPID_OFF;
    memmove(buf + VLAN_TPID_OFF + REWSR_VLAN_TAG_LEN, buf + VLAN_TPID_OFF,
            tail);

    buf[VLAN_TPID_OFF] = (uint8_t)(REWSR_ETH_P_8021Q >> 8);
    buf[VLAN_TPID_OFF + 1] = (uint8_t)(REWSR_ETH_P_8021Q & 0xff);
    buf[VLAN_TCI_OFF] = (uint8_t)(tci >> 8);
    buf[VLAN_TCI_OFF + 1] = (uint8_t)(tci & 0xff);

    return (int)(frame_len + REWSR_VLAN_TAG_LEN);
}

int rewsr_vlan_strip(uint8_t *buf, size_t frame_len, uint16_t *tci_out) {
    if (!rewsr_vlan_is_tagged(buf, frame_len) ||
        frame_len < REWSR_ETH_HLEN + REWSR_VLAN_TAG_LEN) {
        return (int)frame_len;
    }

    if (tci_out != NULL) {
        *tci_out =
            (uint16_t)((buf[VLAN_TCI_OFF] << 8) | buf[VLAN_TCI_OFF + 1]);
    }

    /* Move the inner EtherType and everything after it back over the tag. */
    size_t tail = frame_len - (VLAN_TPID_OFF + REWSR_VLAN_TAG_LEN);
    memmove(buf + VLAN_TPID_OFF, buf + VLAN_TPID_OFF + REWSR_VLAN_TAG_LEN,
            tail);

    return (int)(frame_len - REWSR_VLAN_TAG_LEN);
}
