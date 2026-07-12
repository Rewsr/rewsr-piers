package pier

import (
	"encoding/json"
	"slices"
	"strings"
	"testing"
)

// procSelfStatus is the relevant slice of a real /proc/self/status for an
// unprivileged process that was granted CAP_NET_RAW and CAP_BPF via file
// capabilities, bounding set intact.
const procSelfStatus = `Name:	piers
Umask:	0022
State:	R (running)
Tgid:	4242
Ngid:	0
Pid:	4242
PPid:	4100
TracerPid:	0
Uid:	1000	1000	1000	1000
Gid:	1000	1000	1000	1000
FDSize:	64
Groups:	4 24 27 30 46 1000
NStgid:	4242
NSpid:	4242
NSpgid:	4242
NSsid:	4100
VmPeak:	   12345 kB
CapInh:	0000000000000000
CapPrm:	0000008000002000
CapEff:	0000008000002000
CapBnd:	000001ffffffffff
CapAmb:	0000000000000000
NoNewPrivs:	0
Seccomp:	0
Seccomp_filters:	0
Speculation_Store_Bypass:	thread vulnerable
Cpus_allowed:	ffff
Mems_allowed:	00000001
voluntary_ctxt_switches:	100
nonvoluntary_ctxt_switches:	10
`

func TestParseStatusCaps(t *testing.T) {
	sc, err := ParseStatusCaps([]byte(procSelfStatus))
	if err != nil {
		t.Fatalf("ParseStatusCaps: %v", err)
	}
	if !sc.Supported {
		t.Fatal("Supported must be true after a successful parse")
	}

	// 0x2000 is bit 13 (CAP_NET_RAW), 0x8000000000 is bit 39 (CAP_BPF).
	if !sc.Effective.Has("CAP_NET_RAW") || !sc.Effective.Has("CAP_BPF") {
		t.Errorf("effective = %s (%v)", sc.Effective, sc.Effective.Names())
	}
	if sc.Effective.Has("CAP_NET_ADMIN") || sc.Effective.Has("CAP_SYS_ADMIN") {
		t.Errorf("effective should not include admin caps: %v", sc.Effective.Names())
	}
	if sc.Effective.Count() != 2 {
		t.Errorf("effective count = %d, want 2", sc.Effective.Count())
	}
	if sc.Bounding != FullCapSet() {
		t.Errorf("bounding = %s, want full set %s", sc.Bounding, FullCapSet())
	}
}

func TestParseStatusCapsMissingLines(t *testing.T) {
	_, err := ParseStatusCaps([]byte("Name:\tpiers\nCapEff:\t0000000000000000\n"))
	if err == nil {
		t.Fatal("expected error when CapPrm and CapBnd are absent")
	}
	if !strings.Contains(err.Error(), "CapPrm") || !strings.Contains(err.Error(), "CapBnd") {
		t.Errorf("error should name the missing lines: %v", err)
	}
}

func TestParseCapMask(t *testing.T) {
	full, err := ParseCapMask("000001ffffffffff")
	if err != nil {
		t.Fatalf("ParseCapMask: %v", err)
	}
	if full != FullCapSet() {
		t.Errorf("root mask = %s, want %s", full, FullCapSet())
	}

	if _, err := ParseCapMask(""); err == nil {
		t.Error("empty mask must error")
	}
	if _, err := ParseCapMask("zzzz"); err == nil {
		t.Error("non-hex mask must error")
	}
}

func TestCapSetOfAndNames(t *testing.T) {
	set, err := CapSetOf("CAP_NET_RAW", "CAP_NET_ADMIN")
	if err != nil {
		t.Fatalf("CapSetOf: %v", err)
	}
	want := []string{"CAP_NET_ADMIN", "CAP_NET_RAW"}
	if got := set.Names(); !slices.Equal(got, want) {
		t.Errorf("names = %v, want %v", got, want)
	}

	if _, err := CapSetOf("CAP_TYPO"); err == nil {
		t.Error("unknown capability name must error")
	}
}

func TestCapSetJSONRoundTrip(t *testing.T) {
	set, _ := CapSetOf("CAP_BPF")
	data, err := json.Marshal(set)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if string(data) != `"0000008000000000"` {
		t.Errorf("marshaled = %s", data)
	}
	var back CapSet
	if err := json.Unmarshal(data, &back); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if back != set {
		t.Errorf("round trip = %s, want %s", back, set)
	}
}

func TestDeficitTier1(t *testing.T) {
	req, ok := RequirementsForTier(Tier1AFXDP{}.Name())
	if !ok {
		t.Fatal("tier1 requirements missing from table")
	}

	limits := Rlimits{Supported: true, MemlockCur: 64 << 20, MemlockMax: 64 << 20, NofileCur: 1024, NofileMax: 1024}

	t.Run("fully satisfied", func(t *testing.T) {
		have, _ := CapSetOf("CAP_NET_RAW", "CAP_BPF")
		if gaps := Deficit(have, limits, req); len(gaps) != 0 {
			t.Errorf("gaps = %v", gaps)
		}
	})

	t.Run("sys_admin satisfies the any-of clause", func(t *testing.T) {
		have, _ := CapSetOf("CAP_NET_RAW", "CAP_SYS_ADMIN")
		if gaps := Deficit(have, limits, req); len(gaps) != 0 {
			t.Errorf("gaps = %v", gaps)
		}
	})

	t.Run("missing net_raw", func(t *testing.T) {
		have, _ := CapSetOf("CAP_BPF")
		gaps := Deficit(have, limits, req)
		if len(gaps) != 1 || !strings.Contains(gaps[0], "CAP_NET_RAW") {
			t.Errorf("gaps = %v", gaps)
		}
	})

	t.Run("missing both bpf alternatives", func(t *testing.T) {
		have, _ := CapSetOf("CAP_NET_RAW")
		gaps := Deficit(have, limits, req)
		if len(gaps) != 1 || !strings.Contains(gaps[0], "CAP_BPF") || !strings.Contains(gaps[0], "CAP_SYS_ADMIN") {
			t.Errorf("gaps = %v", gaps)
		}
	})

	t.Run("memlock too low", func(t *testing.T) {
		have, _ := CapSetOf("CAP_NET_RAW", "CAP_BPF")
		low := Rlimits{Supported: true, MemlockCur: 8 << 20, MemlockMax: 8 << 20}
		gaps := Deficit(have, low, req)
		if len(gaps) != 1 || !strings.Contains(gaps[0], "RLIMIT_MEMLOCK") {
			t.Errorf("gaps = %v", gaps)
		}
	})

	t.Run("rlimits unreadable", func(t *testing.T) {
		have, _ := CapSetOf("CAP_NET_RAW", "CAP_BPF")
		gaps := Deficit(have, Rlimits{}, req)
		if len(gaps) != 1 || !strings.Contains(gaps[0], "could not be read") {
			t.Errorf("gaps = %v", gaps)
		}
	})
}

func TestDeficitTier2NeedsRoot(t *testing.T) {
	req, ok := RequirementsForTier(Tier2Privileged{}.Name())
	if !ok {
		t.Fatal("tier2 requirements missing from table")
	}
	limits := Rlimits{Supported: true}

	have, _ := CapSetOf("CAP_NET_ADMIN")
	gaps := Deficit(have, limits, req)
	if len(gaps) != 1 || !strings.Contains(gaps[0], "needs root") {
		t.Errorf("gaps = %v", gaps)
	}

	if gaps := Deficit(FullCapSet(), limits, req); len(gaps) != 0 {
		t.Errorf("full set should satisfy tier2: %v", gaps)
	}
}

func TestDeficitTier0Trivial(t *testing.T) {
	req, ok := RequirementsForTier(Tier0UDP{}.Name())
	if !ok {
		t.Fatal("tier0 requirements missing from table")
	}
	if !req.Trivial() {
		t.Error("tier0 requirements must be trivial")
	}
	if gaps := Deficit(0, Rlimits{}, req); len(gaps) != 0 {
		t.Errorf("tier0 can never have gaps: %v", gaps)
	}
}
