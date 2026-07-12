#ifndef REWSR_RING_SPSC_H
#define REWSR_RING_SPSC_H

#include <stddef.h>
#include <stdint.h>

/* spsc_ring is a single-producer single-consumer bounded queue of fixed
 * size slots. It is the software fallback the tier ladder uses to shuttle
 * frame descriptors between a receive thread and a worker when AF_XDP's
 * own rings are not available. Correctness rests on two rules: exactly one
 * thread calls the produce side and exactly one calls the consume side,
 * and the head and tail indices are published with release stores and read
 * with acquire loads so a consumer never sees a slot before its contents.
 *
 * The capacity is rounded up to a power of two so the modulo reduction is
 * a mask. One slot is always left empty to distinguish full from empty
 * without a separate count, so a ring created with capacity N holds N-1
 * live entries. */

struct rewsr_spsc_ring {
    uint32_t mask;      /* capacity - 1, capacity a power of two */
    uint32_t slot_size; /* bytes per slot */
    uint8_t *slots;     /* mask+1 slots of slot_size bytes */

    /* head is written only by the consumer, tail only by the producer.
     * They are read by the other side. Kept far apart in the struct to
     * avoid false sharing on the same cache line. */
    _Alignas(64) uint32_t head;
    _Alignas(64) uint32_t tail;
};

/* rewsr_spsc_init prepares a ring over caller-provided storage. slots must
 * point at (roundup_pow2(capacity)) * slot_size bytes that outlive the
 * ring. Returns 0 on success, -1 if capacity or slot_size is zero. */
int rewsr_spsc_init(struct rewsr_spsc_ring *r, uint8_t *slots,
                    uint32_t capacity, uint32_t slot_size);

/* rewsr_spsc_capacity returns the number of live entries the ring can hold
 * (one less than the rounded-up slot count). */
uint32_t rewsr_spsc_capacity(const struct rewsr_spsc_ring *r);

/* rewsr_spsc_produce copies one slot_size-byte item into the ring.
 * Producer side only. Returns 1 on success, 0 if the ring is full. */
int rewsr_spsc_produce(struct rewsr_spsc_ring *r, const void *item);

/* rewsr_spsc_consume copies the next item out into out (slot_size bytes).
 * Consumer side only. Returns 1 on success, 0 if the ring is empty. */
int rewsr_spsc_consume(struct rewsr_spsc_ring *r, void *out);

/* rewsr_spsc_count returns an approximate live-entry count. It is exact
 * when called from a thread that has quiesced both sides, and otherwise a
 * consistent snapshot suitable for gauges, never a torn value. */
uint32_t rewsr_spsc_count(const struct rewsr_spsc_ring *r);

#endif /* REWSR_RING_SPSC_H */
