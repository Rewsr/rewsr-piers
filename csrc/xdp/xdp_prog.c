/* xdp_prog is the in-kernel XDP program that steers packets into the
 * AF_XDP socket for the fabric flow. It is compiled separately with
 * clang -target bpf (not by the host Makefile) and loaded onto the
 * interface; the userspace side in xsk.c binds the matching queue.
 *
 * The policy is deliberately narrow: UDP datagrams whose destination port
 * is the fabric port are redirected into the XSK map for the queue they
 * arrived on, and everything else is passed up to the normal network
 * stack. Redirecting only our own traffic means the host stays reachable
 * for SSH, ARP, and everything else while the data plane runs.
 *
 * This file references the BPF helper and map machinery from the kernel's
 * bpf headers, which are only present in a BPF build environment, so it is
 * wrapped so a host compiler skips it. It is real, loadable code, not a
 * host-linked translation unit. */

#if defined(REWSR_BUILD_BPF)

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>

/* The fabric UDP port the data plane owns. Traffic to any other port is
 * left for the kernel stack. */
#define REWSR_FABRIC_PORT 4789

/* xsks_map is the BPF_MAP_TYPE_XSKMAP the userspace side populates with one
 * socket fd per queue; bpf_redirect_map indexes it by queue id. */
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} xsks_map SEC(".maps");

/* rx_queue_index counts, per queue, how many packets were redirected, for
 * observability from userspace via a pinned map. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u64);
} redirect_count SEC(".maps");

SEC("xdp")
int rewsr_xdp_redirect(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* Every access is bounds checked against data_end because the verifier
     * rejects any load it cannot prove is in range. */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        return XDP_PASS;
    }
    if (eth->h_proto != __builtin_bswap16(ETH_P_IP)) {
        return XDP_PASS;
    }

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end) {
        return XDP_PASS;
    }
    if (ip->protocol != IPPROTO_UDP) {
        return XDP_PASS;
    }

    /* Honor the IPv4 header length so options do not misplace the UDP
     * header. */
    void *l4 = (void *)ip + ip->ihl * 4;
    struct udphdr *udp = l4;
    if ((void *)(udp + 1) > data_end) {
        return XDP_PASS;
    }
    if (udp->dest != __builtin_bswap16(REWSR_FABRIC_PORT)) {
        return XDP_PASS;
    }

    __u32 q = ctx->rx_queue_index;
    __u64 *cnt = bpf_map_lookup_elem(&redirect_count, &q);
    if (cnt) {
        *cnt += 1;
    }

    /* Redirect into the XSK bound to this queue. If none is bound the
     * helper returns XDP_PASS via the flags, so traffic is never black
     * holed. */
    return bpf_redirect_map(&xsks_map, q, XDP_PASS);
}

char _license[] SEC("license") = "GPL";

#else /* !REWSR_BUILD_BPF */

/* On a host (non-BPF) build this file has no code. A single typedef keeps
 * it a valid ISO C translation unit so the host syntax check passes; the
 * real program only exists in a clang -target bpf build. */
typedef int rewsr_xdp_prog_host_placeholder;

#endif /* REWSR_BUILD_BPF */
