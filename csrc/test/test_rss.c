#include "ctest.h"

#include "../net/rss.h"

/* The canonical RSS verification vectors from the Microsoft RSS
 * specification, the same ones every driver is validated against:
 *
 *   src 66.9.149.187:2794  dst 161.142.100.80:1766
 *     two-tuple (IPs only) hash  = 0x323e8fc2
 *     four-tuple (IPs+ports)hash = 0x51ccc178
 *
 *   src 199.92.111.2:14230 dst 65.69.140.83:4739
 *     four-tuple hash            = 0xc626b0ea
 */

#define IP(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (d))

static int test_two_tuple_vector(void) {
    uint32_t h = rewsr_rss_hash_ipv4_2tuple(
        rewsr_rss_default_key, REWSR_RSS_KEY_LEN, IP(66, 9, 149, 187),
        IP(161, 142, 100, 80));
    ASSERT_EQ_U64(0x323e8fc2u, h);
    return 0;
}

static int test_four_tuple_vectors(void) {
    uint32_t h1 = rewsr_rss_hash_ipv4(rewsr_rss_default_key, REWSR_RSS_KEY_LEN,
                                      IP(66, 9, 149, 187),
                                      IP(161, 142, 100, 80), 2794, 1766);
    ASSERT_EQ_U64(0x51ccc178u, h1);

    uint32_t h2 = rewsr_rss_hash_ipv4(rewsr_rss_default_key, REWSR_RSS_KEY_LEN,
                                      IP(199, 92, 111, 2), IP(65, 69, 140, 83),
                                      14230, 4739);
    ASSERT_EQ_U64(0xc626b0eau, h2);
    return 0;
}

static int test_queue_mapping(void) {
    /* The low bits of the hash select the queue; a 16-entry table masks to
     * the low 4 bits. */
    ASSERT_EQ_U64(0x323e8fc2u & 0xf, rewsr_rss_queue(0x323e8fc2u, 16));
    ASSERT_EQ_U64(0, rewsr_rss_queue(0x12340000u, 16));
    /* A zero table size is treated as a single queue. */
    ASSERT_EQ_U64(0, rewsr_rss_queue(0xdeadbeef, 0));
    return 0;
}

static int test_symmetric_key_not_required_for_determinism(void) {
    /* The same flow always hashes to the same value, the property queue
     * pinning relies on. */
    uint32_t a = rewsr_rss_hash_ipv4(rewsr_rss_default_key, REWSR_RSS_KEY_LEN,
                                     IP(10, 0, 0, 1), IP(10, 0, 0, 2), 1000,
                                     2000);
    uint32_t b = rewsr_rss_hash_ipv4(rewsr_rss_default_key, REWSR_RSS_KEY_LEN,
                                     IP(10, 0, 0, 1), IP(10, 0, 0, 2), 1000,
                                     2000);
    ASSERT_EQ_U64(a, b);
    return 0;
}

REWSR_TEST_MAIN("rss", {
    RUN_TEST(test_two_tuple_vector);
    RUN_TEST(test_four_tuple_vectors);
    RUN_TEST(test_queue_mapping);
    RUN_TEST(test_symmetric_key_not_required_for_determinism);
})
