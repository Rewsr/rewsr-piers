package probe

import (
	"encoding/json"
	"os"
	"runtime"
	"time"
)

// DeepReport is the full fabric-depth audit: the XDP, RDMA, hugepage, and
// timer probes assembled next to the same host identity header the base
// Report carries. Where the base audit answers "which tools exist here",
// this one answers "what will the data plane actually get".
type DeepReport struct {
	GeneratedAt time.Time `json:"generated_at"`
	Hostname    string    `json:"hostname"`
	GOOS        string    `json:"goos"`
	NumCPU      int       `json:"num_cpu"`

	XDP       XDPReport       `json:"xdp"`
	RDMA      RDMAReport      `json:"rdma"`
	Hugepages HugepagesReport `json:"hugepages"`
	Timers    TimersReport    `json:"timers"`
	Net       NetTuningReport `json:"net"`
}

// GatherDeep runs every deep probe against root ("/" for the real host).
// Like Run, it never fails: missing subsystems land in each section's
// Notes.
func GatherDeep(root string) DeepReport {
	hostname, _ := os.Hostname()
	return DeepReport{
		GeneratedAt: time.Now().UTC(),
		Hostname:    hostname,
		GOOS:        runtime.GOOS,
		NumCPU:      runtime.NumCPU(),
		XDP:         XDPProbe(root),
		RDMA:        RDMAProbe(root),
		Hugepages:   HugepagesProbe(root),
		Timers:      TimersProbe(root),
		Net:         NetTuningProbe(root),
	}
}

// JSON renders the report as indented JSON, same convention as Report.
func (r DeepReport) JSON() ([]byte, error) {
	return json.MarshalIndent(r, "", "  ")
}
