package probe

import (
	"os"
	"path/filepath"
	"strings"
	"time"
)

// TimersReport describes the timing fidelity of this host: whether the
// TSC is usable as a stable clock, which clocksource the kernel actually
// selected, whether busy polling is enabled, and the observed granularity
// of the Go monotonic clock. Latency numbers from a host with an unstable
// clocksource are noise, so the ladder's reports carry this alongside the
// tier decision.
type TimersReport struct {
	TSCFlags              []string `json:"tsc_flags,omitempty"`
	CurrentClocksource    string   `json:"current_clocksource,omitempty"`
	AvailableClocksources []string `json:"available_clocksources,omitempty"`
	BusyPoll              string   `json:"busy_poll,omitempty"`
	BusyRead              string   `json:"busy_read,omitempty"`
	MeasuredGranularityNs int64    `json:"measured_granularity_ns"`
	Notes                 []string `json:"notes,omitempty"`
}

// tscFlagNames are the /proc/cpuinfo flags that certify the TSC as a
// trustworthy time base.
var tscFlagNames = []string{
	"constant_tsc",
	"nonstop_tsc",
	"tsc_deadline_timer",
	"rdtscp",
	"tsc_known_freq",
}

// ParseCpuinfoTSCFlags extracts the TSC-related flags from the first
// flags line of /proc/cpuinfo.
func ParseCpuinfoTSCFlags(data []byte) []string {
	for _, line := range strings.Split(string(data), "\n") {
		name, value, ok := strings.Cut(line, ":")
		if !ok || strings.TrimSpace(name) != "flags" {
			continue
		}
		present := map[string]bool{}
		for _, f := range strings.Fields(value) {
			present[f] = true
		}
		var out []string
		for _, f := range tscFlagNames {
			if present[f] {
				out = append(out, f)
			}
		}
		return out
	}
	return nil
}

// MeasureClockGranularityNs samples the monotonic clock in a tight loop
// and returns the smallest positive delta observed, an upper bound on the
// clock's real granularity. The loop is bounded and sleep-free, so this
// costs microseconds, not milliseconds.
func MeasureClockGranularityNs(samples int) int64 {
	if samples <= 0 {
		samples = 4096
	}
	best := int64(0)
	prev := time.Now()
	for i := 0; i < samples; i++ {
		now := time.Now()
		d := now.Sub(prev).Nanoseconds()
		if d > 0 && (best == 0 || d < best) {
			best = d
		}
		prev = now
	}
	return best
}

// TimersProbe assembles the timing report. root is prepended to every
// file path for tests.
func TimersProbe(root string) TimersReport {
	r := TimersReport{}

	if data, err := os.ReadFile(filepath.Join(root, "proc/cpuinfo")); err == nil {
		r.TSCFlags = ParseCpuinfoTSCFlags(data)
	} else {
		r.Notes = append(r.Notes, "proc/cpuinfo unreadable: "+err.Error())
	}

	csDir := filepath.Join(root, "sys/devices/system/clocksource/clocksource0")
	if data, err := os.ReadFile(filepath.Join(csDir, "current_clocksource")); err == nil {
		r.CurrentClocksource = strings.TrimSpace(string(data))
	} else {
		r.Notes = append(r.Notes, "current_clocksource unreadable: "+err.Error())
	}
	if data, err := os.ReadFile(filepath.Join(csDir, "available_clocksource")); err == nil {
		r.AvailableClocksources = strings.Fields(string(data))
	}

	if data, err := os.ReadFile(filepath.Join(root, "proc/sys/net/core/busy_poll")); err == nil {
		r.BusyPoll = strings.TrimSpace(string(data))
	}
	if data, err := os.ReadFile(filepath.Join(root, "proc/sys/net/core/busy_read")); err == nil {
		r.BusyRead = strings.TrimSpace(string(data))
	}

	r.MeasuredGranularityNs = MeasureClockGranularityNs(4096)

	return r
}
