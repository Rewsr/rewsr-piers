#include "ctest.h"

#include "../ring/spsc_ring.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static int test_capacity_is_power_of_two_minus_one(void) {
    uint8_t storage[8 * 4];
    struct rewsr_spsc_ring r;
    /* Capacity 5 rounds up to 8 slots, so 7 live entries. */
    ASSERT_EQ_INT(0, rewsr_spsc_init(&r, storage, 5, 4));
    ASSERT_EQ_U64(7, rewsr_spsc_capacity(&r));
    return 0;
}

static int test_produce_consume_fifo(void) {
    uint8_t storage[8 * sizeof(uint32_t)];
    struct rewsr_spsc_ring r;
    rewsr_spsc_init(&r, storage, 8, sizeof(uint32_t));

    for (uint32_t i = 0; i < 7; i++) {
        ASSERT_EQ_INT(1, rewsr_spsc_produce(&r, &i));
    }
    /* Ring is now full (7 live in 8 slots); the next produce fails. */
    uint32_t x = 99;
    ASSERT_EQ_INT(0, rewsr_spsc_produce(&r, &x));
    ASSERT_EQ_U64(7, rewsr_spsc_count(&r));

    for (uint32_t i = 0; i < 7; i++) {
        uint32_t got = 0xffffffff;
        ASSERT_EQ_INT(1, rewsr_spsc_consume(&r, &got));
        ASSERT_EQ_U64(i, got);
    }
    /* Empty now. */
    uint32_t got;
    ASSERT_EQ_INT(0, rewsr_spsc_consume(&r, &got));
    return 0;
}

static int test_wraparound(void) {
    uint8_t storage[4 * sizeof(uint32_t)];
    struct rewsr_spsc_ring r;
    rewsr_spsc_init(&r, storage, 4, sizeof(uint32_t));

    /* Push/pop many more than the ring size to force the indices to wrap
     * past their range several times. */
    uint32_t next_push = 0, next_pop = 0;
    for (int round = 0; round < 100; round++) {
        while (rewsr_spsc_produce(&r, &next_push)) {
            next_push++;
        }
        uint32_t got;
        while (rewsr_spsc_consume(&r, &got)) {
            ASSERT_EQ_U64(next_pop, got);
            next_pop++;
        }
    }
    ASSERT_EQ_U64(next_push, next_pop);
    ASSERT_TRUE(next_push > 100);
    return 0;
}

struct thread_arg {
    struct rewsr_spsc_ring *r;
    uint32_t count;
};

static void *producer_thread(void *p) {
    struct thread_arg *a = (struct thread_arg *)p;
    for (uint32_t i = 0; i < a->count;) {
        if (rewsr_spsc_produce(a->r, &i)) {
            i++;
        }
    }
    return NULL;
}

static int test_concurrent_spsc(void) {
    /* One producer thread, one consumer (this thread): every value sent
     * must arrive exactly once and strictly in order, which is the SPSC
     * contract. Run enough items to exercise real contention. */
    const uint32_t N = 500000;
    uint8_t *storage = malloc((size_t)1024 * sizeof(uint32_t));
    ASSERT_TRUE(storage != NULL);
    struct rewsr_spsc_ring r;
    rewsr_spsc_init(&r, storage, 1024, sizeof(uint32_t));

    struct thread_arg arg = {&r, N};
    pthread_t producer;
    ASSERT_EQ_INT(0, pthread_create(&producer, NULL, producer_thread, &arg));

    uint32_t expect = 0;
    while (expect < N) {
        uint32_t got;
        if (rewsr_spsc_consume(&r, &got)) {
            if (got != expect) {
                printf("    order violation: want %u got %u\n", expect, got);
                rewsr_ctest_fail_count++;
                break;
            }
            expect++;
        }
    }
    pthread_join(producer, NULL);
    ASSERT_EQ_U64(N, expect);
    free(storage);
    return 0;
}

REWSR_TEST_MAIN("spsc_ring", {
    RUN_TEST(test_capacity_is_power_of_two_minus_one);
    RUN_TEST(test_produce_consume_fifo);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_concurrent_spsc);
})
