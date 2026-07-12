// Package pier defines the permissionless-to-privileged deployment tier
// ladder. Tier 0 works on any host with no special privileges. Higher tiers
// are auto-detected and only used when the current host genuinely supports
// them, and they fall back cleanly rather than crash when unavailable.
package pier

// Pier is one rung on the tier ladder. Each pier knows its own name, can
// detect whether it is usable on the current host right now, and can
// describe what it guarantees when it is used.
type Pier interface {
	// Name is a short stable identifier for this tier, e.g. "tier0-udp".
	Name() string

	// Detect reports whether this tier is actually usable on the current
	// host right now. This must be a real check, not a guess: missing
	// kernel features, missing privileges, or missing hardware must all
	// result in false, never a panic or an error.
	Detect() bool

	// Guarantees describes what this tier promises when selected.
	Guarantees() Guarantees
}

// Guarantees describes the operating requirements and expected performance
// envelope of a pier.
type Guarantees struct {
	RequiresRoot         bool
	RequiresKernelModule bool
	MaxThroughputMbps    int
	LatencyBudgetUs      int
	Description          string
}
