#include "ethtool_ioctl.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

const char *rewsr_ethtool_speed_name(uint32_t speed_mbps) {
    /* A small static buffer holds the rendered "<n>M" form for speeds that
     * are not a round GbE value. It is not reentrant, which is fine for the
     * single-threaded facts collection path this serves. */
    static char buf[16];
    switch (speed_mbps) {
    case REWSR_SPEED_UNKNOWN:
        return "unknown";
    case 1000:
        return "1G";
    case 2500:
        return "2.5G";
    case 5000:
        return "5G";
    case 10000:
        return "10G";
    case 25000:
        return "25G";
    case 40000:
        return "40G";
    case 50000:
        return "50G";
    case 100000:
        return "100G";
    case 200000:
        return "200G";
    case 400000:
        return "400G";
    default:
        break;
    }
    if (speed_mbps >= 1000 && speed_mbps % 1000 == 0) {
        snprintf(buf, sizeof buf, "%uG", speed_mbps / 1000);
    } else {
        snprintf(buf, sizeof buf, "%uM", speed_mbps);
    }
    return buf;
}

const char *rewsr_ethtool_duplex_name(uint8_t duplex) {
    switch (duplex) {
    case REWSR_DUPLEX_HALF:
        return "half";
    case REWSR_DUPLEX_FULL:
        return "full";
    default:
        return "unknown";
    }
}

#if defined(__linux__)

#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* eth_ioctl opens a scratch socket, fills an ifreq for ifname pointing at
 * the caller's ethtool command struct, and issues SIOCETHTOOL. It is the
 * one place the socket lifecycle lives so each getter stays short. */
static int eth_ioctl(const char *ifname, void *cmd) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return errno != 0 ? -errno : -1;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_data = (void *)cmd;
    int rc = ioctl(fd, SIOCETHTOOL, &ifr);
    if (rc != 0) {
        rc = errno != 0 ? -errno : -1;
    }
    close(fd);
    return rc;
}

int rewsr_ethtool_get_drvinfo(const char *ifname,
                              struct rewsr_ethtool_drvinfo *out) {
    struct ethtool_drvinfo info;
    memset(&info, 0, sizeof info);
    info.cmd = ETHTOOL_GDRVINFO;
    int rc = eth_ioctl(ifname, &info);
    if (rc != 0) {
        return rc;
    }
    memset(out, 0, sizeof *out);
    strncpy(out->driver, info.driver, sizeof out->driver - 1);
    strncpy(out->version, info.version, sizeof out->version - 1);
    strncpy(out->fw_version, info.fw_version, sizeof out->fw_version - 1);
    strncpy(out->bus_info, info.bus_info, sizeof out->bus_info - 1);
    return 0;
}

int rewsr_ethtool_get_link(const char *ifname,
                           struct rewsr_ethtool_link *out) {
    /* ETHTOOL_GLINKSETTINGS uses a two-call handshake: the first call with
     * link_mode_masks_nwords == 0 makes the kernel report the negative of
     * the word count it wants, and the second call supplies it. The speed
     * and duplex fields are valid after the first successful call, which is
     * all we read here. */
    struct ethtool_link_settings ls;
    memset(&ls, 0, sizeof ls);
    ls.cmd = ETHTOOL_GLINKSETTINGS;
    int rc = eth_ioctl(ifname, &ls);
    if (rc != 0) {
        return rc;
    }
    memset(out, 0, sizeof *out);
    out->speed_mbps = ls.speed == 0 ? REWSR_SPEED_UNKNOWN : (uint32_t)ls.speed;
    out->duplex = ls.duplex;
    out->autoneg = ls.autoneg;
    return 0;
}

int rewsr_ethtool_get_channels(const char *ifname,
                               struct rewsr_ethtool_channels *out) {
    struct ethtool_channels ch;
    memset(&ch, 0, sizeof ch);
    ch.cmd = ETHTOOL_GCHANNELS;
    int rc = eth_ioctl(ifname, &ch);
    if (rc != 0) {
        return rc;
    }
    memset(out, 0, sizeof *out);
    out->max_combined = ch.max_combined;
    out->combined = ch.combined_count;
    out->max_rx = ch.max_rx;
    out->max_tx = ch.max_tx;
    out->rx = ch.rx_count;
    out->tx = ch.tx_count;
    return 0;
}

#else /* !__linux__ */

int rewsr_ethtool_get_drvinfo(const char *ifname,
                              struct rewsr_ethtool_drvinfo *out) {
    (void)ifname;
    (void)out;
    return -ENOTSUP;
}

int rewsr_ethtool_get_link(const char *ifname,
                           struct rewsr_ethtool_link *out) {
    (void)ifname;
    (void)out;
    return -ENOTSUP;
}

int rewsr_ethtool_get_channels(const char *ifname,
                               struct rewsr_ethtool_channels *out) {
    (void)ifname;
    (void)out;
    return -ENOTSUP;
}

#endif /* __linux__ */
