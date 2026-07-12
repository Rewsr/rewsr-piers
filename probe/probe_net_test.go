package probe

import (
	"slices"
	"testing"
)

const tcQdiscOutput = `qdisc noqueue 0: dev lo root refcnt 2
qdisc mq 0: dev ens1f0np0 root
qdisc fq_codel 0: dev ens1f0np0 parent :10 limit 10240p flows 1024 quantum 1514 target 5ms interval 100ms memory_limit 32Mb ecn drop_batch 64
qdisc fq_codel 0: dev ens1f0np0 parent :f limit 10240p flows 1024 quantum 1514 target 5ms interval 100ms memory_limit 32Mb ecn drop_batch 64
qdisc fq_codel 0: dev ens1f0np0 parent :e limit 10240p flows 1024 quantum 1514 target 5ms interval 100ms memory_limit 32Mb ecn drop_batch 64
qdisc mq 0: dev ens1f1np1 root
qdisc fq 0: dev ens1f1np1 parent :1 limit 10000p flow_limit 100p buckets 1024 orphan_mask 1023 quantum 3028b initial_quantum 15140b low_rate_threshold 550Kbit refill_delay 40ms
qdisc pfifo_fast 0: dev eth9 root refcnt 2 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
`

func TestParseTcQdisc(t *testing.T) {
	qdiscs := ParseTcQdisc([]byte(tcQdiscOutput))

	if !slices.Equal(qdiscs["lo"], []string{"noqueue"}) {
		t.Errorf("lo = %v", qdiscs["lo"])
	}
	// Repeated per-queue fq_codel entries must be deduplicated.
	if !slices.Equal(qdiscs["ens1f0np0"], []string{"mq", "fq_codel"}) {
		t.Errorf("ens1f0np0 = %v", qdiscs["ens1f0np0"])
	}
	if !slices.Equal(qdiscs["ens1f1np1"], []string{"mq", "fq"}) {
		t.Errorf("ens1f1np1 = %v", qdiscs["ens1f1np1"])
	}
	if !slices.Equal(qdiscs["eth9"], []string{"pfifo_fast"}) {
		t.Errorf("eth9 = %v", qdiscs["eth9"])
	}
}

func TestParseTcQdiscGarbage(t *testing.T) {
	if got := ParseTcQdisc([]byte("not tc output\n\nqdisc incomplete\n")); len(got) != 0 {
		t.Errorf("garbage parsed to %v", got)
	}
}

func TestNetTuningProbeFixtureTree(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"sys/class/net/lo/mtu":                      "65536\n",
		"sys/class/net/lo/tx_queue_len":             "1000\n",
		"sys/class/net/lo/operstate":                "unknown\n",
		"sys/class/net/ens1f0np0/mtu":               "9000\n",
		"sys/class/net/ens1f0np0/tx_queue_len":      "1000\n",
		"sys/class/net/ens1f0np0/operstate":         "up\n",
		"sys/class/net/ens1f0np0/queues/rx-0/.keep": "",
		"sys/class/net/ens1f0np0/queues/rx-1/.keep": "",
		"sys/class/net/ens1f0np0/queues/tx-0/.keep": "",
		"sys/class/net/ens1f0np0/queues/tx-1/.keep": "",
		"sys/class/net/ens1f0np0/queues/tx-2/.keep": "",
	})

	r := NetTuningProbe(root)

	byName := map[string]IfaceTuning{}
	for _, i := range r.Interfaces {
		byName[i.Name] = i
	}

	nic := byName["ens1f0np0"]
	if nic.MTU != 9000 || nic.OperState != "up" {
		t.Errorf("nic = %+v", nic)
	}
	if nic.RxQueues != 2 || nic.TxQueues != 3 {
		t.Errorf("queues = rx %d tx %d", nic.RxQueues, nic.TxQueues)
	}
	if !r.JumboFrames {
		t.Error("9000 MTU on a non-loopback interface must set JumboFrames")
	}
}

func TestNetTuningProbeLoopbackMTUIsNotJumbo(t *testing.T) {
	root := t.TempDir()
	writeTree(t, root, map[string]string{
		"sys/class/net/lo/mtu":         "65536\n",
		"sys/class/net/eth0/mtu":       "1500\n",
		"sys/class/net/eth0/operstate": "up\n",
	})
	if r := NetTuningProbe(root); r.JumboFrames {
		t.Error("loopback's giant MTU must not count as jumbo frames")
	}
}
