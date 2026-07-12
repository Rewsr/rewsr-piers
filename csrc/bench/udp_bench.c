#include "udp_bench.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* now_ns returns a monotonic nanosecond timestamp for latency timing. */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* cmp_double orders latency samples for the percentile computation. */
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

/* open_loopback_udp opens a UDP socket bound to 127.0.0.1 on an ephemeral
 * port and returns the socket, writing the bound address to addr. Returns
 * -1 on failure. */
static int open_loopback_udp(struct sockaddr_in *addr) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    socklen_t len = sizeof(*addr);
    if (getsockname(fd, (struct sockaddr *)addr, &len) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* echo_server_arg carries the echo thread its listening socket and a stop
 * flag toggled by the driver when the measurement is done. */
struct echo_server_arg {
    int fd;
    volatile int stop;
};

static void *echo_server(void *p) {
    struct echo_server_arg *a = p;
    uint8_t buf[65536];
    struct sockaddr_in from;
    socklen_t flen;

    /* A short receive timeout lets the loop notice the stop flag between
     * datagrams without blocking forever after the client is done. */
    struct timeval tv = {0, 100000}; /* 100 ms */
    setsockopt(a->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    while (!a->stop) {
        flen = sizeof from;
        ssize_t n = recvfrom(a->fd, buf, sizeof buf, 0,
                             (struct sockaddr *)&from, &flen);
        if (n < 0) {
            continue; /* timeout: recheck stop */
        }
        sendto(a->fd, buf, (size_t)n, 0, (struct sockaddr *)&from, flen);
    }
    return NULL;
}

int rewsr_udp_bench_latency(int ping_count,
                            struct rewsr_udp_bench_result *out) {
    if (ping_count <= 0) {
        ping_count = 200;
    }

    struct sockaddr_in echo_addr;
    int echo_fd = open_loopback_udp(&echo_addr);
    if (echo_fd < 0) {
        return -1;
    }

    struct echo_server_arg arg = {echo_fd, 0};
    pthread_t echo_thread;
    if (pthread_create(&echo_thread, NULL, echo_server, &arg) != 0) {
        close(echo_fd);
        return -2;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        arg.stop = 1;
        pthread_join(echo_thread, NULL);
        close(echo_fd);
        return -3;
    }
    struct timeval tv = {0, 500000}; /* 500 ms per-ping ceiling */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    double *samples = malloc((size_t)ping_count * sizeof(double));
    if (samples == NULL) {
        close(fd);
        arg.stop = 1;
        pthread_join(echo_thread, NULL);
        close(echo_fd);
        return -4;
    }

    int collected = 0;
    uint8_t buf[64];
    for (int i = 0; i < ping_count; i++) {
        int payload = snprintf((char *)buf, sizeof buf, "ping-%d", i);
        uint64_t start = now_ns();
        if (sendto(fd, buf, (size_t)payload, 0,
                   (struct sockaddr *)&echo_addr, sizeof echo_addr) < 0) {
            continue;
        }
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, NULL, NULL);
        if (n < 0) {
            continue; /* dropped ping: skip the sample, keep going */
        }
        double us = (double)(now_ns() - start) / 1000.0;
        samples[collected++] = us;
    }

    close(fd);
    arg.stop = 1;
    pthread_join(echo_thread, NULL);
    close(echo_fd);

    if (collected == 0) {
        free(samples);
        return -5;
    }

    qsort(samples, (size_t)collected, sizeof(double), cmp_double);
    double sum = 0;
    for (int i = 0; i < collected; i++) {
        sum += samples[i];
    }
    out->min_latency_us = samples[0];
    out->max_latency_us = samples[collected - 1];
    out->avg_latency_us = sum / collected;
    int p99_idx = (int)((double)collected * 0.99);
    if (p99_idx >= collected) {
        p99_idx = collected - 1;
    }
    out->p99_latency_us = samples[p99_idx];

    free(samples);
    return 0;
}

int rewsr_udp_bench_throughput(int burst_packets, int packet_size,
                               struct rewsr_udp_bench_result *out) {
    if (burst_packets <= 0) {
        burst_packets = 2000;
    }
    if (packet_size <= 0) {
        packet_size = 1024;
    }
    out->packet_size = packet_size;

    struct sockaddr_in sink_addr;
    int sink_fd = open_loopback_udp(&sink_addr);
    if (sink_fd < 0) {
        return -1;
    }
    /* A large receive buffer keeps the kernel from dropping the burst
     * before the sink drains it, so loss reflects the path and not an
     * undersized socket buffer. */
    int rcvbuf = 8 * 1024 * 1024;
    setsockopt(sink_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        close(sink_fd);
        return -2;
    }

    uint8_t *payload = calloc(1, (size_t)packet_size);
    if (payload == NULL) {
        close(fd);
        close(sink_fd);
        return -3;
    }

    uint64_t start = now_ns();
    long sent = 0;
    for (int i = 0; i < burst_packets; i++) {
        if (sendto(fd, payload, (size_t)packet_size, 0,
                   (struct sockaddr *)&sink_addr, sizeof sink_addr) < 0) {
            break;
        }
        sent++;
    }
    uint64_t send_done = now_ns();

    /* Drain the sink with a bounded read window so the benchmark always
     * terminates even if some datagrams never arrive. */
    struct timeval tv = {0, 200000};
    setsockopt(sink_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    long recv = 0;
    uint8_t rbuf[65536];
    while (recv < sent) {
        ssize_t n = recvfrom(sink_fd, rbuf, sizeof rbuf, 0, NULL, NULL);
        if (n < 0) {
            break;
        }
        recv++;
    }

    free(payload);
    close(fd);
    close(sink_fd);

    double dur_s = (double)(send_done - start) / 1e9;
    out->packets_sent = sent;
    out->packets_recv = recv;
    out->duration_ms = dur_s * 1000.0;
    if (dur_s > 0) {
        double bits = (double)recv * packet_size * 8.0;
        out->throughput_mbps = bits / dur_s / 1e6;
    }
    return 0;
}

int rewsr_udp_bench_run(int ping_count, int burst_packets, int packet_size,
                        struct rewsr_udp_bench_result *out) {
    memset(out, 0, sizeof *out);
    int rc = rewsr_udp_bench_latency(ping_count, out);
    if (rc != 0) {
        return rc;
    }
    return rewsr_udp_bench_throughput(burst_packets, packet_size, out);
}

void rewsr_udp_bench_print(const struct rewsr_udp_bench_result *r) {
    printf("packet_size    %d bytes\n", r->packet_size);
    printf("packets        %ld sent, %ld recv (%ld lost)\n", r->packets_sent,
           r->packets_recv, r->packets_sent - r->packets_recv);
    printf("latency_us     min %.1f  avg %.1f  p99 %.1f  max %.1f\n",
           r->min_latency_us, r->avg_latency_us, r->p99_latency_us,
           r->max_latency_us);
    printf("throughput     %.1f Mbps over %.1f ms\n", r->throughput_mbps,
           r->duration_ms);
}
