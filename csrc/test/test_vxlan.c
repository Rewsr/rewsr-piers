#include "ctest.h"

#include "../net/vxlan.h"
#include "../net/packet.h"

#include <string.h>

static struct rewsr_vxlan_tunnel make_tunnel(uint32_t vni) {
    struct rewsr_vxlan_tunnel t;
    memset(&t, 0, sizeof t);
    uint8_t sm[6] = {0x02, 0, 0, 0, 0x0a, 0x01};
    uint8_t dm[6] = {0x02, 0, 0, 0, 0x0a, 0x02};
    memcpy(t.outer_src_mac, sm, 6);
    memcpy(t.outer_dst_mac, dm, 6);
    t.outer_src_ip = 0x0a0a0a01;
    t.outer_dst_ip = 0x0a0a0a02;
    t.src_port = 55555;
    t.vni = vni;
    return t;
}

static int test_vni_read_write(void) {
    uint8_t vni[3];
    rewsr_vxlan_write_vni(vni, 0x123456);
    ASSERT_EQ_INT(0x12, vni[0]);
    ASSERT_EQ_INT(0x34, vni[1]);
    ASSERT_EQ_INT(0x56, vni[2]);
    ASSERT_EQ_U64(0x123456, rewsr_vxlan_read_vni(vni));
    return 0;
}

static int test_encap_decap_roundtrip(void) {
    /* Build an inner Ethernet frame (any bytes with a plausible header). */
    uint8_t inner[100];
    for (int i = 0; i < 100; i++) {
        inner[i] = (uint8_t)(i + 1);
    }

    struct rewsr_vxlan_tunnel tun = make_tunnel(0xabcdef);
    uint8_t outer[512];
    int n = rewsr_vxlan_encap(outer, sizeof outer, &tun, inner, sizeof inner,
                              0x2222);
    ASSERT_EQ_INT((int)(REWSR_VXLAN_OVERHEAD + sizeof inner), n);

    /* The outer IPv4 checksum must verify, and the outer frame must parse
     * as UDP to the VXLAN port. */
    ASSERT_TRUE(rewsr_verify_ipv4_checksum(outer, (size_t)n));

    const uint8_t *rec_inner;
    size_t rec_len;
    uint32_t vni;
    ASSERT_EQ_INT(REWSR_PARSE_OK,
                  rewsr_vxlan_decap(outer, (size_t)n, &rec_inner, &rec_len,
                                    &vni));
    ASSERT_EQ_U64(0xabcdef, vni);
    ASSERT_EQ_U64(sizeof inner, rec_len);
    ASSERT_MEM_EQ(inner, rec_inner, sizeof inner);
    return 0;
}

static int test_encap_rejects_bad_vni(void) {
    struct rewsr_vxlan_tunnel tun = make_tunnel(0x1000000); /* > 24 bits */
    uint8_t outer[256];
    uint8_t inner[32] = {0};
    ASSERT_EQ_INT(-1, rewsr_vxlan_encap(outer, sizeof outer, &tun, inner,
                                        sizeof inner, 1));
    return 0;
}

static int test_encap_rejects_small_buffer(void) {
    struct rewsr_vxlan_tunnel tun = make_tunnel(1);
    uint8_t outer[16];
    uint8_t inner[64] = {0};
    ASSERT_EQ_INT(-1, rewsr_vxlan_encap(outer, sizeof outer, &tun, inner,
                                        sizeof inner, 1));
    return 0;
}

static int test_decap_rejects_wrong_port(void) {
    /* A plain UDP datagram not to the VXLAN port must be rejected. */
    uint8_t sm[6] = {2, 0, 0, 0, 0, 1};
    uint8_t dm[6] = {2, 0, 0, 0, 0, 2};
    uint8_t frame[128];
    uint8_t payload[16] = {0};
    int n = rewsr_build_udp_ipv4(frame, sizeof frame, sm, dm, 0x0a000001,
                                 0x0a000002, 1234, 5678, 1, payload, 16);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ_INT(REWSR_PARSE_NOT_UDP,
                  rewsr_vxlan_decap(frame, (size_t)n, NULL, NULL, NULL));
    return 0;
}

static int test_decap_rejects_missing_vni_flag(void) {
    struct rewsr_vxlan_tunnel tun = make_tunnel(7);
    uint8_t outer[256];
    uint8_t inner[32] = {0};
    int n = rewsr_vxlan_encap(outer, sizeof outer, &tun, inner, sizeof inner,
                              1);
    /* Clear the VNI-valid flag in the VXLAN header; decap must now reject. */
    outer[REWSR_ETH_HLEN + REWSR_IPV4_HLEN + REWSR_UDP_HLEN] = 0x00;
    ASSERT_TRUE(rewsr_vxlan_decap(outer, (size_t)n, NULL, NULL, NULL) !=
                REWSR_PARSE_OK);
    return 0;
}

REWSR_TEST_MAIN("vxlan", {
    RUN_TEST(test_vni_read_write);
    RUN_TEST(test_encap_decap_roundtrip);
    RUN_TEST(test_encap_rejects_bad_vni);
    RUN_TEST(test_encap_rejects_small_buffer);
    RUN_TEST(test_decap_rejects_wrong_port);
    RUN_TEST(test_decap_rejects_missing_vni_flag);
})
