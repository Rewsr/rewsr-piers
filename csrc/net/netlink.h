#ifndef REWSR_NET_NETLINK_H
#define REWSR_NET_NETLINK_H

#include <stddef.h>
#include <stdint.h>

/* netlink enumerates network interfaces by talking rtnetlink directly
 * instead of shelling out to `ip link`. The facts and tier layers need an
 * interface's index, MTU, operational state, MAC, and packet counters; a
 * RTM_GETLINK dump returns all of that in one round trip. The message
 * parsing (walking the ifinfomsg and its rtattr attributes) is pure buffer
 * work and is unit tested against hand-built messages; the socket that
 * sends the dump request and reads the reply is Linux only and gated at
 * runtime, returning -ENOTSUP elsewhere.
 *
 * The rtnetlink constants and structures below mirror the Linux uapi
 * (linux/rtnetlink.h, linux/if_link.h) and are reproduced so the parser
 * builds and is tested on a non-Linux host. */

/* Netlink message header (struct nlmsghdr). */
struct rewsr_nlmsghdr {
    uint32_t nlmsg_len;
    uint16_t nlmsg_type;
    uint16_t nlmsg_flags;
    uint32_t nlmsg_seq;
    uint32_t nlmsg_pid;
};

/* Interface info message (struct ifinfomsg), the payload of RTM_NEWLINK. */
struct rewsr_ifinfomsg {
    uint8_t ifi_family;
    uint8_t ifi_pad;
    uint16_t ifi_type;
    int32_t ifi_index;
    uint32_t ifi_flags;
    uint32_t ifi_change;
};

/* Route attribute header (struct rtattr). */
struct rewsr_rtattr {
    uint16_t rta_len;
    uint16_t rta_type;
};

/* Message types and attribute ids we use (linux/rtnetlink.h,
 * linux/if_link.h). */
#define REWSR_RTM_GETLINK 18
#define REWSR_RTM_NEWLINK 16
#define REWSR_NLMSG_DONE 3
#define REWSR_NLMSG_ERROR 2

#define REWSR_IFLA_ADDRESS 1
#define REWSR_IFLA_IFNAME 3
#define REWSR_IFLA_MTU 4
#define REWSR_IFLA_OPERSTATE 16
#define REWSR_IFLA_STATS64 23

/* Operational states (linux/if.h IF_OPER_*). */
#define REWSR_IF_OPER_UP 6
#define REWSR_IF_OPER_DOWN 2

/* rewsr_link_info is the parsed view of one interface. */
struct rewsr_link_info {
    int index;
    char name[16];
    uint32_t mtu;
    uint8_t oper_state;
    uint8_t mac[6];
    int has_mac;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    int has_stats;
};

/* rewsr_nl_parse_link parses one RTM_NEWLINK message body (the ifinfomsg
 * plus its attributes) of length len into out. Returns 0 on success, -1 if
 * the buffer is too short or malformed. The stats and MAC fields are only
 * filled if their attributes are present (has_stats / has_mac record that). */
int rewsr_nl_parse_link(const uint8_t *msg, size_t len,
                        struct rewsr_link_info *out);

/* rewsr_nl_oper_state_name maps an operational state to a stable string. */
const char *rewsr_nl_oper_state_name(uint8_t state);

/* rewsr_nl_list_links performs a RTM_GETLINK dump and calls cb for each
 * interface. Linux only; returns -ENOTSUP-equivalent elsewhere, or a
 * negative errno on a socket failure. */
typedef void (*rewsr_link_cb)(const struct rewsr_link_info *link, void *user);
int rewsr_nl_list_links(rewsr_link_cb cb, void *user);

#endif /* REWSR_NET_NETLINK_H */
