// Package probe implements a fabric-independence audit: it gathers real
// host facts about the network and compute fabric underneath this
// machine, so the choices the pier ladder makes can be explained. It
// shells out to a handful of standard inspection tools when they are
// present, and skips them gracefully when they are not, since this must
// also run cleanly on a macOS dev machine that has none of the
// Linux-native tools installed.
package probe

import (
	"encoding/json"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"time"
)

// ToolResult captures the outcome of trying to run one inspection tool.
type ToolResult struct {
	Tool      string   `json:"tool"`
	Args      []string `json:"args,omitempty"`
	Available bool     `json:"available"`
	Output    string   `json:"output,omitempty"`
	Error     string   `json:"error,omitempty"`
}

// Report is the full fabric-independence audit for this host.
type Report struct {
	GeneratedAt time.Time    `json:"generated_at"`
	Hostname    string       `json:"hostname"`
	GOOS        string       `json:"goos"`
	GOARCH      string       `json:"goarch"`
	NumCPU      int          `json:"num_cpu"`
	Tools       []ToolResult `json:"tools"`
}

// Run performs the audit and returns the assembled report. It never
// returns an error: a missing or failing tool is recorded in the report
// as such, not surfaced as a fatal condition.
func Run() Report {
	hostname, _ := os.Hostname()

	r := Report{
		GeneratedAt: time.Now().UTC(),
		Hostname:    hostname,
		GOOS:        runtime.GOOS,
		GOARCH:      runtime.GOARCH,
		NumCPU:      runtime.NumCPU(),
	}

	r.Tools = append(r.Tools, runTool("uname", "-a"))
	r.Tools = append(r.Tools, runTool("nproc"))
	r.Tools = append(r.Tools, runTool("lspci", "-nn"))

	ipLink := runTool("ip", "-o", "link", "show")
	r.Tools = append(r.Tools, ipLink)
	r.Tools = append(r.Tools, ethtoolResult(ipLink))

	return r
}

// ethtoolResult runs `ethtool -i <iface>` against the first non-loopback
// interface reported by `ip -o link show`. ethtool needs an interface
// argument to do anything useful, so if ip link is unavailable or
// reports no usable interface, this is recorded as a graceful skip
// rather than run with no arguments.
func ethtoolResult(ipLink ToolResult) ToolResult {
	if !ipLink.Available {
		return ToolResult{
			Tool:      "ethtool",
			Available: toolAvailable("ethtool"),
			Error:     "skipped: ip is not on PATH, no interface to inspect",
		}
	}
	if ipLink.Error != "" {
		return ToolResult{
			Tool:      "ethtool",
			Available: toolAvailable("ethtool"),
			Error:     "skipped: ip link show failed, no interface to inspect",
		}
	}

	iface := firstNonLoInterface(ipLink.Output)
	if iface == "" {
		return ToolResult{
			Tool:      "ethtool",
			Available: toolAvailable("ethtool"),
			Error:     "skipped: no non-loopback interface found in ip link output",
		}
	}

	return runTool("ethtool", "-i", iface)
}

// toolAvailable reports whether name is on PATH.
func toolAvailable(name string) bool {
	_, err := exec.LookPath(name)
	return err == nil
}

// runTool runs name with args if it is on PATH. If it is not, it returns
// a ToolResult marked unavailable instead of failing: this audit must
// run cleanly on hosts, like a macOS dev machine, that lack most of
// these Linux-native tools.
func runTool(name string, args ...string) ToolResult {
	res := ToolResult{Tool: name, Args: args}

	path, err := exec.LookPath(name)
	if err != nil {
		res.Available = false
		res.Error = "not found on PATH"
		return res
	}
	res.Available = true

	cmd := exec.Command(path, args...)
	out, err := cmd.CombinedOutput()
	res.Output = strings.TrimSpace(string(out))
	if err != nil {
		res.Error = err.Error()
	}
	return res
}

// firstNonLoInterface picks the first non-loopback interface name out of
// `ip -o link show` output, one interface per line in the form
// "<index>: <name>: <flags and details>".
func firstNonLoInterface(ipLinkOutput string) string {
	for _, line := range strings.Split(ipLinkOutput, "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		parts := strings.SplitN(line, ": ", 3)
		if len(parts) < 2 {
			continue
		}
		name := strings.TrimSpace(parts[1])
		if name == "" || name == "lo" {
			continue
		}
		return name
	}
	return ""
}

// JSON renders the report as indented JSON.
func (r Report) JSON() ([]byte, error) {
	return json.MarshalIndent(r, "", "  ")
}
