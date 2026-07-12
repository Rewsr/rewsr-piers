package probe

import (
	"bytes"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// XDPReport answers "can this host run the AF_XDP tier natively" with the
// evidence attached: kernel version, the kernel config switches that gate
// XDP sockets, and, when bpftool is installed, the kernel's own runtime
// answer for which program types load.
type XDPReport struct {
	KernelRelease     string            `json:"kernel_release,omitempty"`
	KernelMajor       int               `json:"kernel_major,omitempty"`
	KernelMinor       int               `json:"kernel_minor,omitempty"`
	KernelSupportsXDP bool              `json:"kernel_supports_xdp"`
	ConfigSource      string            `json:"config_source,omitempty"`
	ConfigFlags       map[string]string `json:"config_flags,omitempty"`
	BpftoolChecked    bool              `json:"bpftool_checked"`
	ProgramTypes      map[string]bool   `json:"program_types,omitempty"`
	Notes             []string          `json:"notes,omitempty"`
}

// xdpConfigKeys are the kernel config switches that decide whether AF_XDP
// exists at all and whether BPF programs can be loaded to drive it.
var xdpConfigKeys = []string{
	"CONFIG_XDP_SOCKETS",
	"CONFIG_BPF",
	"CONFIG_BPF_SYSCALL",
	"CONFIG_DEBUG_INFO_BTF",
}

// ParseKernelRelease extracts major and minor from a kernel release
// string like "6.8.0-41-generic".
func ParseKernelRelease(release string) (major, minor int, ok bool) {
	parts := strings.SplitN(strings.TrimSpace(release), ".", 3)
	if len(parts) < 2 {
		return 0, 0, false
	}
	major, errA := strconv.Atoi(parts[0])
	// The minor component can carry a suffix in exotic builds; take the
	// leading digits only.
	minorStr := parts[1]
	for i, r := range minorStr {
		if r < '0' || r > '9' {
			minorStr = minorStr[:i]
			break
		}
	}
	minor, errB := strconv.Atoi(minorStr)
	if errA != nil || errB != nil {
		return 0, 0, false
	}
	return major, minor, true
}

// ParseKernelConfig extracts the requested CONFIG_ keys from a kernel
// config, accepting both the plain text of /boot/config-<release> and the
// gzipped /proc/config.gz (detected by the 0x1f 0x8b magic). Values are
// "y", "m", the literal value, or "not set" for commented-out entries.
func ParseKernelConfig(data []byte, keys []string) (map[string]string, error) {
	if len(data) >= 2 && data[0] == 0x1f && data[1] == 0x8b {
		zr, err := gzip.NewReader(bytes.NewReader(data))
		if err != nil {
			return nil, err
		}
		defer zr.Close()
		plain, err := io.ReadAll(zr)
		if err != nil {
			return nil, err
		}
		data = plain
	}

	want := make(map[string]bool, len(keys))
	for _, k := range keys {
		want[k] = true
	}

	out := map[string]string{}
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "#") {
			// "# CONFIG_FOO is not set"
			trimmed := strings.TrimSpace(strings.TrimPrefix(line, "#"))
			key, _, found := strings.Cut(trimmed, " ")
			if found && want[key] && strings.HasSuffix(trimmed, "is not set") {
				out[key] = "not set"
			}
			continue
		}
		key, value, found := strings.Cut(line, "=")
		if found && want[key] {
			out[key] = value
		}
	}
	return out, nil
}

// ParseBpftoolFeatures pulls the program-type availability lines out of
// `bpftool feature probe` output, which look like:
//
//	eBPF program_type xdp is available
//	eBPF program_type sched_cls is available
//	eBPF program_type lirc_mode2 is NOT available
func ParseBpftoolFeatures(out []byte) map[string]bool {
	types := map[string]bool{}
	for _, line := range strings.Split(string(out), "\n") {
		line = strings.TrimSpace(line)
		rest, found := strings.CutPrefix(line, "eBPF program_type ")
		if !found {
			continue
		}
		name, verdict, found := strings.Cut(rest, " is ")
		if !found {
			continue
		}
		types[name] = strings.HasPrefix(verdict, "available")
	}
	return types
}

// XDPProbe assembles the XDP support report. root is prepended to every
// file path so tests run against a fixture tree; pass "/" for the real
// host. Missing sources become Notes entries rather than failures, in the
// same spirit as runTool.
func XDPProbe(root string) XDPReport {
	r := XDPReport{}

	release, err := os.ReadFile(filepath.Join(root, "proc/sys/kernel/osrelease"))
	if err != nil {
		r.Notes = append(r.Notes, "kernel release unreadable: "+err.Error())
	} else {
		r.KernelRelease = strings.TrimSpace(string(release))
		if major, minor, ok := ParseKernelRelease(r.KernelRelease); ok {
			r.KernelMajor, r.KernelMinor = major, minor
			// AF_XDP sockets landed in 4.18.
			r.KernelSupportsXDP = major > 4 || (major == 4 && minor >= 18)
		}
	}

	for _, candidate := range []string{
		"proc/config.gz",
		"boot/config-" + r.KernelRelease,
	} {
		if r.KernelRelease == "" && strings.HasSuffix(candidate, "config-") {
			continue
		}
		data, err := os.ReadFile(filepath.Join(root, candidate))
		if err != nil {
			continue
		}
		flags, err := ParseKernelConfig(data, xdpConfigKeys)
		if err != nil {
			r.Notes = append(r.Notes, candidate+": "+err.Error())
			continue
		}
		r.ConfigSource = "/" + candidate
		r.ConfigFlags = flags
		break
	}
	if r.ConfigSource == "" {
		r.Notes = append(r.Notes, "no readable kernel config (checked /proc/config.gz and /boot)")
	}

	if toolAvailable("bpftool") {
		res := runTool("bpftool", "feature", "probe")
		if res.Error == "" {
			r.BpftoolChecked = true
			r.ProgramTypes = ParseBpftoolFeatures([]byte(res.Output))
		} else {
			r.Notes = append(r.Notes, "bpftool feature probe failed: "+res.Error)
		}
	} else {
		r.Notes = append(r.Notes, "bpftool not on PATH, runtime probe skipped")
	}

	return r
}
