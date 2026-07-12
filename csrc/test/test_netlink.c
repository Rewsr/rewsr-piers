#include "ctest.h"

#include "../net/netlink.h"

#include <string.h>

/* build_rtattr appends one aligned rtattr (header + payload) to buf at
 * *off and advances *off, mirroring how the kernel lays out a link
 * message. Returns the new offset. */
static size_t build_rtattr(uint8_t *buf, size_t off, uint16_t type,
                           const void *payload, uint16_t plen) {
    struct rewsr_rtattr rta;
    rta.rta_len = (uint16_t)(sizeof(struct rewsr_rtattr) + plen);
    rta.rta_type = type;
    memcpy(buf + off, &rta, sizeof rta);
    memcpy(buf + off + sizeof rta, payload, plen);
    size_t total = sizeof rta + plen;
    /* Pad to the 4-byte alignment the parser steps by. */
    size_t aligned = (total + 3) & ~((size_t)3);
    for (size_t i = total; i < aligned; i++) {
        buf[off + i] = 0;
    }
    return off + aligned;
}

static int test_parse_full_link(void) {
    uint8_t buf[512];
    memset(buf, 0, sizeof buf);

    struct rewsr_ifinfomsg ifi;
    memset(&ifi, 0, sizeof ifi);
    ifi.ifi_family = 0;
    ifi.ifi_type = 1;
    ifi.ifi_index = 7;
    memcpy(buf, &ifi, sizeof ifi);
    size_t off = sizeof ifi;

    const char *name = "ens1f0np0";
    off = build_rtattr(buf, off, REWSR_IFLA_IFNAME, name,
                       (uint16_t)(strlen(name) + 1));

    uint32_t mtu = 9000;
    off = build_rtattr(buf, off, REWSR_IFLA_MTU, &mtu, sizeof mtu);

    uint8_t oper = REWSR_IF_OPER_UP;
    off = build_rtattr(buf, off, REWSR_IFLA_OPERSTATE, &oper, 1);

    uint8_t mac[6] = {0xb8, 0x59, 0x9f, 0xca, 0xe2, 0xf8};
    off = build_rtattr(buf, off, REWSR_IFLA_ADDRESS, mac, 6);

    /* stats64: rx_packets, tx_packets, rx_bytes, tx_bytes then padding to
     * the real struct's size, which the parser tolerates via plen check. */
    uint64_t stats[8] = {1000, 2000, 3000000, 4000000, 0, 0, 0, 0};
    off = build_rtattr(buf, off, REWSR_IFLA_STATS64, stats, sizeof stats);

    struct rewsr_link_info info;
    ASSERT_EQ_INT(0, rewsr_nl_parse_link(buf, off, &info));
    ASSERT_EQ_INT(7, info.index);
    ASSERT_EQ_STR("ens1f0np0", info.name);
    ASSERT_EQ_U64(9000, info.mtu);
    ASSERT_EQ_INT(REWSR_IF_OPER_UP, info.oper_state);
    ASSERT_EQ_STR("up", rewsr_nl_oper_state_name(info.oper_state));
    ASSERT_TRUE(info.has_mac);
    ASSERT_MEM_EQ(mac, info.mac, 6);
    ASSERT_TRUE(info.has_stats);
    ASSERT_EQ_U64(1000, info.rx_packets);
    ASSERT_EQ_U64(2000, info.tx_packets);
    ASSERT_EQ_U64(3000000, info.rx_bytes);
    ASSERT_EQ_U64(4000000, info.tx_bytes);
    return 0;
}

static int test_parse_missing_optional_attrs(void) {
    /* A link with only a name and index: MAC and stats absent, and the
     * has_ flags must reflect that rather than reporting zeros as real. */
    uint8_t buf[128];
    memset(buf, 0, sizeof buf);
    struct rewsr_ifinfomsg ifi;
    memset(&ifi, 0, sizeof ifi);
    ifi.ifi_index = 1;
    memcpy(buf, &ifi, sizeof ifi);
    size_t off = build_rtattr(buf, sizeof ifi, REWSR_IFLA_IFNAME, "lo", 3);

    struct rewsr_link_info info;
    ASSERT_EQ_INT(0, rewsr_nl_parse_link(buf, off, &info));
    ASSERT_EQ_STR("lo", info.name);
    ASSERT_FALSE(info.has_mac);
    ASSERT_FALSE(info.has_stats);
    return 0;
}

static int test_parse_rejects_short_message(void) {
    uint8_t buf[4] = {0};
    struct rewsr_link_info info;
    ASSERT_EQ_INT(-1, rewsr_nl_parse_link(buf, sizeof buf, &info));
    return 0;
}

static int test_parse_tolerates_truncated_attr(void) {
    /* An attribute claiming more length than the buffer holds must stop
     * the walk cleanly, not read past the end. */
    uint8_t buf[64];
    memset(buf, 0, sizeof buf);
    struct rewsr_ifinfomsg ifi;
    memset(&ifi, 0, sizeof ifi);
    ifi.ifi_index = 3;
    memcpy(buf, &ifi, sizeof ifi);

    struct rewsr_rtattr rta;
    rta.rta_len = 200; /* lies: far past the buffer */
    rta.rta_type = REWSR_IFLA_IFNAME;
    memcpy(buf + sizeof ifi, &rta, sizeof rta);

    struct rewsr_link_info info;
    /* Parse must succeed (index read) and simply not populate the bogus
     * attribute. */
    ASSERT_EQ_INT(0, rewsr_nl_parse_link(buf, sizeof ifi + sizeof rta, &info));
    ASSERT_EQ_INT(3, info.index);
    ASSERT_EQ_STR("", info.name);
    return 0;
}

static int test_oper_state_names(void) {
    ASSERT_EQ_STR("up", rewsr_nl_oper_state_name(REWSR_IF_OPER_UP));
    ASSERT_EQ_STR("down", rewsr_nl_oper_state_name(REWSR_IF_OPER_DOWN));
    ASSERT_EQ_STR("unknown", rewsr_nl_oper_state_name(0));
    return 0;
}

REWSR_TEST_MAIN("netlink", {
    RUN_TEST(test_parse_full_link);
    RUN_TEST(test_parse_missing_optional_attrs);
    RUN_TEST(test_parse_rejects_short_message);
    RUN_TEST(test_parse_tolerates_truncated_attr);
    RUN_TEST(test_oper_state_names);
})
