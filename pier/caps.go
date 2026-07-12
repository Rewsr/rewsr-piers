package pier

import (
	"fmt"
	"math/bits"
	"strconv"
	"strings"
)

// capNames maps every standard Linux capability bit index to its name,
// CAP_CHOWN = 0 through CAP_CHECKPOINT_RESTORE = 40, matching
// include/uapi/linux/capability.h as of kernel 5.9 (the last kernel to
// add a capability). Bits above the end of this table are rendered
// numerically rather than dropped, in case a future kernel grows the set
// before this table catches up.
var capNames = [...]string{
	0:  "CAP_CHOWN",
	1:  "CAP_DAC_OVERRIDE",
	2:  "CAP_DAC_READ_SEARCH",
	3:  "CAP_FOWNER",
	4:  "CAP_FSETID",
	5:  "CAP_KILL",
	6:  "CAP_SETGID",
	7:  "CAP_SETUID",
	8:  "CAP_SETPCAP",
	9:  "CAP_LINUX_IMMUTABLE",
	10: "CAP_NET_BIND_SERVICE",
	11: "CAP_NET_BROADCAST",
	12: "CAP_NET_ADMIN",
	13: "CAP_NET_RAW",
	14: "CAP_IPC_LOCK",
	15: "CAP_IPC_OWNER",
	16: "CAP_SYS_MODULE",
	17: "CAP_SYS_RAWIO",
	18: "CAP_SYS_CHROOT",
	19: "CAP_SYS_PTRACE",
	20: "CAP_SYS_PACCT",
	21: "CAP_SYS_ADMIN",
	22: "CAP_SYS_BOOT",
	23: "CAP_SYS_NICE",
	24: "CAP_SYS_RESOURCE",
	25: "CAP_SYS_TIME",
	26: "CAP_SYS_TTY_CONFIG",
	27: "CAP_MKNOD",
	28: "CAP_LEASE",
	29: "CAP_AUDIT_WRITE",
	30: "CAP_AUDIT_CONTROL",
	31: "CAP_SETFCAP",
	32: "CAP_MAC_OVERRIDE",
	33: "CAP_MAC_ADMIN",
	34: "CAP_SYSLOG",
	35: "CAP_WAKE_ALARM",
	36: "CAP_BLOCK_SUSPEND",
	37: "CAP_AUDIT_READ",
	38: "CAP_PERFMON",
	39: "CAP_BPF",
	40: "CAP_CHECKPOINT_RESTORE",
}

// capBit is the reverse lookup of capNames.
var capBit = func() map[string]int {
	m := make(map[string]int, len(capNames))
	for i, n := range capNames {
		m[n] = i
	}
	return m
}()

// CapSet is a Linux capability set as a 64-bit mask, one bit per
// capability index. This is the exact representation used by the CapEff,
// CapPrm and CapBnd lines in /proc/self/status: 16 lowercase hex digits,
// bit N set meaning the capability with index N is held. A fully
// privileged root process on a 5.9+ kernel reads 000001ffffffffff.
type CapSet uint64

// ParseCapMask parses one /proc/self/status capability mask value, for
// example "000001ffffffffff". A leading "0x" is tolerated even though
// /proc never emits one.
func ParseCapMask(s string) (CapSet, error) {
	s = strings.TrimSpace(s)
	s = strings.TrimPrefix(strings.TrimPrefix(s, "0x"), "0X")
	if s == "" {
		return 0, fmt.Errorf("empty capability mask")
	}
	v, err := strconv.ParseUint(s, 16, 64)
	if err != nil {
		return 0, fmt.Errorf("bad capability mask %q: %w", s, err)
	}
	return CapSet(v), nil
}

// CapSetOf builds a CapSet from capability names. Unknown names are an
// error rather than silently dropped, since a typo in a requirements
// table would otherwise make a requirement unenforceable.
func CapSetOf(names ...string) (CapSet, error) {
	var c CapSet
	for _, n := range names {
		bit, ok := capBit[n]
		if !ok {
			return 0, fmt.Errorf("unknown capability name %q", n)
		}
		c |= 1 << uint(bit)
	}
	return c, nil
}

// FullCapSet returns the set with every capability in the table held.
// This is what CapEff reads for a real root process on a 5.9+ kernel, so
// it doubles as the precise "is this effectively root" reference set.
func FullCapSet() CapSet {
	return CapSet(1<<uint(len(capNames))) - 1
}

// Has reports whether the named capability is in the set. Names are the
// exact uppercase kernel names, e.g. "CAP_NET_RAW". Unknown names are
// simply not in any set.
func (c CapSet) Has(name string) bool {
	bit, ok := capBit[name]
	if !ok {
		return false
	}
	return c.HasIndex(bit)
}

// HasIndex reports whether the capability with the given bit index is in
// the set.
func (c CapSet) HasIndex(bit int) bool {
	if bit < 0 || bit > 63 {
		return false
	}
	return c&(1<<uint(bit)) != 0
}

// Count returns how many capabilities are in the set.
func (c CapSet) Count() int {
	return bits.OnesCount64(uint64(c))
}

// Empty reports whether the set holds no capabilities at all, the normal
// state of an unprivileged process.
func (c CapSet) Empty() bool { return c == 0 }

// Names returns the names of every capability in the set, ordered by bit
// index. Set bits beyond the known table are rendered as "CAP_<index>"
// so nothing a newer kernel granted is silently hidden.
func (c CapSet) Names() []string {
	var out []string
	for bit := 0; bit < 64; bit++ {
		if c&(1<<uint(bit)) == 0 {
			continue
		}
		if bit < len(capNames) {
			out = append(out, capNames[bit])
		} else {
			out = append(out, fmt.Sprintf("CAP_%d", bit))
		}
	}
	return out
}

// String renders the set in the /proc/self/status format: 16 lowercase
// hex digits, no 0x prefix.
func (c CapSet) String() string {
	return fmt.Sprintf("%016x", uint64(c))
}

// MarshalJSON emits the /proc-style hex form, e.g. "000001ffffffffff",
// so reports stay directly comparable with what an operator sees in
// /proc/self/status on the host.
func (c CapSet) MarshalJSON() ([]byte, error) {
	return []byte(`"` + c.String() + `"`), nil
}

// UnmarshalJSON accepts the same hex form MarshalJSON produces.
func (c *CapSet) UnmarshalJSON(data []byte) error {
	s := strings.Trim(string(data), `"`)
	v, err := ParseCapMask(s)
	if err != nil {
		return err
	}
	*c = v
	return nil
}

// StatusCaps holds the three capability sets that matter for tier
// eligibility, as read from /proc/self/status. Effective is what the
// kernel actually checks on each syscall; Permitted is what the process
// could raise into Effective; Bounding caps what execve can ever grant.
//
// Supported records whether these values came from a real read.
// ParseStatusCaps sets it on success; the non-Linux fallback leaves the
// whole struct zero with Supported false.
type StatusCaps struct {
	Supported bool   `json:"supported"`
	Effective CapSet `json:"effective"`
	Permitted CapSet `json:"permitted"`
	Bounding  CapSet `json:"bounding"`
}

// ParseStatusCaps extracts CapEff, CapPrm and CapBnd from the contents
// of /proc/self/status. The relevant lines look like
//
//	CapPrm:	000001ffffffffff
//	CapEff:	000001ffffffffff
//	CapBnd:	000001ffffffffff
//
// with a single tab between the key and the 16-digit hex mask. All three
// lines are present on every Linux kernel this project can run on, so a
// missing line means the input was not a real status file and is an
// error.
func ParseStatusCaps(data []byte) (StatusCaps, error) {
	var (
		sc                     StatusCaps
		gotEff, gotPrm, gotBnd bool
	)
	for _, line := range strings.Split(string(data), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		var dst *CapSet
		switch fields[0] {
		case "CapEff:":
			dst, gotEff = &sc.Effective, true
		case "CapPrm:":
			dst, gotPrm = &sc.Permitted, true
		case "CapBnd:":
			dst, gotBnd = &sc.Bounding, true
		default:
			continue
		}
		v, err := ParseCapMask(fields[1])
		if err != nil {
			return StatusCaps{}, fmt.Errorf("%s: %w", strings.TrimSuffix(fields[0], ":"), err)
		}
		*dst = v
	}
	if !gotEff || !gotPrm || !gotBnd {
		var missing []string
		if !gotEff {
			missing = append(missing, "CapEff")
		}
		if !gotPrm {
			missing = append(missing, "CapPrm")
		}
		if !gotBnd {
			missing = append(missing, "CapBnd")
		}
		return StatusCaps{}, fmt.Errorf("status data is missing %s lines", strings.Join(missing, ", "))
	}
	sc.Supported = true
	return sc, nil
}

// Rlimits holds the two resource limits that gate the privileged piers:
// RLIMIT_MEMLOCK (AF_XDP UMEM and RDMA memory registration both pin
// pages) and RLIMIT_NOFILE (each NIC queue costs descriptors). Values
// are bytes for memlock and descriptor counts for nofile; an unlimited
// limit is the kernel's RLIM_INFINITY, all 64 bits set, so plain >=
// comparisons still do the right thing.
type Rlimits struct {
	Supported  bool   `json:"supported"`
	MemlockCur uint64 `json:"memlock_cur"`
	MemlockMax uint64 `json:"memlock_max"`
	NofileCur  uint64 `json:"nofile_cur"`
	NofileMax  uint64 `json:"nofile_max"`
}

// TierRequirements is the precise privilege bill of materials for one
// tier: which capabilities the kernel will actually check, how much
// locked memory the tier pins, and whether it needs full root or a
// loaded kernel module. Guarantees.RequiresRoot stays the coarse
// operator-facing answer; this is the fine-grained one Deficit scores
// against.
type TierRequirements struct {
	Tier                 string   `json:"tier"`
	Capabilities         []string `json:"capabilities,omitempty"`
	AnyOfCapabilities    []string `json:"any_of_capabilities,omitempty"`
	MinMemlockBytes      uint64   `json:"min_memlock_bytes,omitempty"`
	RequiresRoot         bool     `json:"requires_root"`
	RequiresKernelModule bool     `json:"requires_kernel_module"`
}

// afxdpMinMemlockBytes is the locked-memory headroom a minimal AF_XDP
// setup needs: one single-queue UMEM of 4096 descriptors times 4096-byte
// frames is 16 MiB that must stay resident. Kernels before 5.11 charge
// UMEM against RLIMIT_MEMLOCK (newer ones charge the memcg instead), so
// this is the conservative bound that works on both.
const afxdpMinMemlockBytes = 16 << 20

// TierRequirementsTable maps each tier's Name() string to its real
// privilege requirements. The keys are taken from the tier types
// themselves so a renamed tier breaks this table at compile time rather
// than silently orphaning its entry.
//
// tier1-afxdp: binding an AF_XDP socket to a live queue checks
// CAP_NET_RAW, and loading the redirect program needs CAP_BPF, which was
// split out of CAP_SYS_ADMIN in kernel 5.8; on older kernels the same
// operations demand CAP_SYS_ADMIN itself, so either satisfies the
// requirement.
//
// tier2-privileged: configuring the NIC for kernel bypass checks
// CAP_NET_ADMIN, and the verbs/DPDK setup paths (hugepage mapping, VFIO
// or uverbs device access) are root-gated on stock hosts.
func TierRequirementsTable() map[string]TierRequirements {
	return map[string]TierRequirements{
		Tier0UDP{}.Name(): {
			Tier: Tier0UDP{}.Name(),
		},
		Tier1AFXDP{}.Name(): {
			Tier:              Tier1AFXDP{}.Name(),
			Capabilities:      []string{"CAP_NET_RAW"},
			AnyOfCapabilities: []string{"CAP_BPF", "CAP_SYS_ADMIN"},
			MinMemlockBytes:   afxdpMinMemlockBytes,
		},
		Tier2Privileged{}.Name(): {
			Tier:                 Tier2Privileged{}.Name(),
			Capabilities:         []string{"CAP_NET_ADMIN"},
			RequiresRoot:         true,
			RequiresKernelModule: true,
		},
	}
}

// RequirementsForTier looks up the requirements for one tier by its
// Name() string.
func RequirementsForTier(name string) (TierRequirements, bool) {
	req, ok := TierRequirementsTable()[name]
	return req, ok
}

// Trivial reports whether these requirements demand nothing at all, like
// tier0's. A trivial requirement can never have a deficit and never
// needs verification.
func (req TierRequirements) Trivial() bool {
	return len(req.Capabilities) == 0 &&
		len(req.AnyOfCapabilities) == 0 &&
		req.MinMemlockBytes == 0 &&
		!req.RequiresRoot
}

// Deficit compares what this process actually holds against what a tier
// requires and returns one human-readable line per gap. An empty result
// means the tier's privilege requirements are fully met on this host
// right now (which says nothing about hardware or kernel support; that
// is Detect()'s job).
//
// euid is deliberately not an input: the kernel checks capability bits,
// not uids, and a uid-0 process that had its capabilities dropped (the
// normal container hardening) fails the exact same syscalls. A real root
// process carries the full effective set, so full-set membership is the
// precise root test.
func Deficit(have CapSet, r Rlimits, req TierRequirements) []string {
	var gaps []string

	for _, name := range req.Capabilities {
		if !have.Has(name) {
			gaps = append(gaps, fmt.Sprintf("missing required capability %s", name))
		}
	}

	if len(req.AnyOfCapabilities) > 0 {
		any := false
		for _, name := range req.AnyOfCapabilities {
			if have.Has(name) {
				any = true
				break
			}
		}
		if !any {
			gaps = append(gaps, fmt.Sprintf("missing all of %s (at least one required)",
				strings.Join(req.AnyOfCapabilities, ", ")))
		}
	}

	if req.MinMemlockBytes > 0 {
		switch {
		case !r.Supported:
			gaps = append(gaps, fmt.Sprintf(
				"RLIMIT_MEMLOCK could not be read on this host, cannot verify the required %d bytes of locked memory",
				req.MinMemlockBytes))
		case r.MemlockCur < req.MinMemlockBytes:
			gaps = append(gaps, fmt.Sprintf(
				"RLIMIT_MEMLOCK current limit %d bytes is below the required %d bytes (hard limit %d)",
				r.MemlockCur, req.MinMemlockBytes, r.MemlockMax))
		}
	}

	if req.RequiresRoot {
		full := FullCapSet()
		if have&full != full {
			missing := full.Count() - (have & full).Count()
			gaps = append(gaps, fmt.Sprintf(
				"needs root: effective capability set is missing %d of the %d capabilities a root process holds",
				missing, full.Count()))
		}
	}

	return gaps
}
