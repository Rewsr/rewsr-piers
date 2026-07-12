package pier

import (
	"encoding/json"
	"os"
	"time"
)

// SelfTestReport is the full "can this host carry fabric traffic and how
// well" answer: the preflight (which tier and why) plus a real measured
// run through the selected tier, judged against that tier's own published
// Guarantees. Preflight says what should work; this says what did.
type SelfTestReport struct {
	GeneratedAt time.Time       `json:"generated_at"`
	Hostname    string          `json:"hostname"`
	Preflight   PreflightReport `json:"preflight"`

	// UDP carries the tier0 loopback benchmark. Tier0 is the only tier
	// that can be exercised end to end on any host without privileges,
	// so it is always the one measured; higher tiers currently detect
	// availability only.
	UDP      *UDPBenchResult `json:"udp,omitempty"`
	UDPError string          `json:"udp_error,omitempty"`

	// WithinLatencyBudget reports whether the measured average
	// round-trip latency stayed inside tier0's published LatencyBudgetUs.
	WithinLatencyBudget bool `json:"within_latency_budget"`

	// ThroughputVsClaimPct is the measured throughput as a percentage of
	// tier0's published MaxThroughputMbps. Loopback normally exceeds 100
	// since the published number is a conservative NIC-bound estimate;
	// well under 100 on an idle host means the claim needs revisiting.
	ThroughputVsClaimPct float64 `json:"throughput_vs_claim_pct"`
}

// SelfTestOptions sizes the measured leg. Zero values take the same
// defaults BenchmarkUDPLoopback applies.
type SelfTestOptions struct {
	PingCount    int
	BurstPackets int
	PacketSize   int
}

// RunSelfTest runs preflight over the real ladder and then measures
// tier0 for real. A benchmark failure is recorded in UDPError rather
// than failing the report; the preflight half is still the answer to
// "why did the ladder land here".
func RunSelfTest(opts SelfTestOptions) SelfTestReport {
	hostname, _ := os.Hostname()
	report := SelfTestReport{
		GeneratedAt: time.Now().UTC(),
		Hostname:    hostname,
		Preflight:   Preflight(Ladder()),
	}

	result, err := BenchmarkUDPLoopback(opts.PingCount, opts.BurstPackets, opts.PacketSize)
	if err != nil {
		report.UDPError = err.Error()
		return report
	}
	report.UDP = &result

	g := Tier0UDP{}.Guarantees()
	if g.LatencyBudgetUs > 0 {
		report.WithinLatencyBudget = result.AvgLatencyUs <= float64(g.LatencyBudgetUs)
	}
	if g.MaxThroughputMbps > 0 {
		report.ThroughputVsClaimPct = result.ThroughputMbps / float64(g.MaxThroughputMbps) * 100
	}

	return report
}

// JSON renders the report as indented JSON, same convention as the other
// reports in this repo.
func (r SelfTestReport) JSON() ([]byte, error) {
	return json.MarshalIndent(r, "", "  ")
}
