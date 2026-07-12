#include "ctest.h"

#include "../net/checksum.h"

/* A worked IPv4 header from RFC 1071 style references: the 20-byte header
 * 4500 003c 1c46 4000 4006 0000 ac10 0a63 ac10 0a0c has a correct checksum
 * of 0xb1e6 in the zeroed slot. We verify both that we compute it and that
 * summing the completed header folds to zero. */
static int test_ipv4_header_checksum(void) {
    uint8_t hdr[20] = {0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40, 0x00,
                       0x40, 0x06, 0x00, 0x00, 0xac, 0x10, 0x0a, 0x63,
                       0xac, 0x10, 0x0a, 0x0c};
    uint16_t c = rewsr_cksum(hdr, sizeof hdr);
    ASSERT_EQ_U64(0xb1e6, c);

    hdr[10] = (uint8_t)(c >> 8);
    hdr[11] = (uint8_t)(c & 0xff);
    ASSERT_EQ_U64(0, rewsr_cksum(hdr, sizeof hdr));
    return 0;
}

static int test_fold_carries(void) {
    /* 0x1fffe folds to 0xffff (0xfffe + 1). */
    ASSERT_EQ_U64(0xffff, rewsr_cksum_fold(0x1fffe));
    /* 0x00000 stays 0. */
    ASSERT_EQ_U64(0, rewsr_cksum_fold(0));
    /* A value needing two fold rounds: 0x2ffff -> 0x30000&... = 0x0002. */
    ASSERT_EQ_U64(0x0002, rewsr_cksum_fold(0x2ffff));
    return 0;
}

static int test_partial_seed_composes_at_even_offset(void) {
    uint8_t buf[16];
    for (int i = 0; i < 16; i++) {
        buf[i] = (uint8_t)(i * 17 + 3);
    }
    /* Summing the whole buffer in one call must equal summing two halves
     * with the first sum seeded into the second, PROVIDED the split is at
     * an even offset (the documented composition rule). */
    uint32_t whole = rewsr_cksum_partial(buf, 16, 0);
    uint32_t a = rewsr_cksum_partial(buf, 8, 0);
    uint32_t split = rewsr_cksum_partial(buf + 8, 8, a);
    ASSERT_EQ_U64(rewsr_cksum_fold(whole), rewsr_cksum_fold(split));

    /* An odd-offset split must NOT match, which is exactly why the UDP
     * path keeps odd-length payload last. */
    uint32_t bad = rewsr_cksum_partial(buf + 7, 9,
                                       rewsr_cksum_partial(buf, 7, 0));
    ASSERT_TRUE(rewsr_cksum_fold(whole) != rewsr_cksum_fold(bad));
    return 0;
}

static int test_odd_length(void) {
    uint8_t even[4] = {0x12, 0x34, 0x56, 0x78};
    uint8_t odd[3] = {0x12, 0x34, 0x56};
    /* The odd buffer must checksum as if a trailing zero byte were
     * present, i.e. the same as {0x12,0x34,0x56,0x00}. */
    uint8_t padded[4] = {0x12, 0x34, 0x56, 0x00};
    ASSERT_EQ_U64(rewsr_cksum(padded, 4), rewsr_cksum(odd, 3));
    /* Sanity: the even buffer differs from the padded one. */
    ASSERT_TRUE(rewsr_cksum(even, 4) != rewsr_cksum(odd, 3));
    return 0;
}

static int test_incremental_update(void) {
    /* Build a checksum over a field, change the field, and confirm the
     * incremental update matches a full recompute. */
    uint8_t data[4] = {0xaa, 0xbb, 0x00, 0x00};
    uint16_t full_old = rewsr_cksum(data, 4);

    uint16_t old_field = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t new_field = 0x1234;
    data[0] = 0x12;
    data[1] = 0x34;
    uint16_t full_new = rewsr_cksum(data, 4);

    uint16_t incr = rewsr_cksum_update(full_old, old_field, new_field);
    ASSERT_EQ_U64(full_new, incr);
    return 0;
}

REWSR_TEST_MAIN("checksum", {
    RUN_TEST(test_ipv4_header_checksum);
    RUN_TEST(test_fold_carries);
    RUN_TEST(test_partial_seed_composes_at_even_offset);
    RUN_TEST(test_odd_length);
    RUN_TEST(test_incremental_update);
})
