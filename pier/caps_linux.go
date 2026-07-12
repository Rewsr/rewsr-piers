//go:build linux

package pier

import (
	"os"
	"syscall"
)

// rlimitMemlock is RLIMIT_MEMLOCK from include/uapi/asm-generic/resource.h
// (value 8 on every Linux architecture). The stdlib syscall package does
// not export it, only golang.org/x/sys/unix does, and this module stays
// dependency-free on purpose.
const rlimitMemlock = 8

// CurrentCaps reads and parses this process's capability sets from
// /proc/self/status. A read or parse failure degrades to Supported=false
// rather than an error: procfs being absent or restricted (some hardened
// containers) is a host condition to report, not a crash.
func CurrentCaps() StatusCaps {
	data, err := os.ReadFile("/proc/self/status")
	if err != nil {
		return StatusCaps{}
	}
	sc, err := ParseStatusCaps(data)
	if err != nil {
		return StatusCaps{}
	}
	return sc
}

// CurrentRlimits reads RLIMIT_MEMLOCK and RLIMIT_NOFILE for this
// process. Getrlimit failing is effectively impossible for valid
// resource numbers, but if it does the result degrades to
// Supported=false like CurrentCaps.
func CurrentRlimits() Rlimits {
	var memlock, nofile syscall.Rlimit
	if err := syscall.Getrlimit(rlimitMemlock, &memlock); err != nil {
		return Rlimits{}
	}
	if err := syscall.Getrlimit(syscall.RLIMIT_NOFILE, &nofile); err != nil {
		return Rlimits{}
	}
	return Rlimits{
		Supported:  true,
		MemlockCur: uint64(memlock.Cur),
		MemlockMax: uint64(memlock.Max),
		NofileCur:  uint64(nofile.Cur),
		NofileMax:  uint64(nofile.Max),
	}
}

// CurrentEUID returns this process's effective uid. Kept alongside
// CurrentCaps because preflight reports both: euid 0 with a stripped
// capability set is a real and misleading combination worth surfacing.
func CurrentEUID() int {
	return os.Geteuid()
}
