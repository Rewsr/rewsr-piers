//go:build !linux || !cgo

package pier

// Tier2Privileged is the top rung of the ladder: an RDMA/DPDK-class
// pier for hosts with real kernel-bypass NIC hardware. The real
// implementation probes for RDMA device nodes via a C shim, so it only
// exists on linux+cgo builds (see tier2_privileged.go). On every other
// platform, or when cgo is disabled, this process cannot probe for or
// attach to that hardware, so this tier always reports unavailable.
// This must degrade cleanly, not crash: a macOS dev machine or a
// CGO_ENABLED=0 build is a normal, expected situation, not an error.
type Tier2Privileged struct{}

// Name implements Pier.
func (Tier2Privileged) Name() string { return "tier2-privileged" }

// Detect implements Pier. Always false outside linux+cgo builds.
func (Tier2Privileged) Detect() bool { return false }

// Guarantees implements Pier.
func (Tier2Privileged) Guarantees() Guarantees {
	return Guarantees{
		RequiresRoot:         true,
		RequiresKernelModule: true,
		MaxThroughputMbps:    100000,
		LatencyBudgetUs:      5,
		Description:          "RDMA/DPDK-class kernel-bypass NIC, needs RDMA-capable hardware, a loaded verbs driver, and normally root (unavailable on this build: not linux+cgo)",
	}
}
