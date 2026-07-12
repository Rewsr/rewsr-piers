#include "xsk_ring.h"

/* is_pow2 rejects a non-power-of-two size, which would make the mask
 * indexing wrong. */
static int is_pow2(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

int rewsr_xsk_ring_init(struct rewsr_xsk_ring *r, uint32_t *producer,
                        uint32_t *consumer, uint8_t *ring, uint32_t size,
                        size_t desc_size) {
    if (producer == NULL || consumer == NULL || ring == NULL ||
        desc_size == 0 || !is_pow2(size)) {
        return -1;
    }
    r->producer = producer;
    r->consumer = consumer;
    r->ring = ring;
    r->size = size;
    r->mask = size - 1;
    r->desc_size = desc_size;
    r->cached_prod = __atomic_load_n(producer, __ATOMIC_RELAXED);
    r->cached_cons = __atomic_load_n(consumer, __ATOMIC_RELAXED);
    return 0;
}

void *rewsr_xsk_ring_desc(const struct rewsr_xsk_ring *r, uint32_t idx) {
    return r->ring + (size_t)(idx & r->mask) * r->desc_size;
}

uint32_t rewsr_xsk_prod_reserve(struct rewsr_xsk_ring *r, uint32_t nb,
                                uint32_t *idx) {
    /* Free slots = size - (producer - cached_consumer). The subtraction is
     * modulo 2^32, which is correct because both counters are free running
     * and the in-flight count never exceeds size. */
    uint32_t free_slots = r->size - (r->cached_prod - r->cached_cons);
    if (free_slots < nb) {
        /* The cached consumer counter may be stale; refresh it once with
         * an acquire load and re-check before giving up. */
        r->cached_cons = __atomic_load_n(r->consumer, __ATOMIC_ACQUIRE);
        free_slots = r->size - (r->cached_prod - r->cached_cons);
        if (free_slots < nb) {
            return 0;
        }
    }
    *idx = r->cached_prod;
    r->cached_prod += nb;
    return nb;
}

void rewsr_xsk_prod_submit(struct rewsr_xsk_ring *r, uint32_t nb) {
    uint32_t p = __atomic_load_n(r->producer, __ATOMIC_RELAXED);
    /* Release so a consumer that later reads this producer counter also
     * sees the descriptor writes made before submit. */
    __atomic_store_n(r->producer, p + nb, __ATOMIC_RELEASE);
}

uint32_t rewsr_xsk_prod_free(struct rewsr_xsk_ring *r) {
    r->cached_cons = __atomic_load_n(r->consumer, __ATOMIC_ACQUIRE);
    return r->size - (r->cached_prod - r->cached_cons);
}

uint32_t rewsr_xsk_cons_peek(struct rewsr_xsk_ring *r, uint32_t nb,
                             uint32_t *idx) {
    uint32_t avail = r->cached_prod - r->cached_cons;
    if (avail < nb) {
        /* Refresh the cached producer counter with an acquire load so the
         * descriptors it published are visible before we read them. */
        r->cached_prod = __atomic_load_n(r->producer, __ATOMIC_ACQUIRE);
        avail = r->cached_prod - r->cached_cons;
    }
    if (avail > nb) {
        avail = nb;
    }
    if (avail > 0) {
        *idx = r->cached_cons;
    }
    return avail;
}

void rewsr_xsk_cons_release(struct rewsr_xsk_ring *r, uint32_t nb) {
    uint32_t c = __atomic_load_n(r->consumer, __ATOMIC_RELAXED);
    __atomic_store_n(r->consumer, c + nb, __ATOMIC_RELEASE);
    r->cached_cons += nb;
}

uint32_t rewsr_xsk_cons_available(struct rewsr_xsk_ring *r) {
    r->cached_prod = __atomic_load_n(r->producer, __ATOMIC_ACQUIRE);
    return r->cached_prod - r->cached_cons;
}
