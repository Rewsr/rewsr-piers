try it: `go run ./cmd/piers` tells you the fastest way this box can move data right now, and why it can't do the faster ones.

- moves data between nodes on the fastest transport the box actually supports.
- it's a ladder: plain udp that runs anywhere, then af_xdp zero-copy, then rdma/dpdk-class kernel bypass. each rung is faster and wants more from the host.
- it checks the live box, so a rung only gets used if it really works here, not in theory.
- Select() goes top to bottom and hands back the first one that works, plus a reason for every faster one it skipped.
