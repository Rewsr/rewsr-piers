#ifndef REWSR_BENCH_UDP_H
#define REWSR_BENCH_UDP_H

#include <stddef.h>
#include <stdint.h>

/* udp_bench is the C counterpart to the Go tier0 loopback benchmark. It
 * measures round-trip latency with a ping-pong exchange and one-way
 * throughput with a back-to-back burst, over ordinary POSIX UDP sockets on
 * the loopback interface, so it runs on any host without privileges and
 * gives a like-for-like number to compare against the pure-Go tier0 path.
 * Nothing here is Linux specific; it is the portable floor the AF_XDP tier
 * is measured against. */

struct rewsr_udp_bench_result {
    int packet_size;
    long packets_sent;
    long packets_recv;
    double avg_latency_us;
    double min_latency_us;
    double max_latency_us;
    double p99_latency_us;
    double throughput_mbps;
    double duration_ms;
};

/* rewsr_udp_bench_latency runs ping_count round trips against a loopback
 * echo server it starts internally, filling the latency fields of out. The
 * min/avg/max/p99 are computed from the collected samples. Returns 0 on
 * success, negative on a socket setup failure. */
int rewsr_udp_bench_latency(int ping_count,
                            struct rewsr_udp_bench_result *out);

/* rewsr_udp_bench_throughput sends burst_packets datagrams of packet_size
 * bytes to a loopback sink as fast as the socket accepts them, filling the
 * throughput, packets_sent, packets_recv, and duration fields of out.
 * Returns 0 on success, negative on failure. */
int rewsr_udp_bench_throughput(int burst_packets, int packet_size,
                               struct rewsr_udp_bench_result *out);

/* rewsr_udp_bench_run combines a latency and a throughput leg into one
 * result. Zero or negative arguments fall back to defaults (200 pings,
 * 2000 packets, 1024 bytes), matching the Go benchmark's defaults so the
 * numbers line up. */
int rewsr_udp_bench_run(int ping_count, int burst_packets, int packet_size,
                        struct rewsr_udp_bench_result *out);

/* rewsr_udp_bench_print writes a human-readable summary of a result to
 * stdout. */
void rewsr_udp_bench_print(const struct rewsr_udp_bench_result *r);

#endif /* REWSR_BENCH_UDP_H */
