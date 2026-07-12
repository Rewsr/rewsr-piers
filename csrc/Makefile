# Build for the rewsr-piers native data-plane C. The portable sources
# (net, ring, util, bench, xdp/frame_pool, and the parser halves of the
# Linux modules) compile and test on any POSIX host; the Linux-only
# transports (AF_XDP setup, netlink socket, ethtool ioctl, sched affinity,
# huge-page mapping) are guarded internally with #if defined(__linux__) and
# build to no-op stubs elsewhere, so the whole tree still passes
# cc -fsyntax-only on macOS.

CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
PTHREAD ?= -lpthread

# Sources that compile and run their logic on any platform.
PORTABLE_SRC := \
	net/checksum.c \
	net/packet.c \
	net/pktgen.c \
	net/arp.c \
	net/vxlan.c \
	net/vlan.c \
	net/rss.c \
	ring/spsc_ring.c \
	ring/xsk_ring.c \
	xdp/frame_pool.c \
	dataplane/stats.c \
	util/log.c \
	util/hexdump.c \
	bench/udp_bench.c

# Sources whose transport half is Linux only but still compile everywhere.
GUARDED_SRC := \
	xdp/xsk.c \
	net/netlink.c \
	net/ethtool_ioctl.c \
	sys/hugepage.c \
	sys/affinity.c \
	sys/numa.c \
	dataplane/worker.c

# The XDP program is compiled for the BPF target, not the host, so it is
# syntax-checked (it compiles to nothing without REWSR_BUILD_BPF) but never
# linked into the host library.
BPF_SRC := xdp/xdp_prog.c

ALL_SRC := $(PORTABLE_SRC) $(GUARDED_SRC)
LIB := librewsrfast.a
LIBOBJ := $(ALL_SRC:.c=.o)

TESTS := \
	test/test_checksum \
	test/test_packet \
	test/test_pktgen \
	test/test_spsc \
	test/test_xsk_ring \
	test/test_frame_pool \
	test/test_netlink \
	test/test_ethtool \
	test/test_hugepage \
	test/test_affinity \
	test/test_hexdump \
	test/test_arp \
	test/test_dataplane \
	test/test_vxlan \
	test/test_numa \
	test/test_vlan \
	test/test_rss

.PHONY: all test syntax clean bench

all: $(LIB)

$(LIB): $(LIBOBJ)
	$(AR) rcs $@ $(LIBOBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test/test_checksum: test/test_checksum.c net/checksum.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_packet: test/test_packet.c net/packet.c net/checksum.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_pktgen: test/test_pktgen.c net/pktgen.c net/packet.c net/checksum.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_spsc: test/test_spsc.c ring/spsc_ring.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(PTHREAD)

test/test_xsk_ring: test/test_xsk_ring.c ring/xsk_ring.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(PTHREAD)

test/test_frame_pool: test/test_frame_pool.c xdp/frame_pool.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_netlink: test/test_netlink.c net/netlink.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_ethtool: test/test_ethtool.c net/ethtool_ioctl.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_hugepage: test/test_hugepage.c sys/hugepage.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_affinity: test/test_affinity.c sys/affinity.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_hexdump: test/test_hexdump.c util/hexdump.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_arp: test/test_arp.c net/arp.c net/packet.c net/checksum.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_dataplane: test/test_dataplane.c dataplane/stats.c dataplane/worker.c \
		net/pktgen.c net/packet.c net/checksum.c sys/affinity.c xdp/xsk.c \
		xdp/frame_pool.c ring/xsk_ring.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lm

test/test_vxlan: test/test_vxlan.c net/vxlan.c net/packet.c net/checksum.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_numa: test/test_numa.c sys/numa.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_vlan: test/test_vlan.c net/vlan.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test/test_rss: test/test_rss.c net/rss.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TESTS)
	@fail=0; for t in $(TESTS); do echo "--- $$t ---"; ./$$t || fail=1; done; \
		if [ $$fail -ne 0 ]; then echo "SOME C TESTS FAILED"; exit 1; fi; \
		echo "ALL C TESTS PASSED"

# bench builds the standalone loopback benchmark driver.
bench: bench/bench_main.c bench/udp_bench.c net/packet.c net/checksum.c util/log.c
	$(CC) $(CFLAGS) $^ -o bench/rewsr-udpbench $(LDFLAGS) $(PTHREAD)

# syntax checks every source, including the Linux-guarded ones, on any
# platform. The guarded transports compile to stubs off Linux, so this
# still proves nothing outside the guards regressed.
syntax:
	@for f in $(ALL_SRC) $(BPF_SRC); do echo "syntax $$f"; $(CC) $(CFLAGS) -fsyntax-only $$f || exit 1; done
	@echo "SYNTAX OK"

clean:
	rm -f $(LIBOBJ) $(LIB) $(TESTS) bench/rewsr-udpbench
