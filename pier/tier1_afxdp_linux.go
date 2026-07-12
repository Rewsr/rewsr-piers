//go:build linux && cgo

package pier

/*
#include "cshim/shim.c"
*/
import "C"

import (
	"os"
	"strconv"
	"strings"
)

// Tier1AFXDP is the AF_XDP privileged pier. AF_XDP lets a userspace
// process attach to a zero-copy ring on a real NIC queue, bypassing most
// of the normal socket stack. It needs a Linux kernel with XDP/eBPF
// support, the bpf filesystem mounted, and normally CAP_NET_RAW or root
// to attach to a live interface.
type Tier1AFXDP struct{}

// Name implements Pier.
func (Tier1AFXDP) Name() string { return "tier1-afxdp" }

// Detect implements Pier. It checks the running kernel release against
// the minimum version AF_XDP shipped in, then calls into the C shim to
// check whether the bpf filesystem is actually present, the same check
// that would gate real socket setup.
func (Tier1AFXDP) Detect() bool {
	if !kernelSupportsAFXDP() {
		return false
	}
	return C.rewsr_afxdp_probe() == 1
}

// Guarantees implements Pier.
func (Tier1AFXDP) Guarantees() Guarantees {
	return Guarantees{
		RequiresRoot:         true,
		RequiresKernelModule: false, // AF_XDP is built into mainline kernels, no extra module needed
		MaxThroughputMbps:    40000,
		LatencyBudgetUs:      50,
		Description:          "AF_XDP zero-copy socket on a real NIC queue, needs Linux 4.18+ with bpf fs mounted and normally CAP_NET_RAW or root",
	}
}

// kernelSupportsAFXDP checks the running kernel release against the
// minimum version AF_XDP shipped in (4.18). It reads /proc/sys/kernel/
// osrelease directly rather than calling uname(2), since the osrelease
// file has a stable format and avoids architecture-specific struct
// layout issues in the Go syscall package.
func kernelSupportsAFXDP() bool {
	data, err := os.ReadFile("/proc/sys/kernel/osrelease")
	if err != nil {
		return false
	}
	major, minor, ok := parseKernelVersion(strings.TrimSpace(string(data)))
	if !ok {
		return false
	}
	if major > 4 {
		return true
	}
	return major == 4 && minor >= 18
}

// parseKernelVersion pulls the major and minor version numbers out of a
// kernel release string such as "6.8.0-31-generic" or "4.18.0-553".
func parseKernelVersion(release string) (major, minor int, ok bool) {
	parts := strings.SplitN(release, ".", 3)
	if len(parts) < 2 {
		return 0, 0, false
	}
	major, err := strconv.Atoi(parts[0])
	if err != nil {
		return 0, 0, false
	}
	minor, err = strconv.Atoi(parts[1])
	if err != nil {
		return 0, 0, false
	}
	return major, minor, true
}
