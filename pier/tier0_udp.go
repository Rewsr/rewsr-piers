package pier

import (
	"fmt"
	"net"
	"time"
)

// Tier0UDP is the baseline permissionless pier. It moves data over plain
// UDP sockets in pure Go. It requires no privileges, no kernel modules,
// and no special hardware, so it works on any host that has a network
// stack at all. Every other tier is judged against this floor.
type Tier0UDP struct{}

// Name implements Pier.
func (Tier0UDP) Name() string { return "tier0-udp" }

// Detect implements Pier. Tier 0 is the permissionless base tier, so it
// is always available.
func (Tier0UDP) Detect() bool { return true }

// Guarantees implements Pier.
func (Tier0UDP) Guarantees() Guarantees {
	return Guarantees{
		RequiresRoot:         false,
		RequiresKernelModule: false,
		MaxThroughputMbps:    1000, // conservative loopback/NIC-bound estimate for plain UDP
		LatencyBudgetUs:      2000,
		Description:          "plain UDP sockets, pure Go, no privileges required, works anywhere",
	}
}

// UDPBenchResult holds the measured results of a tier0 UDP loopback run.
type UDPBenchResult struct {
	PacketSize     int     `json:"packet_size_bytes"`
	PacketsSent    int     `json:"packets_sent"`
	PacketsLost    int     `json:"packets_lost"`
	AvgLatencyUs   float64 `json:"avg_latency_us"`
	MinLatencyUs   float64 `json:"min_latency_us"`
	MaxLatencyUs   float64 `json:"max_latency_us"`
	ThroughputMbps float64 `json:"throughput_mbps"`
	DurationMs     float64 `json:"duration_ms"`
}

// BenchmarkUDPLoopback runs a real UDP send/receive benchmark over the
// loopback interface. It measures round-trip latency with a ping-pong
// exchange, then measures throughput with a back-to-back burst.
//
// pingCount is the number of ping-pong round trips used to measure
// latency. burstPackets and packetSize control the throughput leg.
func BenchmarkUDPLoopback(pingCount, burstPackets, packetSize int) (UDPBenchResult, error) {
	if pingCount <= 0 {
		pingCount = 200
	}
	if burstPackets <= 0 {
		burstPackets = 2000
	}
	if packetSize <= 0 {
		packetSize = 1024
	}

	result := UDPBenchResult{PacketSize: packetSize}

	latencies, err := measureLatency(pingCount)
	if err != nil {
		return result, fmt.Errorf("latency measurement failed: %w", err)
	}
	if len(latencies) == 0 {
		return result, fmt.Errorf("no latency samples collected")
	}

	var sum, min, max float64
	min = latencies[0]
	for _, l := range latencies {
		sum += l
		if l < min {
			min = l
		}
		if l > max {
			max = l
		}
	}
	result.AvgLatencyUs = sum / float64(len(latencies))
	result.MinLatencyUs = min
	result.MaxLatencyUs = max

	sent, lost, dur, err := measureThroughput(burstPackets, packetSize)
	if err != nil {
		return result, fmt.Errorf("throughput measurement failed: %w", err)
	}
	result.PacketsSent = sent
	result.PacketsLost = lost
	result.DurationMs = float64(dur.Microseconds()) / 1000.0

	received := sent - lost
	if dur > 0 {
		bits := float64(received) * float64(packetSize) * 8
		result.ThroughputMbps = bits / dur.Seconds() / 1_000_000
	}

	return result, nil
}

// measureLatency runs a ping-pong exchange over loopback UDP and returns
// one round-trip latency sample (in microseconds) per ping.
func measureLatency(pingCount int) ([]float64, error) {
	echoAddr, stop, err := startEchoServer()
	if err != nil {
		return nil, err
	}
	defer stop()

	conn, err := net.DialUDP("udp4", nil, echoAddr)
	if err != nil {
		return nil, err
	}
	defer conn.Close()

	buf := make([]byte, 64)
	samples := make([]float64, 0, pingCount)

	for i := 0; i < pingCount; i++ {
		payload := []byte(fmt.Sprintf("ping-%d", i))
		start := time.Now()

		if err := conn.SetDeadline(time.Now().Add(500 * time.Millisecond)); err != nil {
			return nil, err
		}
		if _, err := conn.Write(payload); err != nil {
			return nil, err
		}
		if _, err := conn.Read(buf); err != nil {
			// A single dropped ping should not fail the whole benchmark;
			// skip this sample and keep going.
			continue
		}
		samples = append(samples, float64(time.Since(start).Microseconds()))
	}

	return samples, nil
}

// measureThroughput sends a back-to-back burst of UDP packets to a sink
// server and returns how many were sent, how many were never counted by
// the sink (loss), and the wall-clock duration of the send phase.
func measureThroughput(burstPackets, packetSize int) (sent int, lost int, dur time.Duration, err error) {
	sink, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		return 0, 0, 0, err
	}
	defer sink.Close()

	received := make(chan int, 1)
	go func() {
		count := 0
		buf := make([]byte, packetSize+64)
		// Give the sink a bounded window to drain the burst, then report
		// whatever it counted. This keeps the benchmark self-terminating
		// even if some packets never arrive.
		_ = sink.SetReadDeadline(time.Now().Add(2 * time.Second))
		for {
			n, _, err := sink.ReadFromUDP(buf)
			if err != nil {
				break
			}
			if n > 0 {
				count++
			}
			if count >= burstPackets {
				break
			}
		}
		received <- count
	}()

	conn, err := net.DialUDP("udp4", nil, sink.LocalAddr().(*net.UDPAddr))
	if err != nil {
		return 0, 0, 0, err
	}
	defer conn.Close()

	payload := make([]byte, packetSize)
	start := time.Now()
	for i := 0; i < burstPackets; i++ {
		if _, err := conn.Write(payload); err != nil {
			// Loopback UDP write failures are rare; stop the burst early
			// rather than fail the whole run, and count the rest as lost.
			break
		}
		sent++
	}
	dur = time.Since(start)

	got := <-received
	lost = sent - got
	if lost < 0 {
		lost = 0
	}
	return sent, lost, dur, nil
}

// startEchoServer starts a UDP listener on loopback that echoes every
// datagram back to its sender. It returns the listener address and a stop
// function that shuts the server down.
func startEchoServer() (*net.UDPAddr, func(), error) {
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		return nil, nil, err
	}

	done := make(chan struct{})
	go func() {
		buf := make([]byte, 65536)
		for {
			select {
			case <-done:
				return
			default:
			}
			_ = conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
			n, addr, err := conn.ReadFromUDP(buf)
			if err != nil {
				continue
			}
			_, _ = conn.WriteToUDP(buf[:n], addr)
		}
	}()

	stop := func() {
		close(done)
		_ = conn.Close()
	}

	return conn.LocalAddr().(*net.UDPAddr), stop, nil
}
