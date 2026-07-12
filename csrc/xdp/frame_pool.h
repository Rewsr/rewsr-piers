#ifndef REWSR_XDP_FRAME_POOL_H
#define REWSR_XDP_FRAME_POOL_H

#include <stddef.h>
#include <stdint.h>

/* frame_pool manages the UMEM frame addresses an AF_XDP socket cycles
 * through. The UMEM is a flat region carved into equal chunks; at any
 * moment a chunk is either owned by the kernel (posted on the fill ring or
 * sitting in an RX/TX descriptor) or free for userspace to hand out. The
 * pool is the free set: a stack of chunk-relative addresses. A stack (LIFO)
 * rather than a queue is deliberate, the most recently freed frame is the
 * warmest in cache, so reusing it first helps the fast path.
 *
 * This is pure bookkeeping over integer addresses with no syscalls, so it
 * builds and is tested on any platform even though the frames it tracks
 * only mean something to a live AF_XDP socket. */

struct rewsr_frame_pool {
    uint64_t *stack;   /* free chunk addresses, top at count-1 */
    uint32_t count;    /* number currently free */
    uint32_t capacity; /* total frames the pool can track */
    uint32_t chunk_size;
};

/* rewsr_frame_pool_init prepares a pool over caller-provided stack storage
 * (capacity uint64_t entries) and seeds it full: every one of the
 * num_frames chunks of chunk_size bytes is free, at addresses 0,
 * chunk_size, 2*chunk_size, ... Returns 0 on success, -1 on a bad
 * argument. */
int rewsr_frame_pool_init(struct rewsr_frame_pool *p, uint64_t *stack,
                          uint32_t num_frames, uint32_t chunk_size);

/* rewsr_frame_pool_alloc pops one free frame address into *addr. Returns 1
 * on success, 0 if the pool is empty (every frame is with the kernel). */
int rewsr_frame_pool_alloc(struct rewsr_frame_pool *p, uint64_t *addr);

/* rewsr_frame_pool_free returns a frame address to the pool. The address is
 * masked down to its chunk base so a descriptor address that points partway
 * into a frame (the kernel may return an offset for headroom) still frees
 * the right chunk. Returns 1 on success, 0 if the pool is already full or
 * the address is out of range. */
int rewsr_frame_pool_free(struct rewsr_frame_pool *p, uint64_t addr);

/* rewsr_frame_pool_available returns how many frames are currently free. */
uint32_t rewsr_frame_pool_available(const struct rewsr_frame_pool *p);

/* rewsr_frame_pool_chunk_base masks an address to its owning chunk base. */
uint64_t rewsr_frame_pool_chunk_base(const struct rewsr_frame_pool *p,
                                     uint64_t addr);

#endif /* REWSR_XDP_FRAME_POOL_H */
