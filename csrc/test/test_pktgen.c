#include "ctest.h"

#include "../net/pktgen.h"
#include "../net/packet.h"

static const uint8_t SM[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t DM[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

static int test_init_rejects_bad_payload(void) {
    struct rewsr_pktgen g;
    ASSERT_EQ_INT(-1, rewsr_pktgen_init(&g, SM, DM, 0x0a000001, 0x0a000002,
                                        1000, 2000, 4));
    ASSERT_EQ_INT(-1, rewsr_pktgen_init(&g, SM, DM, 0x0a000001, 0x0a000002,
                                        1000, 2000, 4096));
    ASSERT_EQ_INT(0, rewsr_pktgen_init(&g, SM, DM, 0x0a000001, 0x0a000002,
                                       1000, 2000, 64));
    return 0;
}

static int test_generated_frames_verify(void) {
    struct rewsr_pktgen g;
    ASSERT_EQ_INT(0, rewsr_pktgen_init(&g, SM, DM, 0x0a000063, 0x0a00000c,
                                       40000, 4789, 128));

    uint8_t buf[2048];
    /* Every generated frame, across many packets, must carry valid IPv4
     * and UDP checksums. This is the real test of the incremental update:
     * a wrong delta shows up as a checksum that no longer verifies. */
    for (int i = 0; i < 1000; i++) {
        int n = rewsr_pktgen_next(&g, buf, sizeof buf);
        ASSERT_TRUE(n > 0);
        if (!rewsr_verify_ipv4_checksum(buf, (size_t)n)) {
            printf("    ipv4 checksum failed at packet %d\n", i);
            rewsr_ctest_fail_count++;
            break;
        }
        if (!rewsr_verify_udp_checksum(buf, (size_t)n)) {
            printf("    udp checksum failed at packet %d\n", i);
            rewsr_ctest_fail_count++;
            break;
        }
    }
    return 0;
}

static int test_sequence_advances_and_reads_back(void) {
    struct rewsr_pktgen g;
    rewsr_pktgen_init(&g, SM, DM, 0x0a000063, 0x0a00000c, 40000, 4789, 64);

    uint8_t buf[2048];
    for (uint64_t expect = 0; expect < 500; expect++) {
        int n = rewsr_pktgen_next(&g, buf, sizeof buf);
        ASSERT_TRUE(n > 0);
        uint64_t seq;
        ASSERT_EQ_INT(0, rewsr_pktgen_read_seq(buf, (size_t)n, &seq));
        ASSERT_EQ_U64(expect, seq);
    }
    return 0;
}

static int test_incremental_matches_full_rebuild(void) {
    /* The incremental generator must produce byte-identical frames to a
     * from-scratch build with the same id and sequence. Build a reference
     * with rewsr_build_udp_ipv4 and compare. */
    struct rewsr_pktgen g;
    rewsr_pktgen_init(&g, SM, DM, 0x0a000063, 0x0a00000c, 40000, 4789, 64);

    uint8_t gen[2048];
    int n = rewsr_pktgen_next(&g, gen, sizeof gen); /* id 0x1000, seq 0 */
    ASSERT_TRUE(n > 0);

    /* Reference payload: seq 0 in first 8 bytes, then the same pattern
     * pktgen uses. */
    uint8_t payload[64];
    for (int i = 0; i < 8; i++) {
        payload[i] = 0;
    }
    for (int i = 8; i < 64; i++) {
        payload[i] = (uint8_t)(0xa0 ^ (i & 0xff));
    }
    uint8_t ref[2048];
    int rn = rewsr_build_udp_ipv4(ref, sizeof ref, SM, DM, 0x0a000063,
                                  0x0a00000c, 40000, 4789, 0x1000, payload,
                                  64);
    ASSERT_EQ_INT(rn, n);
    ASSERT_MEM_EQ(ref, gen, (size_t)n);
    return 0;
}

static int test_frame_len_matches(void) {
    struct rewsr_pktgen g;
    rewsr_pktgen_init(&g, SM, DM, 0x0a000063, 0x0a00000c, 40000, 4789, 100);
    ASSERT_EQ_U64(REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN + 100,
                  rewsr_pktgen_frame_len(&g));
    return 0;
}

REWSR_TEST_MAIN("pktgen", {
    RUN_TEST(test_init_rejects_bad_payload);
    RUN_TEST(test_generated_frames_verify);
    RUN_TEST(test_sequence_advances_and_reads_back);
    RUN_TEST(test_incremental_matches_full_rebuild);
    RUN_TEST(test_frame_len_matches);
})
