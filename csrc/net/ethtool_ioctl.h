#ifndef REWSR_NET_ETHTOOL_H
#define REWSR_NET_ETHTOOL_H

#include <stdint.h>

/* ethtool_ioctl reads NIC driver and link facts through the SIOCETHTOOL
 * ioctl instead of shelling out to the ethtool binary. The facts layer
 * wants the driver name and firmware version (ETHTOOL_GDRVINFO), the
 * negotiated speed and duplex (ETHTOOL_GLINKSETTINGS), and the queue count
 * (ETHTOOL_GCHANNELS). The value decoders (speed number to name, duplex
 * code to name, link-mode bit meanings) are pure and unit tested; the
 * ioctl that fills the structs is Linux only and gated at runtime. */

/* Duplex codes (linux/ethtool.h DUPLEX_*). */
#define REWSR_DUPLEX_HALF 0x00
#define REWSR_DUPLEX_FULL 0x01
#define REWSR_DUPLEX_UNKNOWN 0xff

/* Speed sentinel for "unknown/not connected" (SPEED_UNKNOWN is -1 as a
 * u32). */
#define REWSR_SPEED_UNKNOWN 0xffffffffu

/* rewsr_ethtool_drvinfo is the subset of struct ethtool_drvinfo the facts
 * layer carries. Strings are NUL terminated. */
struct rewsr_ethtool_drvinfo {
    char driver[32];
    char version[32];
    char fw_version[32];
    char bus_info[32];
};

/* rewsr_ethtool_link is the decoded link state. */
struct rewsr_ethtool_link {
    uint32_t speed_mbps; /* REWSR_SPEED_UNKNOWN if not linked */
    uint8_t duplex;
    uint8_t autoneg;
};

/* rewsr_ethtool_channels is the queue configuration. */
struct rewsr_ethtool_channels {
    uint32_t max_combined;
    uint32_t combined;
    uint32_t max_rx;
    uint32_t max_tx;
    uint32_t rx;
    uint32_t tx;
};

/* rewsr_ethtool_speed_name maps a speed in Mbps to a human string such as
 * "25G" or "100G", or "unknown" for the sentinel. Round numbers get the
 * short GbE form; anything else is rendered as "<n>M". */
const char *rewsr_ethtool_speed_name(uint32_t speed_mbps);

/* rewsr_ethtool_duplex_name maps a duplex code to "half", "full", or
 * "unknown". */
const char *rewsr_ethtool_duplex_name(uint8_t duplex);

/* rewsr_ethtool_get_drvinfo, _get_link, and _get_channels fill their out
 * struct for ifname via SIOCETHTOOL. Linux only; return -ENOTSUP-equivalent
 * elsewhere, or a negative errno on failure. */
int rewsr_ethtool_get_drvinfo(const char *ifname,
                              struct rewsr_ethtool_drvinfo *out);
int rewsr_ethtool_get_link(const char *ifname,
                           struct rewsr_ethtool_link *out);
int rewsr_ethtool_get_channels(const char *ifname,
                               struct rewsr_ethtool_channels *out);

#endif /* REWSR_NET_ETHTOOL_H */
