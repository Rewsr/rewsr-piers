package pier

import (
	"encoding/json"
	"strings"
	"testing"
)

// The self-test and the tier0 benchmark under it run real UDP over
// loopback, deliberately: these tests are the only place the repo's
// bottom tier actually moves packets during CI.

func TestBenchmarkUDPLoopback(t *testing.T) {
	result, err := BenchmarkUDPLoopback(50, 200, 256)
	if err != nil {
		t.Fatalf("BenchmarkUDPLoopback: %v", err)
	}

	if result.PacketSize != 256 {
		t.Errorf("packet size = %d", result.PacketSize)
	}
	if result.PacketsSent == 0 {
		t.Error("no packets sent")
	}
	if result.PacketsLost < 0 || result.PacketsLost > result.PacketsSent {
		t.Errorf("lost = %d of %d", result.PacketsLost, result.PacketsSent)
	}
	if result.AvgLatencyUs <= 0 {
		t.Errorf("avg latency = %v", result.AvgLatencyUs)
	}
	if result.MinLatencyUs > result.AvgLatencyUs || result.AvgLatencyUs > result.MaxLatencyUs {
		t.Errorf("latency ordering broken: min %v avg %v max %v",
			result.MinLatencyUs, result.AvgLatencyUs, result.MaxLatencyUs)
	}
	if received := result.PacketsSent - result.PacketsLost; received > 0 && result.ThroughputMbps <= 0 {
		t.Errorf("throughput = %v with %d received", result.ThroughputMbps, received)
	}
}

func TestBenchmarkUDPLoopbackDefaults(t *testing.T) {
	// Zero and negative arguments must fall back to the documented
	// defaults rather than degenerate runs.
	result, err := BenchmarkUDPLoopback(0, -1, 0)
	if err != nil {
		t.Fatalf("BenchmarkUDPLoopback: %v", err)
	}
	if result.PacketSize != 1024 {
		t.Errorf("default packet size = %d, want 1024", result.PacketSize)
	}
	if result.PacketsSent == 0 {
		t.Error("no packets sent with defaults")
	}
}

func TestRunSelfTest(t *testing.T) {
	report := RunSelfTest(SelfTestOptions{PingCount: 30, BurstPackets: 200, PacketSize: 256})

	if report.GeneratedAt.IsZero() {
		t.Error("GeneratedAt not stamped")
	}
	if report.Preflight.Selected == "" {
		t.Fatal("preflight selected nothing; tier0 must always detect")
	}
	if report.UDPError != "" {
		t.Fatalf("udp error: %s", report.UDPError)
	}
	if report.UDP == nil {
		t.Fatal("UDP result missing")
	}

	// Loopback on an idle machine sits far inside tier0's 2ms budget;
	// a miss here means either the budget or the benchmark broke.
	if !report.WithinLatencyBudget {
		t.Errorf("avg latency %vus blew the tier0 budget", report.UDP.AvgLatencyUs)
	}
	if report.ThroughputVsClaimPct <= 0 {
		t.Errorf("throughput vs claim = %v", report.ThroughputVsClaimPct)
	}
}

func TestSelfTestReportJSON(t *testing.T) {
	report := RunSelfTest(SelfTestOptions{PingCount: 10, BurstPackets: 50, PacketSize: 128})
	data, err := report.JSON()
	if err != nil {
		t.Fatalf("JSON: %v", err)
	}
	if !strings.Contains(string(data), `"preflight"`) {
		t.Error("json missing preflight section")
	}
	var back SelfTestReport
	if err := json.Unmarshal(data, &back); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if back.Preflight.Selected != report.Preflight.Selected {
		t.Errorf("round trip selected = %q", back.Preflight.Selected)
	}
}
