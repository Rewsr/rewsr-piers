package pier

import "fmt"

// Ladder returns every known pier, ordered from the highest-privilege,
// highest-performance tier down to the permissionless base tier. Select
// walks this list top to bottom.
func Ladder() []Pier {
	return []Pier{
		Tier2Privileged{},
		Tier1AFXDP{},
		Tier0UDP{},
	}
}

// SkipReason explains why one specific tier was not selected on this
// host, right now.
type SkipReason struct {
	Pier   string `json:"pier"`
	Reason string `json:"reason"`
}

// Select walks the ladder from highest privilege to lowest and returns
// the first pier that Detect()s as usable on this host right now, along
// with a truth report of why every higher tier was skipped. This is a
// report of what is actually available on this machine, not what is
// theoretically possible in general.
//
// Tier0 always detects true, so Select never returns a nil Pier in
// practice. If it somehow did, callers must treat that as "nothing
// usable" rather than dereference a nil Pier.
func Select() (Pier, []SkipReason) {
	var skips []SkipReason
	for _, p := range Ladder() {
		if p.Detect() {
			return p, skips
		}
		g := p.Guarantees()
		skips = append(skips, SkipReason{
			Pier:   p.Name(),
			Reason: fmt.Sprintf("not available on this host right now (needs: %s)", g.Description),
		})
	}
	return nil, skips
}
