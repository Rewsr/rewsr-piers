#include "ctest.h"

#include "../xdp/frame_pool.h"

static int test_init_seeds_full(void) {
    uint64_t stack[64];
    struct rewsr_frame_pool p;
    ASSERT_EQ_INT(0, rewsr_frame_pool_init(&p, stack, 64, 2048));
    ASSERT_EQ_U64(64, rewsr_frame_pool_available(&p));
    return 0;
}

static int test_init_rejects_non_pow2_chunk(void) {
    uint64_t stack[8];
    struct rewsr_frame_pool p;
    ASSERT_EQ_INT(-1, rewsr_frame_pool_init(&p, stack, 8, 3000));
    return 0;
}

static int test_alloc_order_and_addresses(void) {
    uint64_t stack[4];
    struct rewsr_frame_pool p;
    rewsr_frame_pool_init(&p, stack, 4, 4096);

    /* A fresh pool hands out frame 0 first, then 4096, then 8192, 12288. */
    uint64_t a;
    ASSERT_EQ_INT(1, rewsr_frame_pool_alloc(&p, &a));
    ASSERT_EQ_U64(0, a);
    ASSERT_EQ_INT(1, rewsr_frame_pool_alloc(&p, &a));
    ASSERT_EQ_U64(4096, a);
    ASSERT_EQ_INT(1, rewsr_frame_pool_alloc(&p, &a));
    ASSERT_EQ_U64(8192, a);
    ASSERT_EQ_INT(1, rewsr_frame_pool_alloc(&p, &a));
    ASSERT_EQ_U64(12288, a);

    /* Pool exhausted: the kernel holds every frame now. */
    ASSERT_EQ_INT(0, rewsr_frame_pool_alloc(&p, &a));
    return 0;
}

static int test_free_masks_to_chunk_base(void) {
    uint64_t stack[4];
    struct rewsr_frame_pool p;
    rewsr_frame_pool_init(&p, stack, 4, 4096);

    uint64_t a;
    rewsr_frame_pool_alloc(&p, &a); /* takes 0 */
    /* The kernel returns a descriptor addr partway into the frame (256
     * bytes of headroom, say). Freeing it must recover chunk base 0, and a
     * subsequent alloc must hand that same frame back. */
    ASSERT_EQ_INT(1, rewsr_frame_pool_free(&p, 256));
    uint64_t b;
    rewsr_frame_pool_alloc(&p, &b);
    ASSERT_EQ_U64(0, b);
    return 0;
}

static int test_free_rejects_out_of_range(void) {
    uint64_t stack[4];
    struct rewsr_frame_pool p;
    rewsr_frame_pool_init(&p, stack, 4, 4096);
    /* Drain one so there is room to free, then try to free an address past
     * the UMEM. */
    uint64_t a;
    rewsr_frame_pool_alloc(&p, &a);
    ASSERT_EQ_INT(0, rewsr_frame_pool_free(&p, 4 * 4096));
    return 0;
}

static int test_free_rejects_overfull(void) {
    uint64_t stack[2];
    struct rewsr_frame_pool p;
    rewsr_frame_pool_init(&p, stack, 2, 2048);
    /* Pool starts full; freeing another frame would overflow the stack. */
    ASSERT_EQ_INT(0, rewsr_frame_pool_free(&p, 0));
    return 0;
}

static int test_alloc_free_cycle_conserves_frames(void) {
    uint64_t stack[16];
    struct rewsr_frame_pool p;
    rewsr_frame_pool_init(&p, stack, 16, 2048);

    /* Cycle frames through alloc and free many times; the pool must never
     * lose or duplicate a frame, so availability always returns to 16. */
    for (int round = 0; round < 1000; round++) {
        uint64_t held[16];
        int n = 0;
        uint64_t a;
        while (rewsr_frame_pool_alloc(&p, &a)) {
            held[n++] = a;
        }
        ASSERT_EQ_INT(16, n);
        ASSERT_EQ_U64(0, rewsr_frame_pool_available(&p));
        for (int i = 0; i < n; i++) {
            ASSERT_EQ_INT(1, rewsr_frame_pool_free(&p, held[i]));
        }
        ASSERT_EQ_U64(16, rewsr_frame_pool_available(&p));
    }
    return 0;
}

REWSR_TEST_MAIN("frame_pool", {
    RUN_TEST(test_init_seeds_full);
    RUN_TEST(test_init_rejects_non_pow2_chunk);
    RUN_TEST(test_alloc_order_and_addresses);
    RUN_TEST(test_free_masks_to_chunk_base);
    RUN_TEST(test_free_rejects_out_of_range);
    RUN_TEST(test_free_rejects_overfull);
    RUN_TEST(test_alloc_free_cycle_conserves_frames);
})
