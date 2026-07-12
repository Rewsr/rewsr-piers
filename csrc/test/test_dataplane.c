#include "ctest.h"

#include "../dataplane/stats.h"
#include "../dataplane/worker.h"
#include "../net/pktgen.h"

#include <errno.h>
#include <math.h>
#include <string.h>

static int test_stats_add_and_delta(void) {
    struct rewsr_dp_stats a, b, out;
    rewsr_dp_stats_zero(&a);
    rewsr_dp_stats_zero(&b);
    a.rx_packets = 100;
    a.tx_packets = 90;
    b.rx_packets = 10;
    b.tx_packets = 5;

    rewsr_dp_stats_add(&a, &b);
    ASSERT_EQ_U64(110, a.rx_packets);
    ASSERT_EQ_U64(95, a.tx_packets);

    struct rewsr_dp_stats start, end;
    rewsr_dp_stats_zero(&start);
    rewsr_dp_stats_zero(&end);
    start.rx_bytes = 1000;
    end.rx_bytes = 5000;
    rewsr_dp_stats_delta(&start, &end, &out);
    ASSERT_EQ_U64(4000, out.rx_bytes);
    return 0;
}

static int test_rate_compute(void) {
    struct rewsr_dp_stats delta;
    rewsr_dp_stats_zero(&delta);
    delta.rx_packets = 1000000;
    delta.rx_bytes = 64000000;
    delta.tx_packets = 500000;
    delta.tx_bytes = 32000000;
    delta.tx_drops = 500000;

    struct rewsr_dp_rate rate;
    /* Exactly one second. */
    rewsr_dp_rate_compute(&delta, 1000000000ull, &rate);
    ASSERT_TRUE(fabs(rate.rx_pps - 1000000.0) < 1e-6);
    ASSERT_TRUE(fabs(rate.tx_pps - 500000.0) < 1e-6);
    /* 64 MB/s = 512 Mbps. */
    ASSERT_TRUE(fabs(rate.rx_mbps - 512.0) < 1e-6);
    /* Half of the attempted TX was dropped. */
    ASSERT_TRUE(fabs(rate.drop_ratio - 0.5) < 1e-9);
    return 0;
}

static int test_rate_zero_interval_no_nan(void) {
    struct rewsr_dp_stats delta;
    rewsr_dp_stats_zero(&delta);
    delta.rx_packets = 100;
    struct rewsr_dp_rate rate;
    rewsr_dp_rate_compute(&delta, 0, &rate);
    ASSERT_TRUE(rate.rx_pps == 0.0);
    ASSERT_TRUE(rate.drop_ratio == 0.0);
    return 0;
}

static const uint8_t SM[6] = {0x02, 0, 0, 0, 0, 1};
static const uint8_t DM[6] = {0x02, 0, 0, 0, 0, 2};

static int test_classify_accepts_generated_frame(void) {
    struct rewsr_pktgen g;
    rewsr_pktgen_init(&g, SM, DM, 0x0a000001, 0x0a000002, 1000, 2000, 128);

    uint8_t frame[2048];
    int n = rewsr_pktgen_next(&g, frame, sizeof frame);
    ASSERT_TRUE(n > 0);

    uint32_t payload = 0;
    ASSERT_EQ_INT(1, rewsr_dp_classify(frame, (uint32_t)n, &payload));
    ASSERT_EQ_U64(128, payload);
    return 0;
}

static int test_classify_rejects_corrupt_and_foreign(void) {
    struct rewsr_pktgen g;
    rewsr_pktgen_init(&g, SM, DM, 0x0a000001, 0x0a000002, 1000, 2000, 128);
    uint8_t frame[2048];
    int n = rewsr_pktgen_next(&g, frame, sizeof frame);

    /* Corrupt a payload byte: the UDP checksum no longer verifies, so the
     * classifier must reject it. */
    frame[n - 1] ^= 0xff;
    ASSERT_EQ_INT(0, rewsr_dp_classify(frame, (uint32_t)n, NULL));

    /* A non-IPv4 frame is not ours. */
    uint8_t arp[64] = {0};
    arp[12] = 0x08;
    arp[13] = 0x06;
    ASSERT_EQ_INT(0, rewsr_dp_classify(arp, sizeof arp, NULL));
    return 0;
}

static int test_worker_run_unsupported_off_linux(void) {
    /* On this macOS dev host the AF_XDP run loop must report unsupported
     * rather than pretend to run. On Linux this test is skipped by the
     * platform guard below. */
#if !defined(__linux__)
    struct rewsr_dp_worker w;
    memset(&w, 0, sizeof w);
    ASSERT_EQ_INT(-ENOTSUP, rewsr_dp_worker_run(&w));
#endif
    return 0;
}

REWSR_TEST_MAIN("dataplane", {
    RUN_TEST(test_stats_add_and_delta);
    RUN_TEST(test_rate_compute);
    RUN_TEST(test_rate_zero_interval_no_nan);
    RUN_TEST(test_classify_accepts_generated_frame);
    RUN_TEST(test_classify_rejects_corrupt_and_foreign);
    RUN_TEST(test_worker_run_unsupported_off_linux);
})
