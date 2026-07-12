#include "stats.h"

#include <string.h>

void rewsr_dp_stats_zero(struct rewsr_dp_stats *s) { memset(s, 0, sizeof *s); }

void rewsr_dp_stats_add(struct rewsr_dp_stats *dst,
                        const struct rewsr_dp_stats *src) {
    dst->rx_packets += src->rx_packets;
    dst->rx_bytes += src->rx_bytes;
    dst->tx_packets += src->tx_packets;
    dst->tx_bytes += src->tx_bytes;
    dst->rx_drops += src->rx_drops;
    dst->tx_drops += src->tx_drops;
    dst->parse_errors += src->parse_errors;
}

void rewsr_dp_stats_delta(const struct rewsr_dp_stats *start,
                          const struct rewsr_dp_stats *end,
                          struct rewsr_dp_stats *out) {
    out->rx_packets = end->rx_packets - start->rx_packets;
    out->rx_bytes = end->rx_bytes - start->rx_bytes;
    out->tx_packets = end->tx_packets - start->tx_packets;
    out->tx_bytes = end->tx_bytes - start->tx_bytes;
    out->rx_drops = end->rx_drops - start->rx_drops;
    out->tx_drops = end->tx_drops - start->tx_drops;
    out->parse_errors = end->parse_errors - start->parse_errors;
}

void rewsr_dp_rate_compute(const struct rewsr_dp_stats *delta,
                           uint64_t elapsed_ns, struct rewsr_dp_rate *out) {
    memset(out, 0, sizeof *out);
    if (elapsed_ns == 0) {
        return;
    }
    double secs = (double)elapsed_ns / 1e9;

    out->rx_pps = (double)delta->rx_packets / secs;
    out->tx_pps = (double)delta->tx_packets / secs;
    out->rx_mbps = (double)delta->rx_bytes * 8.0 / secs / 1e6;
    out->tx_mbps = (double)delta->tx_bytes * 8.0 / secs / 1e6;

    /* Drop ratio is over the TX direction: frames dropped versus frames the
     * worker attempted to send (delivered plus dropped). A worker that sent
     * nothing has a zero ratio, not a NaN. */
    uint64_t attempted = delta->tx_packets + delta->tx_drops;
    if (attempted > 0) {
        out->drop_ratio = (double)delta->tx_drops / (double)attempted;
    }
}
