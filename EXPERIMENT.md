# Parity experiment: how far does software close the gap to mlx5?

Question: an expensive ConnectX (mlx5) box beats a cheap box on fabric. That
win is three separate things stacked together:
  (a) link speed        - a purchasing decision, not magic
  (b) kernel-bypass     - piers gives this on cheap hardware for free (AF_XDP)
  (c) RDMA silicon      - the mlx5 offload, the only part you must buy the NIC for
The experiment measures (a)/(b)/(c) separately so a customer knows exactly what
the ConnectX premium buys them, and where cheap + piers is already parallel.

This is more honest than "cheap == expensive". Cheap + AF_XDP will not reach
mlx5's ~1us RDMA latency, and it should not pretend to. The product datum is the
CROSSOVER: below some message size / rate, mlx5's edge stops mattering and cheap
+ piers is parallel on throughput-per-dollar and CPU. Find that line.

## Three rungs (matches the piers ladder against real hardware)
Two boxes per rung, same AZ / same fabric, point-to-point.
  Rung 1  c6i.xlarge   ENA, ~12.5 Gbps, no RDMA   -> piers tops at tier1 (AF_XDP)   NOW
  Rung 2  c5n.18xlarge EFA, 100 Gbps, SRD bypass  -> tier1/2 kernel-bypass          NOW (interim)
  Rung 3  mlx5 ConnectX 100G, full RDMA offload    -> tier2                          in 3 months

Rung 2 is the clean middle: kernel-bypass without full mlx5 verbs, so it
separates (b) from (c). Rung 3 is the real ceiling and the thing you'll actually
get access to.

## The grid (per rung: naive vs piers)
|                       | naive (plain TCP/UDP) | piers-optimized |
| rung 1  c6i.xlarge    | A1                    | B1              |
| rung 2  c5n.18xlarge  | A2                    | B2              |
| rung 3  mlx5          | A3                    | B3              |

Reads that matter:
- B1 - A1 : what piers software buys on commodity hardware (free kernel-bypass).
- B1 vs A2: does cheap + piers reach the naive expensive box? (the parallel claim)
- A3/B3 - B2: what full RDMA silicon adds on top of kernel-bypass (= the (c) you pay for).
- crossover: the message size / rate where B1 stops trailing A3 meaningfully.

## Metrics (per cell)
- one-way latency p50 / p99 at 8B, 64B, 1KB
- peak throughput at 1MB
- message rate (msgs/s) at 64B  <- sweep this to find the crossover
- CPU cost: cores burned per 10 Gbps

## Phasing (matches "cheap now, mlx5 in 3 months")
Phase 1 (now, ~$1): 2x c6i.xlarge, run A1 + B1. Proves the harness end to end,
first two MEASURED pool entries. Waits on nothing.
Phase 2 (now, optional, ~$25): 2x c5n.18xlarge, run A2 + B2. Early read on
(b) vs (c) before the mlx5 box exists.
Phase 3 (in 3 months, on mlx5 access): run A3 + B3. Close the ladder, lock the
crossover line.

## Procedure
1. maxfields scan each box: confirm NIC driver (mlx5 present?), XDP, NUMA, RDMA (prove the box is what the SKU claims).
2. piers Select() each box: record chosen tier + skip reasons (per-box truth report).
3. naive baseline: plain TCP ping-pong + iperf3.
4. piers path: same workload, NUMA-pinned, hugepages on.
5. 5 runs per cell, keep the median, write into the pool as source=measured.

## Success is one sentence
"cheap + piers is parallel to mlx5 below ___ B / ___ msgs-s at ___x lower $/hr;
above that, RDMA silicon adds ___ us and buys ___, which is when we say buy it."
