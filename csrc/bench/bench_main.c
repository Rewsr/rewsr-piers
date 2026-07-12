/* rewsr-udpbench is the standalone driver for the C loopback benchmark. It
 * is configured entirely by environment variable so it drops into a shell
 * loop or a CI step without argument parsing:
 *
 *   REWSR_BENCH_PINGS    ping-pong round trips for latency (default 200)
 *   REWSR_BENCH_BURST    datagrams for the throughput leg (default 2000)
 *   REWSR_BENCH_SIZE     payload bytes for the throughput leg (default 1024)
 *
 * It prints the same fields the Go tier0 benchmark reports, so the two can
 * be compared directly. */

#include "udp_bench.h"
#include "../util/log.h"

#include <stdlib.h>

static int env_int(const char *key, int fallback) {
    const char *v = getenv(key);
    if (v == NULL || *v == '\0') {
        return fallback;
    }
    char *end;
    long n = strtol(v, &end, 10);
    if (*end != '\0' || n <= 0 || n > 100000000) {
        return fallback;
    }
    return (int)n;
}

int main(void) {
    rewsr_log_init();

    int pings = env_int("REWSR_BENCH_PINGS", 200);
    int burst = env_int("REWSR_BENCH_BURST", 2000);
    int size = env_int("REWSR_BENCH_SIZE", 1024);

    REWSR_INFO("udp loopback bench: pings=%d burst=%d size=%d", pings, burst,
               size);

    struct rewsr_udp_bench_result r;
    int rc = rewsr_udp_bench_run(pings, burst, size, &r);
    if (rc != 0) {
        REWSR_ERROR("benchmark failed: rc=%d", rc);
        return 1;
    }
    rewsr_udp_bench_print(&r);
    return 0;
}
