#include "netlink.h"

#include <errno.h>
#include <string.h>

/* RTA_ALIGN rounds an attribute length up to the 4-byte boundary rtnetlink
 * pads every attribute to. The parser must step by the aligned length or it
 * loses sync with the stream. */
#define REWSR_RTA_ALIGNTO 4
#define REWSR_RTA_ALIGN(len) \
    (((len) + REWSR_RTA_ALIGNTO - 1) & ~(REWSR_RTA_ALIGNTO - 1))

/* rta_stats64 mirrors the fields of struct rtnl_link_stats64 that we read.
 * The kernel struct is larger; we only pull the four counters that matter
 * and rely on the attribute length to bound the read. */
struct rewsr_rtnl_stats64 {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    /* trailing counters omitted deliberately */
};

const char *rewsr_nl_oper_state_name(uint8_t state) {
    switch (state) {
    case 0:
        return "unknown";
    case 1:
        return "notpresent";
    case REWSR_IF_OPER_DOWN:
        return "down";
    case 3:
        return "lowerlayerdown";
    case 4:
        return "testing";
    case 5:
        return "dormant";
    case REWSR_IF_OPER_UP:
        return "up";
    default:
        return "invalid";
    }
}

int rewsr_nl_parse_link(const uint8_t *msg, size_t len,
                        struct rewsr_link_info *out) {
    if (len < sizeof(struct rewsr_ifinfomsg)) {
        return -1;
    }
    memset(out, 0, sizeof *out);

    const struct rewsr_ifinfomsg *ifi = (const struct rewsr_ifinfomsg *)msg;
    out->index = ifi->ifi_index;

    /* Attributes follow the ifinfomsg, each aligned to 4 bytes. Walk them
     * with bounds checks so a truncated or hostile message can never read
     * past the buffer. */
    size_t off = sizeof(struct rewsr_ifinfomsg);
    while (off + sizeof(struct rewsr_rtattr) <= len) {
        const struct rewsr_rtattr *rta =
            (const struct rewsr_rtattr *)(msg + off);
        if (rta->rta_len < sizeof(struct rewsr_rtattr) ||
            off + rta->rta_len > len) {
            break;
        }
        const uint8_t *payload = msg + off + sizeof(struct rewsr_rtattr);
        size_t plen = rta->rta_len - sizeof(struct rewsr_rtattr);

        switch (rta->rta_type) {
        case REWSR_IFLA_IFNAME: {
            size_t n = plen;
            if (n >= sizeof out->name) {
                n = sizeof out->name - 1;
            }
            memcpy(out->name, payload, n);
            out->name[n] = '\0';
            break;
        }
        case REWSR_IFLA_MTU:
            if (plen >= sizeof(uint32_t)) {
                memcpy(&out->mtu, payload, sizeof(uint32_t));
            }
            break;
        case REWSR_IFLA_OPERSTATE:
            if (plen >= 1) {
                out->oper_state = payload[0];
            }
            break;
        case REWSR_IFLA_ADDRESS:
            if (plen >= 6) {
                memcpy(out->mac, payload, 6);
                out->has_mac = 1;
            }
            break;
        case REWSR_IFLA_STATS64:
            if (plen >= sizeof(struct rewsr_rtnl_stats64)) {
                struct rewsr_rtnl_stats64 st;
                memcpy(&st, payload, sizeof st);
                out->rx_packets = st.rx_packets;
                out->tx_packets = st.tx_packets;
                out->rx_bytes = st.rx_bytes;
                out->tx_bytes = st.tx_bytes;
                out->has_stats = 1;
            }
            break;
        default:
            break;
        }

        off += REWSR_RTA_ALIGN(rta->rta_len);
    }
    return 0;
}

#if defined(__linux__)

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

int rewsr_nl_list_links(rewsr_link_cb cb, void *user) {
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        return errno != 0 ? -errno : -1;
    }

    /* A RTM_GETLINK with NLM_F_DUMP asks the kernel to return every
     * interface. The request is one nlmsghdr followed by an ifinfomsg. */
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
    } req;
    memset(&req, 0, sizeof req);
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.nh.nlmsg_type = RTM_GETLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nh.nlmsg_seq = 1;
    req.ifi.ifi_family = AF_UNSPEC;

    if (send(fd, &req, req.nh.nlmsg_len, 0) < 0) {
        int rc = errno != 0 ? -errno : -1;
        close(fd);
        return rc;
    }

    uint8_t buf[16384];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0) {
            int rc = errno != 0 ? -errno : -1;
            close(fd);
            return rc;
        }
        int done = 0;
        for (struct nlmsghdr *nh = (struct nlmsghdr *)buf;
             NLMSG_OK(nh, (unsigned)n); nh = NLMSG_NEXT(nh, n)) {
            if (nh->nlmsg_type == NLMSG_DONE) {
                done = 1;
                break;
            }
            if (nh->nlmsg_type == NLMSG_ERROR) {
                close(fd);
                return -EIO;
            }
            if (nh->nlmsg_type == RTM_NEWLINK) {
                struct rewsr_link_info info;
                size_t body_len = nh->nlmsg_len - NLMSG_HDRLEN;
                if (rewsr_nl_parse_link((const uint8_t *)NLMSG_DATA(nh),
                                        body_len, &info) == 0 &&
                    cb != NULL) {
                    cb(&info, user);
                }
            }
        }
        if (done) {
            break;
        }
    }
    close(fd);
    return 0;
}

#else /* !__linux__ */

int rewsr_nl_list_links(rewsr_link_cb cb, void *user) {
    (void)cb;
    (void)user;
    /* rtnetlink is a Linux facility; report unsupported so the caller falls
     * back rather than treating this as a hard failure. */
    return -ENOTSUP;
}

#endif /* __linux__ */
