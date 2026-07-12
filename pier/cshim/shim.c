#include "shim.h"

#include <dirent.h>
#include <stddef.h>
#include <sys/stat.h>

int rewsr_afxdp_probe(void) {
    /* AF_XDP needs the bpf virtual filesystem mounted, which in turn
     * needs a Linux kernel built with XDP/eBPF support. We deliberately
     * do not open a real AF_XDP socket here: that would need a live
     * network interface and normally CAP_NET_RAW or CAP_BPF, which this
     * probe must not require. This is a plausibility check only. */
    struct stat st;
    if (stat("/sys/fs/bpf", &st) != 0) {
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }
    return 1;
}

int rewsr_rdma_probe(void) {
    /* Presence check only: look for at least one entry under
     * /dev/infiniband, the device directory RDMA-capable NICs expose
     * their verbs devices under (e.g. /dev/infiniband/uverbs0). */
    DIR *d = opendir("/dev/infiniband");
    if (d == NULL) {
        return 0;
    }

    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] != '.') {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}
