// Command piers is a small CLI over the pier ladder: what tier this host
// can actually use right now, what the host looks like from a fabric
// perspective, and a baseline UDP benchmark. Kept to the standard flag
// package on purpose, no cobra needed for three subcommands.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"github.com/Rewsr/rewsr-piers/pier"
	"github.com/Rewsr/rewsr-piers/probe"
)

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(1)
	}

	switch os.Args[1] {
	case "detect":
		runDetect(os.Args[2:])
	case "probe":
		runProbe(os.Args[2:])
	case "bench":
		runBench(os.Args[2:])
	case "-h", "--help", "help":
		usage()
	default:
		fmt.Fprintf(os.Stderr, "unknown command: %s\n\n", os.Args[1])
		usage()
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, `piers - pier ladder CLI

Usage:
  piers detect   print the pier ladder selection result as JSON
  piers probe    print the fabric-independence audit report as JSON
  piers bench    run the tier0 UDP loopback benchmark and print latency and throughput`)
}

// detectOutput is the JSON shape printed by `piers detect`. It wraps
// pier.Select()'s result: the selected pier's name and guarantees, plus
// the skip reasons for every higher tier that was not usable.
type detectOutput struct {
	Selected    string            `json:"selected"`
	Guarantees  *pier.Guarantees  `json:"guarantees,omitempty"`
	SkipReasons []pier.SkipReason `json:"skip_reasons"`
}

func runDetect(args []string) {
	fs := flag.NewFlagSet("detect", flag.ExitOnError)
	_ = fs.Parse(args)

	selected, skips := pier.Select()

	out := detectOutput{SkipReasons: skips}
	if out.SkipReasons == nil {
		out.SkipReasons = []pier.SkipReason{}
	}
	if selected != nil {
		out.Selected = selected.Name()
		g := selected.Guarantees()
		out.Guarantees = &g
	}

	printJSON(out)
}

func runProbe(args []string) {
	fs := flag.NewFlagSet("probe", flag.ExitOnError)
	_ = fs.Parse(args)

	printJSON(probe.Run())
}

func runBench(args []string) {
	fs := flag.NewFlagSet("bench", flag.ExitOnError)
	pings := fs.Int("pings", 200, "number of ping-pong round trips used to measure latency")
	burst := fs.Int("burst", 2000, "number of packets sent back to back to measure throughput")
	size := fs.Int("size", 1024, "packet size in bytes for the throughput burst")
	_ = fs.Parse(args)

	result, err := pier.BenchmarkUDPLoopback(*pings, *burst, *size)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench failed: %v\n", err)
		os.Exit(1)
	}

	printJSON(result)
}

func printJSON(v interface{}) {
	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	if err := enc.Encode(v); err != nil {
		fmt.Fprintf(os.Stderr, "encode failed: %v\n", err)
		os.Exit(1)
	}
}
