#ifndef REWSR_RING_XSK_H
#define REWSR_RING_XSK_H

#include <stddef.h>
#include <stdint.h>

/* xsk_ring implements the producer/consumer index management for the four
 * AF_XDP rings (fill, completion, RX, TX) exactly as the kernel ABI
 * defines it, but factored out so it can be unit tested without a live
 * socket. In AF_XDP each ring is a shared-memory array of descriptors with
 * two free-running 32-bit counters: a producer index the writing side
 * advances and a consumer index the reading side advances. Neither is
 * masked; the slot for a counter value is (value & mask), and the ring
 * size is always a power of two so the mask is size-1. The gap between the
 * counters, interpreted modulo 2^32, is the number of entries in flight.
 *
 * Userspace produces into the fill and TX rings and consumes from the
 * completion and RX rings, so both directions are needed. Batching is done
 * through cached copies of the far-side counter so the hot path touches
 * the shared (and possibly contended) counter once per batch, not once per
 * descriptor, which is the whole point of the design.
 *
 * The counters live behind pointers so a test can supply plain uint32_t
 * storage while the real path points them at the mmap'd ring headers. */

struct rewsr_xsk_ring {
    uint32_t *producer;  /* advanced by the producing side */
    uint32_t *consumer;  /* advanced by the consuming side */
    uint8_t *ring;       /* size descriptors of desc_size bytes */
    uint32_t mask;       /* size - 1 */
    uint32_t size;       /* number of descriptors, a power of two */
    size_t desc_size;    /* bytes per descriptor */

    /* Cached copy of the far-side counter, refreshed only when a reserve
     * or peek would otherwise fail, so the shared counter is read at most
     * once per batch. */
    uint32_t cached_prod;
    uint32_t cached_cons;
};

/* rewsr_xsk_ring_init wires a ring over caller-provided counter and
 * descriptor storage. producer and consumer must point at 32-bit counters
 * that persist for the ring's life; ring must hold size*desc_size bytes;
 * size must be a power of two. Returns 0 on success, -1 on a bad argument.
 * The counters are NOT reset, matching the real path where the kernel owns
 * their initial (zero) state. */
int rewsr_xsk_ring_init(struct rewsr_xsk_ring *r, uint32_t *producer,
                        uint32_t *consumer, uint8_t *ring, uint32_t size,
                        size_t desc_size);

/* rewsr_xsk_ring_desc returns a pointer to the descriptor slot for a
 * counter value, i.e. ring + (idx & mask) * desc_size. */
void *rewsr_xsk_ring_desc(const struct rewsr_xsk_ring *r, uint32_t idx);

/* rewsr_xsk_prod_reserve tries to reserve nb slots on the producing side.
 * On success it sets *idx to the first counter value to write and returns
 * nb; if fewer than nb are free it returns 0 and reserves nothing (all or
 * nothing, matching libbpf's xsk_ring_prod__reserve). It refreshes the
 * cached consumer counter at most once when the fast check shows the ring
 * full. */
uint32_t rewsr_xsk_prod_reserve(struct rewsr_xsk_ring *r, uint32_t nb,
                                uint32_t *idx);

/* rewsr_xsk_prod_submit publishes nb previously reserved slots by
 * advancing the producer counter with a release store, so the far side
 * sees the descriptor writes before the counter bump. */
void rewsr_xsk_prod_submit(struct rewsr_xsk_ring *r, uint32_t nb);

/* rewsr_xsk_prod_free returns how many producer slots are currently free
 * without reserving, refreshing the cached consumer counter. */
uint32_t rewsr_xsk_prod_free(struct rewsr_xsk_ring *r);

/* rewsr_xsk_cons_peek reports how many entries (up to nb) are available to
 * consume and sets *idx to the first counter value to read. It refreshes
 * the cached producer counter with an acquire load so descriptor contents
 * are visible. Returns the available count, which may be less than nb. */
uint32_t rewsr_xsk_cons_peek(struct rewsr_xsk_ring *r, uint32_t nb,
                             uint32_t *idx);

/* rewsr_xsk_cons_release returns nb consumed slots to the producing side
 * by advancing the consumer counter with a release store. */
void rewsr_xsk_cons_release(struct rewsr_xsk_ring *r, uint32_t nb);

/* rewsr_xsk_cons_available returns how many entries are ready to consume
 * without consuming, refreshing the cached producer counter. */
uint32_t rewsr_xsk_cons_available(struct rewsr_xsk_ring *r);

#endif /* REWSR_RING_XSK_H */
