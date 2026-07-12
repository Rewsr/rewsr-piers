#ifndef REWSR_XDP_UAPI_H
#define REWSR_XDP_UAPI_H

#include <stdint.h>

/* rewsr_xdp_uapi mirrors the AF_XDP definitions from the Linux kernel
 * uapi header linux/if_xdp.h and the address family and socket-option
 * numbers from linux/socket.h. They are reproduced here rather than
 * included so the data-plane C compiles and type-checks on a non-Linux
 * developer machine (macOS) where those kernel headers are absent; the
 * socket calls that use them are gated at runtime and only succeed on a
 * Linux host with an AF_XDP-capable kernel. Every constant below carries
 * the name it has in the kernel uapi so it can be checked against the
 * source of truth. */

/* linux/socket.h: AF_XDP address family and its SOL_XDP option level. */
#define REWSR_AF_XDP 44
#define REWSR_SOL_XDP 283
#define REWSR_PF_XDP REWSR_AF_XDP

/* linux/if_xdp.h: setsockopt options on an AF_XDP socket. */
#define REWSR_XDP_MMAP_OFFSETS 1
#define REWSR_XDP_RX_RING 2
#define REWSR_XDP_TX_RING 3
#define REWSR_XDP_UMEM_REG 4
#define REWSR_XDP_UMEM_FILL_RING 5
#define REWSR_XDP_UMEM_COMPLETION_RING 6
#define REWSR_XDP_STATISTICS 7
#define REWSR_XDP_OPTIONS 8

/* Bind flags in struct sockaddr_xdp.sxdp_flags. */
#define REWSR_XDP_SHARED_UMEM (1U << 0)
#define REWSR_XDP_COPY (1U << 1)
#define REWSR_XDP_ZEROCOPY (1U << 2)
#define REWSR_XDP_USE_NEED_WAKEUP (1U << 3)

/* Ring flags in the xdp_ring_offset flags fields. */
#define REWSR_XDP_RING_NEED_WAKEUP (1U << 0)

/* mmap page offsets for the four rings, from linux/if_xdp.h. The rings are
 * distinguished by the offset passed to mmap on the socket fd. */
#define REWSR_XDP_PGOFF_RX_RING 0
#define REWSR_XDP_PGOFF_TX_RING 0x80000000ULL
#define REWSR_XDP_UMEM_PGOFF_FILL_RING 0x100000000ULL
#define REWSR_XDP_UMEM_PGOFF_COMPLETION_RING 0x180000000ULL

/* struct xdp_umem_reg: registers the UMEM (the packet buffer area) with
 * the socket. addr is the userspace base of the region, len its size,
 * chunk_size the per-frame stride, headroom the reserved bytes at the
 * front of each frame. */
struct rewsr_xdp_umem_reg {
    uint64_t addr;
    uint64_t len;
    uint32_t chunk_size;
    uint32_t headroom;
    uint32_t flags;
};

/* struct xdp_ring_offset: where the producer, consumer, and descriptor
 * array sit within a mapped ring, returned by XDP_MMAP_OFFSETS. */
struct rewsr_xdp_ring_offset {
    uint64_t producer;
    uint64_t consumer;
    uint64_t desc;
    uint64_t flags;
};

/* struct xdp_mmap_offsets: the offsets for all four rings. */
struct rewsr_xdp_mmap_offsets {
    struct rewsr_xdp_ring_offset rx;
    struct rewsr_xdp_ring_offset tx;
    struct rewsr_xdp_ring_offset fr; /* fill ring */
    struct rewsr_xdp_ring_offset cr; /* completion ring */
};

/* struct xdp_desc: an RX or TX descriptor, a UMEM-relative frame address
 * plus the packet length and option bits. */
struct rewsr_xdp_desc {
    uint64_t addr;
    uint32_t len;
    uint32_t options;
};

/* struct sockaddr_xdp: the bind address selecting an interface queue. */
struct rewsr_sockaddr_xdp {
    uint16_t sxdp_family;
    uint16_t sxdp_flags;
    uint32_t sxdp_ifindex;
    uint32_t sxdp_queue_id;
    uint32_t sxdp_shared_umem_fd;
};

/* struct xdp_statistics: drop and error counters read via XDP_STATISTICS. */
struct rewsr_xdp_statistics {
    uint64_t rx_dropped;
    uint64_t rx_invalid_descs;
    uint64_t tx_invalid_descs;
    uint64_t rx_ring_full;
    uint64_t rx_fill_ring_empty_descs;
    uint64_t tx_ring_empty_descs;
};

#endif /* REWSR_XDP_UAPI_H */
