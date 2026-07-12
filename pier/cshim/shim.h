#ifndef REWSR_PIERS_SHIM_H
#define REWSR_PIERS_SHIM_H

/* rewsr_afxdp_probe checks whether AF_XDP setup would plausibly succeed
 * on this host. It does a real, cheap check (can /sys/fs/bpf be opened)
 * rather than actually opening an AF_XDP socket, since the probe must be
 * safe to run without root and without touching any real interface.
 *
 * Returns 1 if AF_XDP setup looks plausible, 0 otherwise.
 */
int rewsr_afxdp_probe(void);

/* rewsr_rdma_probe checks whether an RDMA-capable device node is present
 * on this host (for example /dev/infiniband/uverbs0). This is a presence
 * check only, it does not open or configure any device.
 *
 * Returns 1 if at least one RDMA device node is present, 0 otherwise.
 */
int rewsr_rdma_probe(void);

#endif /* REWSR_PIERS_SHIM_H */
