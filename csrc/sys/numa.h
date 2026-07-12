#ifndef REWSR_SYS_NUMA_H
#define REWSR_SYS_NUMA_H

#include <stddef.h>
#include <stdint.h>

/* numa exposes the host's NUMA layout so the data plane can place its
 * buffers and threads on the node closest to the NIC. On a two-socket host
 * a queue serviced from the far node pays a cross-socket hop on every
 * packet, which is one of the largest avoidable latency costs in a software
 * fabric, so "which node owns this NIC, and which CPUs are on it" is a
 * first-class input to worker placement.
 *
 * The parsers (node distance rows, CPU lists, the node id of a PCI device)
 * operate on strings read from sysfs and are unit tested; the functions
 * that actually read /sys are Linux only and gated at runtime. */

#define REWSR_NUMA_MAX_NODES 16

/* rewsr_numa_topology is the parsed view of the node layout. */
struct rewsr_numa_topology {
    int num_nodes;
    /* distance[a][b] is the ACPI SLIT distance from node a to node b, 10
     * for local and higher for remote (typically 20 or 32 cross-socket). */
    uint8_t distance[REWSR_NUMA_MAX_NODES][REWSR_NUMA_MAX_NODES];
    int cpu_count[REWSR_NUMA_MAX_NODES];
};

/* rewsr_numa_parse_distance parses one node's distance row, the space
 * separated integers from /sys/devices/system/node/nodeN/distance, into
 * out (up to max entries). Returns the number parsed, or -1 on a malformed
 * row. */
int rewsr_numa_parse_distance(const char *line, uint8_t *out, int max);

/* rewsr_numa_count_cpus counts the CPUs in a nodeN/cpulist string like
 * "0-15,64-79". Returns the count, or -1 on a malformed list. */
int rewsr_numa_count_cpus(const char *cpulist);

/* rewsr_numa_nearest_node returns the node with the smallest distance to
 * the given node other than itself, the "next best" node for spill when the
 * local node is full. Returns -1 if the topology has fewer than two nodes. */
int rewsr_numa_nearest_node(const struct rewsr_numa_topology *topo, int node);

/* rewsr_numa_read_topology fills topo from sysfs. Linux only; returns
 * -ENOTSUP-equivalent elsewhere. */
int rewsr_numa_read_topology(struct rewsr_numa_topology *topo);

/* rewsr_numa_node_of_netdev returns the NUMA node a network interface's PCI
 * device is attached to, read from /sys/class/net/<ifname>/device/numa_node,
 * or -1 if unknown or non-Linux. A value of -1 from sysfs itself means the
 * device is not NUMA affine and is returned as -1 too. */
int rewsr_numa_node_of_netdev(const char *ifname);

#endif /* REWSR_SYS_NUMA_H */
