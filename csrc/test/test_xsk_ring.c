#include "ctest.h"

#include "../ring/xsk_ring.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* An AF_XDP ring is shared between two sides that each keep their own
 * cached view of the far counter. The tests model that with two
 * rewsr_xsk_ring structs over one set of counters and one descriptor
 * array: prod drives reserve/submit, cons drives peek/release. The
 * descriptor here is a single uint64_t, standing in for a UMEM frame
 * address as the fill and completion rings carry. */

struct shared_ring {
    uint32_t producer;
    uint32_t consumer;
    uint64_t descs[16];
    struct rewsr_xsk_ring prod;
    struct rewsr_xsk_ring cons;
};

static void shared_ring_init(struct shared_ring *s, uint32_t size) {
    s->producer = 0;
    s->consumer = 0;
    memset(s->descs, 0, sizeof s->descs);
    rewsr_xsk_ring_init(&s->prod, &s->producer, &s->consumer,
                        (uint8_t *)s->descs, size, sizeof(uint64_t));
    rewsr_xsk_ring_init(&s->cons, &s->producer, &s->consumer,
                        (uint8_t *)s->descs, size, sizeof(uint64_t));
}

static int test_reserve_is_all_or_nothing(void) {
    struct shared_ring s;
    shared_ring_init(&s, 8);

    uint32_t idx;
    /* Reserve 8 into an 8-slot ring: AF_XDP rings can fill entirely (no
     * reserved empty slot, unlike the SPSC ring). */
    ASSERT_EQ_U64(8, rewsr_xsk_prod_reserve(&s.prod, 8, &idx));
    ASSERT_EQ_U64(0, rewsr_xsk_prod_reserve(&s.prod, 1, &idx));
    rewsr_xsk_prod_submit(&s.prod, 8);

    /* Nothing consumed yet, so the producer sees zero free. */
    ASSERT_EQ_U64(0, rewsr_xsk_prod_free(&s.prod));
    return 0;
}

static int test_fifo_roundtrip(void) {
    struct shared_ring s;
    shared_ring_init(&s, 8);

    uint64_t next_tx = 0, next_rx = 0;
    for (int round = 0; round < 50; round++) {
        /* Produce as many as free. */
        uint32_t idx;
        uint32_t nb = rewsr_xsk_prod_reserve(&s.prod, 5, &idx);
        for (uint32_t i = 0; i < nb; i++) {
            uint64_t *slot = rewsr_xsk_ring_desc(&s.prod, idx + i);
            *slot = next_tx++;
        }
        rewsr_xsk_prod_submit(&s.prod, nb);

        /* Consume as many as available, checking strict FIFO. */
        uint32_t cidx;
        uint32_t got = rewsr_xsk_cons_peek(&s.cons, 5, &cidx);
        for (uint32_t i = 0; i < got; i++) {
            uint64_t *slot = rewsr_xsk_ring_desc(&s.cons, cidx + i);
            ASSERT_EQ_U64(next_rx, *slot);
            next_rx++;
        }
        rewsr_xsk_cons_release(&s.cons, got);
    }
    ASSERT_EQ_U64(next_tx, next_rx);
    ASSERT_TRUE(next_tx > 50);
    return 0;
}

static int test_counters_wrap_past_2e32_region(void) {
    /* Prime both counters near the 32-bit wrap so the modulo arithmetic in
     * reserve/peek is exercised across the boundary. The ring stays
     * correct because only the difference of counters matters. */
    struct shared_ring s;
    shared_ring_init(&s, 4);
    s.producer = 0xfffffffeu;
    s.consumer = 0xfffffffeu;
    rewsr_xsk_ring_init(&s.prod, &s.producer, &s.consumer,
                        (uint8_t *)s.descs, 4, sizeof(uint64_t));
    rewsr_xsk_ring_init(&s.cons, &s.producer, &s.consumer,
                        (uint8_t *)s.descs, 4, sizeof(uint64_t));

    uint64_t next_tx = 1000, next_rx = 1000;
    for (int round = 0; round < 20; round++) {
        uint32_t idx;
        uint32_t nb = rewsr_xsk_prod_reserve(&s.prod, 3, &idx);
        for (uint32_t i = 0; i < nb; i++) {
            *(uint64_t *)rewsr_xsk_ring_desc(&s.prod, idx + i) = next_tx++;
        }
        rewsr_xsk_prod_submit(&s.prod, nb);

        uint32_t cidx;
        uint32_t got = rewsr_xsk_cons_peek(&s.cons, 3, &cidx);
        for (uint32_t i = 0; i < got; i++) {
            ASSERT_EQ_U64(next_rx,
                          *(uint64_t *)rewsr_xsk_ring_desc(&s.cons, cidx + i));
            next_rx++;
        }
        rewsr_xsk_cons_release(&s.cons, got);
    }
    ASSERT_EQ_U64(next_tx, next_rx);
    return 0;
}

struct pump_arg {
    struct rewsr_xsk_ring *prod;
    uint32_t count;
};

static void *pump_producer(void *p) {
    struct pump_arg *a = p;
    uint64_t v = 0;
    uint32_t done = 0;
    while (done < a->count) {
        uint32_t idx;
        uint32_t nb = rewsr_xsk_prod_reserve(a->prod, 8, &idx);
        for (uint32_t i = 0; i < nb; i++) {
            *(uint64_t *)rewsr_xsk_ring_desc(a->prod, idx + i) = v++;
        }
        rewsr_xsk_prod_submit(a->prod, nb);
        done += nb;
    }
    return NULL;
}

static int test_concurrent_producer_consumer(void) {
    /* One thread produces, this thread consumes, over a real shared ring.
     * Every descriptor value must arrive exactly once in strict order,
     * which is the single-producer single-consumer AF_XDP guarantee. */
    const uint32_t N = 1000000;
    static uint64_t descs[1024];
    static uint32_t producer, consumer;
    producer = 0;
    consumer = 0;

    struct rewsr_xsk_ring prod, cons;
    rewsr_xsk_ring_init(&prod, &producer, &consumer, (uint8_t *)descs, 1024,
                        sizeof(uint64_t));
    rewsr_xsk_ring_init(&cons, &producer, &consumer, (uint8_t *)descs, 1024,
                        sizeof(uint64_t));

    struct pump_arg arg = {&prod, N};
    pthread_t t;
    ASSERT_EQ_INT(0, pthread_create(&t, NULL, pump_producer, &arg));

    uint64_t expect = 0;
    while (expect < N) {
        uint32_t idx;
        uint32_t got = rewsr_xsk_cons_peek(&cons, 64, &idx);
        for (uint32_t i = 0; i < got; i++) {
            uint64_t v = *(uint64_t *)rewsr_xsk_ring_desc(&cons, idx + i);
            if (v != expect) {
                printf("    order violation: want %llu got %llu\n",
                       (unsigned long long)expect, (unsigned long long)v);
                rewsr_ctest_fail_count++;
                pthread_join(t, NULL);
                return 0;
            }
            expect++;
        }
        rewsr_xsk_cons_release(&cons, got);
    }
    pthread_join(t, NULL);
    ASSERT_EQ_U64(N, expect);
    return 0;
}

REWSR_TEST_MAIN("xsk_ring", {
    RUN_TEST(test_reserve_is_all_or_nothing);
    RUN_TEST(test_fifo_roundtrip);
    RUN_TEST(test_counters_wrap_past_2e32_region);
    RUN_TEST(test_concurrent_producer_consumer);
})
