package pier

import (
	"encoding/json"
	"strings"
	"testing"
)

// fakePier is a scriptable Pier for preflight tests.
type fakePier struct {
	name   string
	detect bool
	g      Guarantees
}

func (f fakePier) Name() string           { return f.name }
func (f fakePier) Detect() bool           { return f.detect }
func (f fakePier) Guarantees() Guarantees { return f.g }

func TestPreflightSelectsFirstDetected(t *testing.T) {
	piers := []Pier{
		fakePier{name: "tier2-privileged", detect: false, g: Guarantees{Description: "root + CAP_NET_ADMIN"}},
		fakePier{name: "tier1-afxdp", detect: false, g: Guarantees{Description: "AF_XDP support"}},
		fakePier{name: "tier0-udp", detect: true, g: Guarantees{Description: "any host"}},
	}

	r := Preflight(piers)

	if r.Selected != "tier0-udp" {
		t.Errorf("selected = %q", r.Selected)
	}
	if len(r.Tiers) != 3 {
		t.Fatalf("tiers = %+v", r.Tiers)
	}
	if len(r.Skips) != 2 {
		t.Fatalf("skips = %+v", r.Skips)
	}
	if r.Skips[0].Pier != "tier2-privileged" || r.Skips[1].Pier != "tier1-afxdp" {
		t.Errorf("skip order = %+v", r.Skips)
	}
	if r.GOOS == "" || r.GeneratedAt.IsZero() {
		t.Error("report metadata not populated")
	}
}

func TestPreflightTopTierWinsNoSkips(t *testing.T) {
	piers := []Pier{
		fakePier{name: "tier2-privileged", detect: true},
		fakePier{name: "tier0-udp", detect: true},
	}
	r := Preflight(piers)
	if r.Selected != "tier2-privileged" {
		t.Errorf("selected = %q", r.Selected)
	}
	if len(r.Skips) != 0 {
		t.Errorf("skips = %+v", r.Skips)
	}
}

func TestPreflightNothingDetected(t *testing.T) {
	piers := []Pier{
		fakePier{name: "tier2-privileged", detect: false, g: Guarantees{Description: "root"}},
	}
	r := Preflight(piers)
	if r.Selected != "" {
		t.Errorf("selected = %q, want empty", r.Selected)
	}
	// With nothing selected the loop never breaks, so every tier gets a
	// skip entry; that is the honest report for a host where even the
	// base tier failed.
	if len(r.Skips) != 1 {
		t.Errorf("skips = %+v", r.Skips)
	}
}

func TestPreflightUsesRealTierRequirements(t *testing.T) {
	// The real tier names hit the requirements table, so the report must
	// carry requirements and, on hosts without those privileges, concrete
	// deficits rather than only the guarantee blurb.
	piers := []Pier{
		fakePier{name: Tier1AFXDP{}.Name(), detect: false, g: Guarantees{Description: "AF_XDP"}},
		fakePier{name: Tier0UDP{}.Name(), detect: true},
	}
	r := Preflight(piers)

	tier1 := r.Tiers[0]
	if tier1.Requirements == nil {
		t.Fatal("tier1 requirements missing")
	}
	if !r.Caps.Supported && len(tier1.Deficits) == 0 {
		t.Error("on a host without capability support the tier1 deficits must be non-empty")
	}

	tier0 := r.Tiers[1]
	if tier0.Requirements == nil || !tier0.Requirements.Trivial() {
		t.Errorf("tier0 requirements = %+v", tier0.Requirements)
	}
	if len(tier0.Deficits) != 0 {
		t.Errorf("tier0 deficits = %v", tier0.Deficits)
	}
}

func TestPreflightJSON(t *testing.T) {
	r := Preflight([]Pier{fakePier{name: "tier0-udp", detect: true}})
	data, err := r.JSON()
	if err != nil {
		t.Fatalf("JSON: %v", err)
	}
	if !strings.Contains(string(data), `"selected": "tier0-udp"`) {
		t.Errorf("json = %s", data)
	}
	var back PreflightReport
	if err := json.Unmarshal(data, &back); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if back.Selected != "tier0-udp" {
		t.Errorf("round trip selected = %q", back.Selected)
	}
}
