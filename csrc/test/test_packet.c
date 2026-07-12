#include "ctest.h"

#include "../net/packet.h"

#include <string.h>

static const uint8_t SRC_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t DST_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

/* 10.0.0.99 -> 10.0.0.12, ports 40000 -> 4789 (a plausible overlay port). */
#define SRC_IP 0x0a000063u
#define DST_IP 0x0a00000cu
#define SRC_PORT 40000
#define DST_PORT 4789

static int test_byte_order_roundtrip(void) {
    /* Network order is big endian, so 0x1234 on the wire reads as the
     * bytes 0x12 0x34; the low-level value seen on a little-endian host is
     * the swapped 0x3412. */
    uint16_t net = rewsr_host_to_net16(0x1234);
    ASSERT_TRUE(net == 0x3412 || net == 0x1234);
    /* host->net->host is identity regardless of platform endianness. */
    ASSERT_EQ_U64(0x1234, rewsr_net_to_host16(rewsr_host_to_net16(0x1234)));
    ASSERT_EQ_U64(0xdeadbeef,
                  rewsr_net_to_host32(rewsr_host_to_net32(0xdeadbeef)));
    return 0;
}

static int test_build_then_parse(void) {
    uint8_t frame[128];
    const char *msg = "fabric-probe";
    int n = rewsr_build_udp_ipv4(frame, sizeof frame, SRC_MAC, DST_MAC,
                                 SRC_IP, DST_IP, SRC_PORT, DST_PORT, 0x1c46,
                                 (const uint8_t *)msg, strlen(msg));
    ASSERT_EQ_INT((int)(REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN +
                        strlen(msg)),
                  n);

    /* Both checksums the builder wrote must verify. */
    ASSERT_TRUE(rewsr_verify_ipv4_checksum(frame, (size_t)n));
    ASSERT_TRUE(rewsr_verify_udp_checksum(frame, (size_t)n));

    struct rewsr_udp_datagram dg;
    ASSERT_EQ_INT(REWSR_PARSE_OK,
                  rewsr_parse_udp_ipv4(frame, (size_t)n, &dg));
    ASSERT_EQ_U64(SRC_IP, dg.src_ip);
    ASSERT_EQ_U64(DST_IP, dg.dst_ip);
    ASSERT_EQ_INT(SRC_PORT, dg.src_port);
    ASSERT_EQ_INT(DST_PORT, dg.dst_port);
    ASSERT_EQ_INT((int)strlen(msg), (int)dg.payload_len);
    ASSERT_MEM_EQ(msg, dg.payload, strlen(msg));
    ASSERT_MEM_EQ(SRC_MAC, dg.src_mac, 6);
    ASSERT_MEM_EQ(DST_MAC, dg.dst_mac, 6);
    return 0;
}

static int test_build_too_small(void) {
    uint8_t frame[8];
    int n = rewsr_build_udp_ipv4(frame, sizeof frame, SRC_MAC, DST_MAC,
                                 SRC_IP, DST_IP, SRC_PORT, DST_PORT, 1,
                                 (const uint8_t *)"x", 1);
    ASSERT_EQ_INT(-1, n);
    return 0;
}

static int test_parse_rejects_non_ipv4(void) {
    uint8_t frame[64];
    memset(frame, 0, sizeof frame);
    /* EtherType 0x0806 (ARP) in the right slot. */
    frame[12] = 0x08;
    frame[13] = 0x06;
    ASSERT_EQ_INT(REWSR_PARSE_NOT_IPV4,
                  rewsr_parse_udp_ipv4(frame, sizeof frame, NULL));
    return 0;
}

static int test_parse_rejects_short(void) {
    uint8_t frame[20];
    memset(frame, 0, sizeof frame);
    ASSERT_EQ_INT(REWSR_PARSE_SHORT,
                  rewsr_parse_udp_ipv4(frame, sizeof frame, NULL));
    return 0;
}

static int test_parse_rejects_lying_length(void) {
    uint8_t frame[128];
    int n = rewsr_build_udp_ipv4(frame, sizeof frame, SRC_MAC, DST_MAC,
                                 SRC_IP, DST_IP, SRC_PORT, DST_PORT, 1,
                                 (const uint8_t *)"payload", 7);
    ASSERT_TRUE(n > 0);
    /* Inflate the IPv4 total length past the real frame; the parser must
     * reject rather than read past the buffer. */
    struct rewsr_ipv4_hdr *ip =
        (struct rewsr_ipv4_hdr *)(frame + REWSR_ETH_HLEN);
    ip->tot_len = rewsr_host_to_net16(9000);
    ASSERT_EQ_INT(REWSR_PARSE_BAD_LEN,
                  rewsr_parse_udp_ipv4(frame, (size_t)n, NULL));
    return 0;
}

static int test_corrupt_payload_breaks_udp_checksum(void) {
    uint8_t frame[128];
    int n = rewsr_build_udp_ipv4(frame, sizeof frame, SRC_MAC, DST_MAC,
                                 SRC_IP, DST_IP, SRC_PORT, DST_PORT, 1,
                                 (const uint8_t *)"integrity", 9);
    ASSERT_TRUE(rewsr_verify_udp_checksum(frame, (size_t)n));
    /* Flip a payload byte: the UDP checksum must now fail while the IPv4
     * header checksum (which does not cover payload) still passes. */
    frame[n - 1] ^= 0x01;
    ASSERT_FALSE(rewsr_verify_udp_checksum(frame, (size_t)n));
    ASSERT_TRUE(rewsr_verify_ipv4_checksum(frame, (size_t)n));
    return 0;
}

REWSR_TEST_MAIN("packet", {
    RUN_TEST(test_byte_order_roundtrip);
    RUN_TEST(test_build_then_parse);
    RUN_TEST(test_build_too_small);
    RUN_TEST(test_parse_rejects_non_ipv4);
    RUN_TEST(test_parse_rejects_short);
    RUN_TEST(test_parse_rejects_lying_length);
    RUN_TEST(test_corrupt_payload_breaks_udp_checksum);
})
