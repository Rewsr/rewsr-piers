#ifndef REWSR_DATAPLANE_STATS_H
#define REWSR_DATAPLANE_STATS_H

#include <stdint.h>

/* stats accumulates the counters a data-plane worker reports: packets and
 * bytes in each direction, drops, and the derived rates over an interval.
 * The counters are plain (each worker owns its own instance, and a
 * collector sums them), so there is no locking on the hot path. The rate
 * computation is pure arithmetic over two snapshots and a duration, which
 * is what the tests exercise; nothing here reads a clock, so the caller
 * supplies elapsed nanoseconds and the result is deterministic. */

struct rewsr_dp_stats {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_drops;   /* frames the pool could not back on refill */
    uint64_t tx_drops;   /* frames dropped because the TX ring was full */
    uint64_t parse_errors;
};

/* rewsr_dp_rate is the throughput derived from the delta of two snapshots
 * over a measured interval. */
struct rewsr_dp_rate {
    double rx_pps;
    double tx_pps;
    double rx_mbps;
    double tx_mbps;
    double drop_ratio; /* drops / (delivered + drops), 0..1 */
};

/* rewsr_dp_stats_zero clears all counters. */
void rewsr_dp_stats_zero(struct rewsr_dp_stats *s);

/* rewsr_dp_stats_add sums src into dst, for a collector combining several
 * per-worker counters into a fleet total. */
void rewsr_dp_stats_add(struct rewsr_dp_stats *dst,
                        const struct rewsr_dp_stats *src);

/* rewsr_dp_stats_delta computes end minus start into out, the per-interval
 * counters the rate is derived from. */
void rewsr_dp_stats_delta(const struct rewsr_dp_stats *start,
                          const struct rewsr_dp_stats *end,
                          struct rewsr_dp_stats *out);

/* rewsr_dp_rate_compute derives rates from a delta over elapsed_ns
 * nanoseconds. A zero or negative interval yields all-zero rates rather
 * than a division by zero. */
void rewsr_dp_rate_compute(const struct rewsr_dp_stats *delta,
                           uint64_t elapsed_ns, struct rewsr_dp_rate *out);

#endif /* REWSR_DATAPLANE_STATS_H */
