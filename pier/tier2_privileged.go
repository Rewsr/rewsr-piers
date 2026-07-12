//go:build linux && cgo

package pier

/*
#include "cshim/shim.h"
*/
import "C"

import "errors"

// Only tier1_afxdp_linux.go pulls in cshim/shim.c (the function bodies).
// This file only needs the declarations from shim.h: both Go files are
// part of the same linux+cgo package build, so the linker resolves
// rewsr_rdma_probe against the object cgo generates from the other
// file. Including shim.c a second time here would define both C
// functions twice and fail at link time with duplicate symbols.

// Tier2Privileged is the top rung of the ladder: an RDMA/DPDK-class
// pier for hosts with real kernel-bypass NIC hardware. This is the
// fastest, lowest-latency tier and also the most demanding: it needs
// RDMA-capable hardware, matching drivers, and normally root.
//
// This scaffold only probes for device presence. It does not yet attach
// real RDMA verbs or DPDK bindings; see attachRDMADevice below for where
// that would happen.
type Tier2Privileged struct{}

// Name implements Pier.
func (Tier2Privileged) Name() string { return "tier2-privileged" }

// Detect implements Pier. It calls into the C shim to check for the
// presence of an RDMA device node under /dev/infiniband. This is a
// presence check, not a working-hardware guarantee: a device node can
// exist without a usable fabric behind it, but its absence is a hard
// no.
func (Tier2Privileged) Detect() bool {
	return C.rewsr_rdma_probe() == 1
}

// Guarantees implements Pier.
func (Tier2Privileged) Guarantees() Guarantees {
	return Guarantees{
		RequiresRoot:         true,
		RequiresKernelModule: true, // RDMA verbs / mlx5_core or equivalent driver must be loaded
		MaxThroughputMbps:    100000,
		LatencyBudgetUs:      5,
		Description:          "RDMA/DPDK-class kernel-bypass NIC, needs RDMA-capable hardware, a loaded verbs driver, and normally root",
	}
}

// attachRDMADevice is the placeholder call point for real RDMA or DPDK
// bindings. A real implementation would call into the C shim to open
// the verbs device (ibv_open_device), allocate protection domains and
// queue pairs, or hand the NIC to a DPDK poll-mode driver via rte_eal_init
// and friends. None of that is implemented here: this scaffold stops at
// device-presence detection.
//
// PLACEHOLDER: wire real RDMA/DPDK setup here via the C shim.
func attachRDMADevice() error {
	return errors.New("rdma/dpdk attach not implemented in this scaffold")
}
