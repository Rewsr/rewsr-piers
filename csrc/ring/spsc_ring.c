#include "spsc_ring.h"

#include <string.h>

/* roundup_pow2 returns the smallest power of two at least v, with a floor
 * of 2 so the ring always has at least one usable slot after the
 * empty-slot reservation. */
static uint32_t roundup_pow2(uint32_t v) {
    if (v < 2) {
        return 2;
    }
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

int rewsr_spsc_init(struct rewsr_spsc_ring *r, uint8_t *slots,
                    uint32_t capacity, uint32_t slot_size) {
    if (capacity == 0 || slot_size == 0 || slots == NULL) {
        return -1;
    }
    uint32_t n = roundup_pow2(capacity);
    r->mask = n - 1;
    r->slot_size = slot_size;
    r->slots = slots;
    r->head = 0;
    r->tail = 0;
    return 0;
}

uint32_t rewsr_spsc_capacity(const struct rewsr_spsc_ring *r) {
    /* One slot is reserved to tell full from empty, so live capacity is
     * the slot count minus one. */
    return r->mask; /* (mask + 1) - 1 */
}

int rewsr_spsc_produce(struct rewsr_spsc_ring *r, const void *item) {
    uint32_t tail = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
    uint32_t next = (tail + 1) & r->mask;

    /* Acquire the consumer's head so a full ring is observed correctly:
     * if next equals head the ring has no free slot. */
    uint32_t head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
    if (next == head) {
        return 0;
    }

    memcpy(r->slots + (size_t)tail * r->slot_size, item, r->slot_size);

    /* Release the write so the consumer that later sees this tail also
     * sees the slot contents. */
    __atomic_store_n(&r->tail, next, __ATOMIC_RELEASE);
    return 1;
}

int rewsr_spsc_consume(struct rewsr_spsc_ring *r, void *out) {
    uint32_t head = __atomic_load_n(&r->head, __ATOMIC_RELAXED);

    /* Acquire the producer's tail so we only read a slot the producer has
     * finished publishing. */
    uint32_t tail = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
    if (head == tail) {
        return 0;
    }

    memcpy(out, r->slots + (size_t)head * r->slot_size, r->slot_size);

    uint32_t next = (head + 1) & r->mask;
    __atomic_store_n(&r->head, next, __ATOMIC_RELEASE);
    return 1;
}

uint32_t rewsr_spsc_count(const struct rewsr_spsc_ring *r) {
    uint32_t tail = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
    uint32_t head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
    return (tail - head) & r->mask;
}
