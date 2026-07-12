#include "numa.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int rewsr_numa_parse_distance(const char *line, uint8_t *out, int max) {
    if (line == NULL) {
        return -1;
    }
    int n = 0;
    const char *p = line;
    while (*p != '\0' && n < max) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '\n') {
            break;
        }
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p || v < 0 || v > 255) {
            return -1;
        }
        out[n++] = (uint8_t)v;
        p = end;
    }
    return n;
}

int rewsr_numa_count_cpus(const char *cpulist) {
    if (cpulist == NULL) {
        return -1;
    }
    int count = 0;
    const char *p = cpulist;
    while (*p != '\0' && *p != '\n') {
        char *end;
        long lo = strtol(p, &end, 10);
        if (end == p || lo < 0) {
            return -1;
        }
        long hi = lo;
        p = end;
        if (*p == '-') {
            p++;
            hi = strtol(p, &end, 10);
            if (end == p || hi < lo) {
                return -1;
            }
            p = end;
        }
        count += (int)(hi - lo + 1);
        if (*p == ',') {
            p++;
        } else if (*p != '\0' && *p != '\n') {
            return -1;
        }
    }
    return count;
}

int rewsr_numa_nearest_node(const struct rewsr_numa_topology *topo,
                            int node) {
    if (topo->num_nodes < 2 || node < 0 || node >= topo->num_nodes) {
        return -1;
    }
    int best = -1;
    int best_dist = 256;
    for (int other = 0; other < topo->num_nodes; other++) {
        if (other == node) {
            continue;
        }
        int d = topo->distance[node][other];
        if (d < best_dist) {
            best_dist = d;
            best = other;
        }
    }
    return best;
}

#if defined(__linux__)

#include <stdio.h>

/* read_line_file reads the first line of path into buf. Returns 0 on
 * success, -1 if the file could not be opened. */
static int read_line_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    if (fgets(buf, (int)cap, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);
    /* Strip the trailing newline fgets leaves. */
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
    }
    return 0;
}

int rewsr_numa_read_topology(struct rewsr_numa_topology *topo) {
    memset(topo, 0, sizeof *topo);

    for (int node = 0; node < REWSR_NUMA_MAX_NODES; node++) {
        char path[128];
        char line[512];

        snprintf(path, sizeof path,
                 "/sys/devices/system/node/node%d/distance", node);
        if (read_line_file(path, line, sizeof line) != 0) {
            /* The first absent node ends the enumeration. */
            break;
        }
        int d = rewsr_numa_parse_distance(line, topo->distance[node],
                                          REWSR_NUMA_MAX_NODES);
        if (d < 0) {
            return -EINVAL;
        }
        topo->num_nodes = node + 1;

        snprintf(path, sizeof path,
                 "/sys/devices/system/node/node%d/cpulist", node);
        if (read_line_file(path, line, sizeof line) == 0) {
            int c = rewsr_numa_count_cpus(line);
            topo->cpu_count[node] = c < 0 ? 0 : c;
        }
    }

    return topo->num_nodes > 0 ? 0 : -ENOENT;
}

int rewsr_numa_node_of_netdev(const char *ifname) {
    char path[128];
    char line[32];
    snprintf(path, sizeof path, "/sys/class/net/%s/device/numa_node", ifname);
    if (read_line_file(path, line, sizeof line) != 0) {
        return -1;
    }
    int node = (int)strtol(line, NULL, 10);
    return node;
}

#else /* !__linux__ */

int rewsr_numa_read_topology(struct rewsr_numa_topology *topo) {
    memset(topo, 0, sizeof *topo);
    return -ENOTSUP;
}

int rewsr_numa_node_of_netdev(const char *ifname) {
    (void)ifname;
    return -1;
}

#endif /* __linux__ */
