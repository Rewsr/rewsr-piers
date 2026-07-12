package probe

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// IfaceTuning is the per-interface tuning state that shapes what a fabric
// data plane gets out of the NIC: MTU (jumbo frames or not), the tx queue
// length, how many hardware queues the driver exposed, and whether the
// link is even up.
type IfaceTuning struct {
	Name       string `json:"name"`
	MTU        int    `json:"mtu"`
	TxQueueLen int    `json:"tx_queue_len"`
	OperState  string `json:"oper_state,omitempty"`
	RxQueues   int    `json:"rx_queues,omitempty"`
	TxQueues   int    `json:"tx_queues,omitempty"`
}

// NetTuningReport describes interface tuning plus the qdisc layout. The
// queueing discipline decides how much latency the kernel adds under
// load, which is why "which qdisc is on the fabric NIC" is a probe and
// not an assumption.
type NetTuningReport struct {
	Interfaces  []IfaceTuning       `json:"interfaces,omitempty"`
	Qdiscs      map[string][]string `json:"qdiscs,omitempty"`
	JumboFrames bool                `json:"jumbo_frames"`
	Notes       []string            `json:"notes,omitempty"`
}

// ParseTcQdisc parses `tc qdisc show` output into device -> qdisc kinds.
// Lines look like:
//
//	qdisc noqueue 0: dev lo root refcnt 2
//	qdisc mq 0: dev ens1f0np0 root
//	qdisc fq_codel 0: dev ens1f0np0 parent :4 limit 10240p flows 1024
//
// A multi-queue device repeats its child qdisc once per hardware queue;
// kinds are deduplicated per device so the answer stays readable.
func ParseTcQdisc(out []byte) map[string][]string {
	result := map[string][]string{}
	seen := map[string]map[string]bool{}

	for _, line := range strings.Split(string(out), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 5 || fields[0] != "qdisc" {
			continue
		}
		kind := fields[1]
		var dev string
		for i, f := range fields {
			if f == "dev" && i+1 < len(fields) {
				dev = fields[i+1]
				break
			}
		}
		if dev == "" {
			continue
		}
		if seen[dev] == nil {
			seen[dev] = map[string]bool{}
		}
		if seen[dev][kind] {
			continue
		}
		seen[dev][kind] = true
		result[dev] = append(result[dev], kind)
	}
	return result
}

// NetTuningProbe assembles the report from /sys/class/net (root-prefixed
// for tests) and, when the tc tool exists, the qdisc layout.
func NetTuningProbe(root string) NetTuningReport {
	r := NetTuningReport{}

	base := filepath.Join(root, "sys/class/net")
	entries, err := os.ReadDir(base)
	if err != nil {
		r.Notes = append(r.Notes, "sys/class/net unreadable: "+err.Error())
	}
	for _, e := range entries {
		name := e.Name()
		dir := filepath.Join(base, name)

		iface := IfaceTuning{
			Name:       name,
			MTU:        readIntFile(filepath.Join(dir, "mtu")),
			TxQueueLen: readIntFile(filepath.Join(dir, "tx_queue_len")),
		}
		if data, err := os.ReadFile(filepath.Join(dir, "operstate")); err == nil {
			iface.OperState = strings.TrimSpace(string(data))
		}
		if queues, err := os.ReadDir(filepath.Join(dir, "queues")); err == nil {
			for _, q := range queues {
				switch {
				case strings.HasPrefix(q.Name(), "rx-"):
					iface.RxQueues++
				case strings.HasPrefix(q.Name(), "tx-"):
					iface.TxQueues++
				}
			}
		}
		r.Interfaces = append(r.Interfaces, iface)

		if name != "lo" && iface.MTU >= 9000 {
			r.JumboFrames = true
		}
	}

	if toolAvailable("tc") {
		res := runTool("tc", "qdisc", "show")
		if res.Error == "" {
			r.Qdiscs = ParseTcQdisc([]byte(res.Output))
		} else {
			r.Notes = append(r.Notes, "tc qdisc show failed: "+res.Error)
		}
	} else {
		r.Notes = append(r.Notes, "tc not on PATH, qdisc layout skipped")
	}

	return r
}

// readIntFile returns the integer contents of a sysfs file, or 0 when the
// file is missing or non-numeric.
func readIntFile(path string) int {
	data, err := os.ReadFile(path)
	if err != nil {
		return 0
	}
	n, err := strconv.Atoi(strings.TrimSpace(string(data)))
	if err != nil {
		return 0
	}
	return n
}
