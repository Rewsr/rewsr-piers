#include "ctest.h"

#include "../net/arp.h"

#include <string.h>

static const uint8_t MY_MAC[6] = {0x02, 0x00, 0x11, 0x22, 0x33, 0x44};
static const uint8_t PEER_MAC[6] = {0x02, 0x00, 0xaa, 0xbb, 0xcc, 0xdd};
#define MY_IP 0x0a000001u
#define PEER_IP 0x0a000002u

static int test_build_and_parse_request(void) {
    uint8_t frame[64];
    int n = rewsr_arp_build_request(frame, sizeof frame, MY_MAC, MY_IP,
                                    PEER_IP);
    ASSERT_EQ_INT(REWSR_ARP_FRAME_LEN, n);

    /* Layer 2 destination must be broadcast. */
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ_INT(0xff, frame[i]);
    }

    struct rewsr_arp arp;
    ASSERT_EQ_INT(REWSR_PARSE_OK,
                  rewsr_arp_parse(frame, (size_t)n, &arp));
    ASSERT_EQ_INT(REWSR_ARP_OP_REQUEST, arp.oper);
    ASSERT_EQ_U64(MY_IP, arp.sender_ip);
    ASSERT_EQ_U64(PEER_IP, arp.target_ip);
    ASSERT_MEM_EQ(MY_MAC, arp.sender_mac, 6);
    /* Target MAC in a request is unknown (zero). */
    uint8_t zero[6] = {0};
    ASSERT_MEM_EQ(zero, arp.target_mac, 6);
    return 0;
}

static int test_build_and_parse_reply(void) {
    uint8_t frame[64];
    int n = rewsr_arp_build_reply(frame, sizeof frame, MY_MAC, MY_IP,
                                  PEER_MAC, PEER_IP);
    ASSERT_EQ_INT(REWSR_ARP_FRAME_LEN, n);
    /* A reply is unicast to the requester. */
    ASSERT_MEM_EQ(PEER_MAC, frame, 6);

    struct rewsr_arp arp;
    ASSERT_EQ_INT(REWSR_PARSE_OK, rewsr_arp_parse(frame, (size_t)n, &arp));
    ASSERT_EQ_INT(REWSR_ARP_OP_REPLY, arp.oper);
    ASSERT_MEM_EQ(MY_MAC, arp.sender_mac, 6);
    ASSERT_MEM_EQ(PEER_MAC, arp.target_mac, 6);
    ASSERT_EQ_U64(PEER_IP, arp.target_ip);
    return 0;
}

static int test_is_request_for(void) {
    uint8_t frame[64];
    rewsr_arp_build_request(frame, sizeof frame, PEER_MAC, PEER_IP, MY_IP);
    ASSERT_TRUE(rewsr_arp_is_request_for(frame, REWSR_ARP_FRAME_LEN, MY_IP));
    /* Not a request for some other address. */
    ASSERT_FALSE(
        rewsr_arp_is_request_for(frame, REWSR_ARP_FRAME_LEN, 0x0a0000ffu));

    /* A reply is never "a request for" anyone. */
    uint8_t reply[64];
    rewsr_arp_build_reply(reply, sizeof reply, PEER_MAC, PEER_IP, MY_MAC,
                          MY_IP);
    ASSERT_FALSE(rewsr_arp_is_request_for(reply, REWSR_ARP_FRAME_LEN, MY_IP));
    return 0;
}

static int test_parse_rejects_non_arp(void) {
    uint8_t frame[64];
    memset(frame, 0, sizeof frame);
    frame[12] = 0x08; /* IPv4 ethertype, not ARP */
    frame[13] = 0x00;
    ASSERT_TRUE(rewsr_arp_parse(frame, sizeof frame, NULL) != REWSR_PARSE_OK);
    return 0;
}

static int test_parse_rejects_short(void) {
    uint8_t frame[20];
    memset(frame, 0, sizeof frame);
    ASSERT_EQ_INT(REWSR_PARSE_SHORT, rewsr_arp_parse(frame, sizeof frame, NULL));
    return 0;
}

REWSR_TEST_MAIN("arp", {
    RUN_TEST(test_build_and_parse_request);
    RUN_TEST(test_build_and_parse_reply);
    RUN_TEST(test_is_request_for);
    RUN_TEST(test_parse_rejects_non_arp);
    RUN_TEST(test_parse_rejects_short);
})
