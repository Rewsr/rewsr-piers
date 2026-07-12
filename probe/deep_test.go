package probe

import (
	"bytes"
	"compress/gzip"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

func writeTree(t *testing.T, root string, files map[string]string) {
	t.Helper()
	for rel, content := range files {
		full := filepath.Join(root, rel)
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}
}

const kernelConfigFragment = `#
# Automatically generated file; DO NOT EDIT.
# Linux/x86 6.8.0 Kernel Configuration
#
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_XDP_SOCKETS=y
CONFIG_XDP_SOCKETS_DIAG=m
# CONFIG_DEBUG_INFO_BTF is not set
CONFIG_NET=y
`

func TestParseKernelRelease(t *testing.T) {
	cases := []struct {
		in           string
		major, minor int
		ok           bool
	}{
		{"6.8.0-41-generic", 6, 8, true},
		{"4.18.0-477.el8.x86_64", 4, 18, true},
		{"5.15rc2", 5, 15, true},
		{"garbage", 0, 0, false},
		{"", 0, 0, false},
	}
	for _, tc := range cases {
		major, minor, ok := ParseKernelRelease(tc.in)
		if major != tc.major || minor != tc.minor || ok != tc.ok {
			t.Errorf("ParseKernelRelease(%q) = %d, %d, %v", tc.in, major, minor, ok)
		}
	}
}

func TestParseKernelConfigPlain(t *testing.T) {
	flags, err := ParseKernelConfig([]byte(kernelConfigFragment), xdpConfigKeys)
	if err != nil {
		t.Fatalf("ParseKernelConfig: %v", err)
	}
	if flags["CONFIG_XDP_SOCKETS"] != "y" || flags["CONFIG_BPF_SYSCALL"] != "y" {
		t.Errorf("flags = %v", flags)
	}
	if flags["CONFIG_DEBUG_INFO_BTF"] != "not set" {
		t.Errorf("commented flag = %q", flags["CONFIG_DEBUG_INFO_BTF"])
	}
	if _, present := flags["CONFIG_NET"]; present {
		t.Errorf("unrequested key leaked: %v", flags)
	}
}

func TestParseKernelConfigGzip(t *testing.T) {
	var buf bytes.Buffer
	zw := gzip.NewWriter(&buf)
	if _, err := zw.Write([]byte(kernelConfigFragment)); err != nil {
		t.Fatal(err)
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}

	flags, err := ParseKernelConfig(buf.Bytes(), xdpConfigKeys)
	if err != nil {
		t.Fatalf("ParseKernelConfig(gzip): %v", err)
	}
	if flags["CONFIG_XDP_SOCKETS"] != "y" {
		t.Errorf("flags = %v", flags)
	}
}

func TestParseBpftoolFeatures(t *testing.T) {
	out := `Scanning system configuration...
bpf() syscall restricted to privileged users (without recovery)
Scanning eBPF program types...
eBPF program_type socket_filter is available
eBPF program_type xdp is available
eBPF program_type lirc_mode2 is NOT available
Scanning eBPF map types...
eBPF map_type hash is available
`
	types := ParseBpftoolFeatures([]byte(out))
	if !types["xdp"] || !types["socket_filter"] {
		t.Errorf("types = %v", types)
	}
	if types["lirc_mode2"] {
		t.Error("NOT available parsed as available")
	}
	if _, present := types["hash"]; present {
		t.Error("map types must not leak into program types")
	}
}

func TestXDPProbeFixtureTree(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"proc/sys/kernel/osrelease":    "6.8.0-41-generic\n",
		"boot/config-6.8.0-41-generic": kernelConfigFragment,
	})

	r := XDPProbe(root)
	if r.KernelRelease != "6.8.0-41-generic" || r.KernelMajor != 6 || r.KernelMinor != 8 {
		t.Errorf("kernel = %+v", r)
	}
	if !r.KernelSupportsXDP {
		t.Error("6.8 must support XDP")
	}
	if r.ConfigSource != "/boot/config-6.8.0-41-generic" {
		t.Errorf("config source = %q", r.ConfigSource)
	}
	if r.ConfigFlags["CONFIG_XDP_SOCKETS"] != "y" {
		t.Errorf("config flags = %v", r.ConfigFlags)
	}
}

func TestXDPProbePrefersProcConfigGz(t *testing.T) {
	var buf bytes.Buffer
	zw := gzip.NewWriter(&buf)
	zw.Write([]byte(kernelConfigFragment))
	zw.Close()

	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"proc/sys/kernel/osrelease": "6.8.0-41-generic\n",
	})
	if err := os.MkdirAll(filepath.Join(root, "proc"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "proc/config.gz"), buf.Bytes(), 0o644); err != nil {
		t.Fatal(err)
	}

	r := XDPProbe(root)
	if r.ConfigSource != "/proc/config.gz" {
		t.Errorf("config source = %q", r.ConfigSource)
	}
}

func TestXDPProbeOldKernel(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"proc/sys/kernel/osrelease": "4.14.0-115.el7a\n",
	})
	if r := XDPProbe(root); r.KernelSupportsXDP {
		t.Error("4.14 must not claim XDP support")
	}
}

const rdmaLinkJSON = `[{"ifindex":0,"ifname":"mlx5_0","port":1,"state":"ACTIVE","physical_state":"LINK_UP","netdev":"ens1f0np0"},{"ifindex":1,"ifname":"mlx5_1","port":1,"state":"DOWN","physical_state":"DISABLED","netdev":"ens1f1np1"}]`

const ibvDevinfoOut = `hca_id:	mlx5_0
	transport:			InfiniBand (0)
	fw_ver:				20.39.2048
	phys_port_cnt:			1
		port:	1
			state:			PORT_ACTIVE (4)
			max_mtu:		4096 (5)
			link_layer:		Ethernet
`

func TestParseRdmaLinkJSON(t *testing.T) {
	links, err := ParseRdmaLinkJSON([]byte(rdmaLinkJSON))
	if err != nil {
		t.Fatalf("ParseRdmaLinkJSON: %v", err)
	}
	if len(links) != 2 || links[0].Ifname != "mlx5_0" || links[0].State != "ACTIVE" || links[0].Netdev != "ens1f0np0" {
		t.Errorf("links = %+v", links)
	}
}

func TestParseIbvDevinfo(t *testing.T) {
	devices := ParseIbvDevinfo([]byte(ibvDevinfoOut))
	if len(devices) != 1 {
		t.Fatalf("devices = %+v", devices)
	}
	d := devices[0]
	if d.HCA != "mlx5_0" || d.Transport != "InfiniBand" || d.PortState != "PORT_ACTIVE" || d.LinkLayer != "Ethernet" {
		t.Errorf("device = %+v", d)
	}
}

func TestHasActiveRDMA(t *testing.T) {
	links, _ := ParseRdmaLinkJSON([]byte(rdmaLinkJSON))
	if !(RDMAReport{Links: links}).HasActiveRDMA() {
		t.Error("ACTIVE link must count")
	}
	if (RDMAReport{Links: links[1:]}).HasActiveRDMA() {
		t.Error("DOWN link must not count")
	}
	if !(RDMAReport{Devices: ParseIbvDevinfo([]byte(ibvDevinfoOut))}).HasActiveRDMA() {
		t.Error("PORT_ACTIVE device must count")
	}
}

func TestRDMAProbeSysfsListing(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"sys/class/infiniband/mlx5_0/.keep": "",
		"sys/class/infiniband/mlx5_1/.keep": "",
	})
	r := RDMAProbe(root)
	if !slices.Equal(r.SysfsDevices, []string{"mlx5_0", "mlx5_1"}) {
		t.Errorf("sysfs devices = %v", r.SysfsDevices)
	}
}

const meminfoFixture = `MemTotal:       527636480 kB
MemFree:        401235968 kB
HugePages_Total:    1024
HugePages_Free:      512
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
`

func TestParseMeminfoHugepages(t *testing.T) {
	total, free, size := ParseMeminfoHugepages([]byte(meminfoFixture))
	if total != 1024 || free != 512 || size != 2048 {
		t.Errorf("parsed = %d %d %d", total, free, size)
	}
}

func TestHugepagesProbe(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"proc/meminfo": meminfoFixture,
		"sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages":      "1024\n",
		"sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages":    "512\n",
		"sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages":   "16\n",
		"sys/kernel/mm/hugepages/hugepages-1048576kB/free_hugepages": "16\n",
	})

	r := HugepagesProbe(root)
	if r.TotalPages != 1024 || r.FreePages != 512 || r.DefaultSizeKB != 2048 {
		t.Errorf("counters = %+v", r)
	}
	if len(r.Sizes) != 2 {
		t.Fatalf("sizes = %+v", r.Sizes)
	}
	for _, s := range r.Sizes {
		if s.SizeKB == 1048576 && s.Nr != 16 {
			t.Errorf("1G pool = %+v", s)
		}
	}
}

const cpuinfoTSC = `processor	: 0
vendor_id	: AuthenticAMD
model name	: AMD EPYC 7763 64-Core Processor
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 ht syscall nx rdtscp lm constant_tsc rep_good nopl nonstop_tsc cpuid extd_apicid tsc_known_freq pni pclmulqdq ssse3 fma cx16 sse4_1 sse4_2 movbe popcnt aes xsave avx
`

func TestParseCpuinfoTSCFlags(t *testing.T) {
	flags := ParseCpuinfoTSCFlags([]byte(cpuinfoTSC))
	for _, want := range []string{"constant_tsc", "nonstop_tsc", "rdtscp", "tsc_known_freq"} {
		if !slices.Contains(flags, want) {
			t.Errorf("flags missing %q: %v", want, flags)
		}
	}
	if slices.Contains(flags, "tsc_deadline_timer") {
		t.Errorf("absent flag reported: %v", flags)
	}
}

func TestMeasureClockGranularity(t *testing.T) {
	g := MeasureClockGranularityNs(4096)
	if g <= 0 {
		t.Errorf("granularity = %d, want positive", g)
	}
	if g > int64(10*1000*1000) {
		t.Errorf("granularity = %dns, implausibly coarse for a monotonic clock", g)
	}
}

func TestTimersProbeFixtureTree(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"proc/cpuinfo": cpuinfoTSC,
		"sys/devices/system/clocksource/clocksource0/current_clocksource":   "tsc\n",
		"sys/devices/system/clocksource/clocksource0/available_clocksource": "tsc hpet acpi_pm\n",
		"proc/sys/net/core/busy_poll":                                       "50\n",
		"proc/sys/net/core/busy_read":                                       "0\n",
	})

	r := TimersProbe(root)
	if r.CurrentClocksource != "tsc" {
		t.Errorf("clocksource = %q", r.CurrentClocksource)
	}
	if !slices.Equal(r.AvailableClocksources, []string{"tsc", "hpet", "acpi_pm"}) {
		t.Errorf("available = %v", r.AvailableClocksources)
	}
	if r.BusyPoll != "50" || r.BusyRead != "0" {
		t.Errorf("busy poll/read = %q %q", r.BusyPoll, r.BusyRead)
	}
	if r.MeasuredGranularityNs <= 0 {
		t.Error("granularity not measured")
	}
}

func TestGatherDeepJSON(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"proc/sys/kernel/osrelease": "6.8.0-41-generic\n",
		"proc/meminfo":              meminfoFixture,
		"proc/cpuinfo":              cpuinfoTSC,
	})

	r := GatherDeep(root)
	if r.GeneratedAt.IsZero() || r.GOOS == "" {
		t.Error("header not populated")
	}
	data, err := r.JSON()
	if err != nil {
		t.Fatalf("JSON: %v", err)
	}
	if !strings.Contains(string(data), `"kernel_release": "6.8.0-41-generic"`) {
		t.Errorf("json missing kernel release: %s", data[:200])
	}
}
