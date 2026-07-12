package pier

import (
	"encoding/json"
	"os"
	"runtime"
	"time"
)

// TierPreflight is the verdict for one tier: whether it detected as
// usable, what it guarantees, what it demands, and exactly which of those
// demands this process fails today.
type TierPreflight struct {
	Pier         string            `json:"pier"`
	Detected     bool              `json:"detected"`
	Guarantees   Guarantees        `json:"guarantees"`
	Requirements *TierRequirements `json:"requirements,omitempty"`
	Deficits     []string          `json:"deficits,omitempty"`
}

// PreflightReport explains, before any traffic moves, which tier the
// ladder will land on and why every higher tier was passed over. It pairs
// the ladder's own Detect() answers with the capability and rlimit audit,
// so "tier1 skipped" always comes with "because you are missing CAP_BPF
// and 8 MiB of memlock", not just a boolean.
type PreflightReport struct {
	GeneratedAt time.Time  `json:"generated_at"`
	Hostname    string     `json:"hostname"`
	GOOS        string     `json:"goos"`
	EUID        int        `json:"euid"`
	Caps        StatusCaps `json:"caps"`
	Rlimits     Rlimits    `json:"rlimits"`

	Tiers    []TierPreflight `json:"tiers"`
	Selected string          `json:"selected,omitempty"`
	Skips    []SkipReason    `json:"skips,omitempty"`
}

// Preflight audits the given piers in ladder order (highest privilege
// first, same order Select walks) against this process's actual
// capabilities and rlimits. Selection mirrors Select exactly: the first
// pier whose Detect() returns true wins; everything above it gets a skip
// reason.
func Preflight(piers []Pier) PreflightReport {
	hostname, _ := os.Hostname()

	r := PreflightReport{
		GeneratedAt: time.Now().UTC(),
		Hostname:    hostname,
		GOOS:        runtime.GOOS,
		EUID:        CurrentEUID(),
		Caps:        CurrentCaps(),
		Rlimits:     CurrentRlimits(),
	}

	for _, p := range piers {
		tp := TierPreflight{
			Pier:       p.Name(),
			Detected:   p.Detect(),
			Guarantees: p.Guarantees(),
		}
		if req, ok := RequirementsForTier(p.Name()); ok {
			tp.Requirements = &req
			if !req.Trivial() {
				tp.Deficits = Deficit(r.Caps.Effective, r.Rlimits, req)
			}
		}
		r.Tiers = append(r.Tiers, tp)

		if r.Selected == "" && tp.Detected {
			r.Selected = p.Name()
		}
	}

	// Skip reasons cover only the tiers above the selected one, matching
	// Select's report. Deficits are the sharper answer when we have them;
	// the guarantee description is the fallback for tiers with no
	// requirements entry.
	for _, tp := range r.Tiers {
		if tp.Pier == r.Selected {
			break
		}
		reason := "not available on this host right now (needs: " + tp.Guarantees.Description + ")"
		if len(tp.Deficits) > 0 {
			reason = tp.Deficits[0]
			for _, d := range tp.Deficits[1:] {
				reason += "; " + d
			}
		}
		r.Skips = append(r.Skips, SkipReason{Pier: tp.Pier, Reason: reason})
	}

	return r
}

// JSON renders the report as indented JSON, same convention as
// probe.Report.
func (r PreflightReport) JSON() ([]byte, error) {
	return json.MarshalIndent(r, "", "  ")
}
