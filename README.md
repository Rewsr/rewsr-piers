# rewsr-piers

The permissionless-to-privileged deployment tier ladder for Rewsr. A
"pier" is one rung on that ladder: a way of moving data in or out of a
workload, ordered by how much the host has to give you before you can
use it.

## The idea

Tier 0 is the base: plain UDP sockets, pure Go, no root, no kernel
module, no special hardware. It works on any host with a network stack,
full stop. That is the floor everything else is measured against.

Every tier above tier 0 is opportunistic. It is only used if the current
host genuinely supports it right now, not if it could in theory on some
other machine. Detection is a real check against the live host, not a
guess:

- **tier1-afxdp**: AF_XDP zero-copy sockets on a real NIC queue. Needs
  Linux 4.18+, the bpf filesystem mounted, and normally CAP_NET_RAW or
  root. Detect() reads the running kernel release and calls into a small
  C shim to check for `/sys/fs/bpf`.
- **tier2-privileged**: an RDMA/DPDK-class kernel-bypass tier for hosts
  with real RDMA-capable NICs. Detect() calls into the C shim to check
  for device nodes under `/dev/infiniband`. This scaffold stops at
  device-presence detection; the actual RDMA/DPDK attach point is marked
  as a placeholder in `pier/tier2_privileged.go`.

If a tier is not available, it is skipped, not crashed on. `pier.Select()`
walks the ladder from highest privilege to lowest and returns the first
pier that actually detects as usable, plus a skip reason for every tier
above it explaining why it was not available on this specific machine
right now. That skip list is the point: it is a truth report, not a
feature matrix.

## Layout

- `pier/pier.go` - the `Pier` interface and `Guarantees` struct every
  tier implements.
- `pier/tier0_udp.go` - the baseline UDP pier, plus a real loopback
  send/receive benchmark.
- `pier/tier1_afxdp_linux.go` / `pier/tier1_afxdp_fallback.go` - AF_XDP,
  build-tag paired: the real linux+cgo implementation and a fallback
  that always reports unavailable everywhere else.
- `pier/tier2_privileged.go` / `pier/tier2_privileged_fallback.go` -
  RDMA/DPDK-class tier, same build-tag pairing.
- `pier/cshim/` - the small C shim the linux+cgo piers call into
  (`shim.c` / `shim.h`), compiled straight into the cgo build, no
  prebuilt library needed.
- `pier/ladder.go` - `Ladder()` (all tiers, ordered) and `Select()`
  (what is actually usable on this host, and why the rest were skipped).
- `probe/probe.go` - a fabric-independence audit: shells out to `ip
  link`, `ethtool`, `lspci`, `nproc`, `uname` when they exist, skips
  gracefully when they do not (so it also runs clean on a Mac).
- `cmd/piers/main.go` - the `piers` CLI: `detect`, `probe`, `bench`.

## Build tag pairing

Every privileged tier follows the same two-file pattern used in
`rewsr-complete`'s `internal/crypto`: one file behind
`//go:build linux && cgo` with the real cgo-backed implementation, one
file behind `//go:build !linux || !cgo` with the same type and method
set that always reports the tier unavailable. Nothing outside `pier/`
needs to know which file was actually compiled.

## Build

```bash
go build -o piers ./cmd/piers
```

## Test

```bash
make test           # CGO_ENABLED=0, tier0 + fallback piers only
make test-native     # CGO_ENABLED=1, also builds the linux+cgo piers and C shim (Linux only)
```

## CLI

```bash
./piers detect   # pier.Select() result as JSON: selected tier, guarantees, skip reasons
./piers probe    # fabric-independence audit report as JSON
./piers bench    # tier0 UDP loopback benchmark: latency and throughput
```

## Related

- [github.com/Rewsr/rewsr-complete](https://github.com/Rewsr/rewsr-complete) -
  the main CLI that deploys workloads into TEEs across AWS Nitro
  Enclaves, GCP Confidential Space, and Azure Confidential Containers.
- [github.com/Rewsr/rewsr-operator](https://github.com/Rewsr/rewsr-operator) -
  the Kubernetes operator; its CRD reports which pier a session actually
  used at runtime in `status.Pier`.
