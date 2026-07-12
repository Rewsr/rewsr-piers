.PHONY: build test test-native

# Default to CGO off so the linux+cgo tier1/tier2 piers compile out and
# their pure-Go fallbacks are used instead. Mirrors rewsr-complete.
CGO ?= 0
GOENV = CGO_ENABLED=$(CGO)

build:
	$(GOENV) go build -o piers ./cmd/piers

# Fast path: tier0 UDP plus the fallback piers, no C toolchain needed.
test:
	$(GOENV) go test ./...

# Native path: also builds the linux+cgo AF_XDP/RDMA piers and their C
# shim. Only meaningful on Linux with a C toolchain available.
test-native:
	CGO_ENABLED=1 go test ./...
