package probe

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// HugepageSizeInfo is one configured hugepage size and its pool state.
type HugepageSizeInfo struct {
	SizeKB int `json:"size_kb"`
	Nr     int `json:"nr"`
	Free   int `json:"free"`
}

// HugepagesReport describes the host's hugepage pools. The AF_XDP and
// privileged tiers both want locked, contiguous memory; an empty pool
// here is the most common reason a "supported" host still cannot climb
// the ladder.
type HugepagesReport struct {
	TotalPages    int                `json:"total_pages"`
	FreePages     int                `json:"free_pages"`
	DefaultSizeKB int                `json:"default_size_kb,omitempty"`
	Sizes         []HugepageSizeInfo `json:"sizes,omitempty"`
	Notes         []string           `json:"notes,omitempty"`
}

// ParseMeminfoHugepages pulls the hugepage counters out of /proc/meminfo:
//
//	HugePages_Total:    1024
//	HugePages_Free:      512
//	Hugepagesize:       2048 kB
func ParseMeminfoHugepages(data []byte) (total, free, defaultSizeKB int) {
	for _, line := range strings.Split(string(data), "\n") {
		name, value, ok := strings.Cut(line, ":")
		if !ok {
			continue
		}
		fields := strings.Fields(value)
		if len(fields) == 0 {
			continue
		}
		n, err := strconv.Atoi(fields[0])
		if err != nil {
			continue
		}
		switch strings.TrimSpace(name) {
		case "HugePages_Total":
			total = n
		case "HugePages_Free":
			free = n
		case "Hugepagesize":
			defaultSizeKB = n
		}
	}
	return total, free, defaultSizeKB
}

// HugepagesProbe assembles the hugepage report from /proc/meminfo and the
// per-size pools under /sys/kernel/mm/hugepages. root is prepended for
// tests.
func HugepagesProbe(root string) HugepagesReport {
	r := HugepagesReport{}

	if data, err := os.ReadFile(filepath.Join(root, "proc/meminfo")); err == nil {
		r.TotalPages, r.FreePages, r.DefaultSizeKB = ParseMeminfoHugepages(data)
	} else {
		r.Notes = append(r.Notes, "proc/meminfo unreadable: "+err.Error())
	}

	base := filepath.Join(root, "sys/kernel/mm/hugepages")
	entries, err := os.ReadDir(base)
	if err != nil {
		r.Notes = append(r.Notes, "sys/kernel/mm/hugepages unreadable: "+err.Error())
		return r
	}
	for _, e := range entries {
		name := e.Name()
		if !strings.HasPrefix(name, "hugepages-") || !strings.HasSuffix(name, "kB") {
			continue
		}
		kb, err := strconv.Atoi(strings.TrimSuffix(strings.TrimPrefix(name, "hugepages-"), "kB"))
		if err != nil {
			continue
		}
		info := HugepageSizeInfo{SizeKB: kb}
		if data, err := os.ReadFile(filepath.Join(base, name, "nr_hugepages")); err == nil {
			info.Nr, _ = strconv.Atoi(strings.TrimSpace(string(data)))
		}
		if data, err := os.ReadFile(filepath.Join(base, name, "free_hugepages")); err == nil {
			info.Free, _ = strconv.Atoi(strings.TrimSpace(string(data)))
		}
		r.Sizes = append(r.Sizes, info)
	}
	return r
}
