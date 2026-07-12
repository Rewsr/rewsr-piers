#include "frame_pool.h"

/* is_pow2 gates the chunk-base mask: a power-of-two chunk size lets the
 * base be computed with a mask instead of a division on the free path. */
static int is_pow2(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

int rewsr_frame_pool_init(struct rewsr_frame_pool *p, uint64_t *stack,
                          uint32_t num_frames, uint32_t chunk_size) {
    if (stack == NULL || num_frames == 0 || !is_pow2(chunk_size)) {
        return -1;
    }
    p->stack = stack;
    p->capacity = num_frames;
    p->chunk_size = chunk_size;

    /* Seed the pool full. Frames are pushed in descending address order so
     * the first allocation returns frame 0, which keeps a fresh pool's
     * allocation order predictable for tests and logs. */
    p->count = 0;
    for (uint32_t i = num_frames; i > 0; i--) {
        p->stack[p->count++] = (uint64_t)(i - 1) * chunk_size;
    }
    return 0;
}

uint64_t rewsr_frame_pool_chunk_base(const struct rewsr_frame_pool *p,
                                     uint64_t addr) {
    return addr & ~((uint64_t)p->chunk_size - 1);
}

int rewsr_frame_pool_alloc(struct rewsr_frame_pool *p, uint64_t *addr) {
    if (p->count == 0) {
        return 0;
    }
    *addr = p->stack[--p->count];
    return 1;
}

int rewsr_frame_pool_free(struct rewsr_frame_pool *p, uint64_t addr) {
    if (p->count >= p->capacity) {
        return 0;
    }
    uint64_t base = rewsr_frame_pool_chunk_base(p, addr);
    if (base >= (uint64_t)p->capacity * p->chunk_size) {
        return 0;
    }
    p->stack[p->count++] = base;
    return 1;
}

uint32_t rewsr_frame_pool_available(const struct rewsr_frame_pool *p) {
    return p->count;
}
