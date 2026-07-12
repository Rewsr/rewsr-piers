//go:build !linux || !cgo

package pier

// Tier1AFXDP is the AF_XDP privileged pier. The real implementation
// needs a Linux kernel with XDP/eBPF support and calls into a C shim via
// cgo, so it only exists on linux+cgo builds (see tier1_afxdp_linux.go).
// On every other platform, or when cgo is disabled, AF_XDP is simply not
// something this process can set up, so this tier always reports
// unavailable. This must degrade cleanly, not crash: a non-Linux host or
// a CGO_ENABLED=0 build is a normal, expected situation, not an error.
type Tier1AFXDP struct{}

// Name implements Pier.
func (Tier1AFXDP) Name() string { return "tier1-afxdp" }

// Detect implements Pier. Always false outside linux+cgo builds.
func (Tier1AFXDP) Detect() bool { return false }

// Guarantees implements Pier.
func (Tier1AFXDP) Guarantees() Guarantees {
	return Guarantees{
		RequiresRoot:         true,
		RequiresKernelModule: false,
		MaxThroughputMbps:    40000,
		LatencyBudgetUs:      50,
		Description:          "AF_XDP zero-copy socket on a real NIC queue, needs Linux 4.18+ with bpf fs mounted and normally CAP_NET_RAW or root (unavailable on this build: not linux+cgo)",
	}
}
