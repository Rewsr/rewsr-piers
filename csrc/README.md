# csrc: native data plane

The C half of rewsr-piers. Tier 0 (UDP) is pure Go; tier 1 is AF_XDP, and
that lives here along with the packet machinery, host probes, and placement
helpers the data plane needs. Go stays the control plane and the tier
ladder; C is where the per-packet work and the syscalls happen.

## Layout

- `net/` — checksum (RFC 1071), Ethernet/IPv4/UDP build+parse, the pktgen
  fast path (incremental-checksum template), ARP, 802.1Q VLAN, VXLAN
  overlay encap/decap, RSS Toeplitz hashing, and the netlink and ethtool
  ioctl probes.
- `ring/` — the SPSC software ring and the AF_XDP producer/consumer index
  management (fill/completion/rx/tx).
- `xdp/` — the AF_XDP socket setup (UMEM, ring mmap, bind), the free-frame
  pool, and the XDP redirect BPF program.
- `sys/` — huge-page allocation, CPU affinity, NUMA topology for
  NIC-local placement.
- `dataplane/` — the tier1 RX/TX worker loop and its stats.
- `util/` — leveled logging and hexdump.
- `bench/` — a standalone UDP loopback benchmark, the C counterpart to the
  Go tier0 benchmark, configured by environment variable.
- `test/` — a dependency-free C test harness (`ctest.h`) and a suite per
  module.

## Portable vs Linux

Everything that is pure logic (checksums, packet and overlay codecs, ring
and pool bookkeeping, RSS, the parsers for netlink/ethtool/meminfo/cpulist)
compiles and runs its tests on any POSIX host. The syscall-bound transports
(AF_XDP, rtnetlink, SIOCETHTOOL, sched affinity, MAP_HUGETLB) are guarded
with `#if defined(__linux__)` and return an unsupported status elsewhere, so
the whole tree still passes `make syntax` on a non-Linux dev machine while
only doing real work on Linux.

## Build

    make            # build librewsrfast.a
    make test       # build and run the C test suite
    make syntax     # cc -fsyntax-only every source, portable and guarded
    make bench      # build the standalone loopback benchmark

## Wiring into Go

The Go tier ladder calls the AF_XDP path over cgo: a `//go:build linux`
bridge links `librewsrfast.a` and calls `rewsr_dp_worker_*`, with a
non-Linux stub so `go build ./...` stays green off Linux. The BPF program in
`xdp/xdp_prog.c` is built separately with `clang -target bpf`.
