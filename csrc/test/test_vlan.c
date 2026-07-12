#include "ctest.h"

#include "../net/vlan.h"

#include <string.h>

static int test_tci_pack_unpack(void) {
    uint16_t tci = rewsr_vlan_tci(5, 1, 100);
    ASSERT_EQ_INT(5, rewsr_vlan_pcp(tci));
    ASSERT_EQ_INT(1, rewsr_vlan_dei(tci));
    ASSERT_EQ_INT(100, rewsr_vlan_vid(tci));

    /* VID is masked to 12 bits; a value above 4095 wraps. */
    uint16_t big = rewsr_vlan_tci(0, 0, 5000);
    ASSERT_EQ_INT(5000 & 0x0fff, rewsr_vlan_vid(big));
    return 0;
}

/* build_frame lays down a minimal untagged Ethernet frame: dst, src, an
 * IPv4 EtherType, and a few payload bytes. */
static size_t build_frame(uint8_t *buf) {
    /* Writes exactly 18 bytes; callers size buf accordingly. */
    memset(buf, 0, 18);
    for (int i = 0; i < 6; i++) {
        buf[i] = (uint8_t)(0xa0 + i);      /* dst */
        buf[6 + i] = (uint8_t)(0xb0 + i);  /* src */
    }
    buf[12] = 0x08; /* IPv4 */
    buf[13] = 0x00;
    buf[14] = 0xde;
    buf[15] = 0xad;
    buf[16] = 0xbe;
    buf[17] = 0xef;
    return 18;
}

static int test_insert_then_strip_roundtrip(void) {
    uint8_t buf[64];
    size_t len = build_frame(buf);

    uint8_t original[64];
    memcpy(original, buf, len);

    uint16_t tci = rewsr_vlan_tci(3, 0, 42);
    int tagged_len = rewsr_vlan_insert(buf, len, sizeof buf, tci);
    ASSERT_EQ_INT((int)(len + 4), tagged_len);
    ASSERT_TRUE(rewsr_vlan_is_tagged(buf, (size_t)tagged_len));

    /* The MAC addresses are untouched; the tag sits right after them. */
    ASSERT_MEM_EQ(original, buf, 12);
    ASSERT_EQ_INT(0x81, buf[12]);
    ASSERT_EQ_INT(0x00, buf[13]);

    uint16_t read_tci;
    ASSERT_EQ_INT(0, rewsr_vlan_read(buf, (size_t)tagged_len, &read_tci));
    ASSERT_EQ_INT(42, rewsr_vlan_vid(read_tci));
    ASSERT_EQ_INT(3, rewsr_vlan_pcp(read_tci));

    /* Stripping restores the original frame exactly. */
    uint16_t stripped_tci = 0;
    int stripped_len = rewsr_vlan_strip(buf, (size_t)tagged_len, &stripped_tci);
    ASSERT_EQ_INT((int)len, stripped_len);
    ASSERT_EQ_INT(42, rewsr_vlan_vid(stripped_tci));
    ASSERT_MEM_EQ(original, buf, len);
    return 0;
}

static int test_strip_untagged_is_noop(void) {
    uint8_t buf[64];
    size_t len = build_frame(buf);
    uint8_t original[64];
    memcpy(original, buf, len);

    uint16_t tci = 0xffff;
    int r = rewsr_vlan_strip(buf, len, &tci);
    ASSERT_EQ_INT((int)len, r);
    ASSERT_MEM_EQ(original, buf, len);
    /* tci_out is left untouched when nothing was stripped. */
    ASSERT_EQ_INT(0xffff, tci);
    return 0;
}

static int test_insert_rejects_full_buffer(void) {
    uint8_t buf[20];
    size_t len = build_frame(buf); /* 18 bytes */
    /* Only 20 bytes of capacity: no room for a 4-byte tag. */
    ASSERT_EQ_INT(-1, rewsr_vlan_insert(buf, len, sizeof buf, 0x1234));
    return 0;
}

static int test_read_untagged_fails(void) {
    uint8_t buf[64];
    size_t len = build_frame(buf);
    ASSERT_EQ_INT(-1, rewsr_vlan_read(buf, len, NULL));
    ASSERT_FALSE(rewsr_vlan_is_tagged(buf, len));
    return 0;
}

REWSR_TEST_MAIN("vlan", {
    RUN_TEST(test_tci_pack_unpack);
    RUN_TEST(test_insert_then_strip_roundtrip);
    RUN_TEST(test_strip_untagged_is_noop);
    RUN_TEST(test_insert_rejects_full_buffer);
    RUN_TEST(test_read_untagged_fails);
})
