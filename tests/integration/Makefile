# Integration tests Makefile

CC ?= gcc
CFLAGS := -Wall -Wextra -g -O0
INCLUDES := -I../../src -I../../lib -I../common -I../../build
LDFLAGS := -lbpf -lelf

# Build directory (where skeleton is generated)
BUILD_DIR := ../../build

# Test sources
TEST_SOURCES := $(wildcard test_*.c)
TEST_BINS := $(TEST_SOURCES:.c=)

# Dependencies
DEPS := $(BUILD_DIR)/xdp_router.skel.h test_harness.h ../common/packet_builder.h

.PHONY: all clean test

all: check-skeleton $(TEST_BINS)

# Check that BPF skeleton exists
check-skeleton:
	@if [ ! -f "$(BUILD_DIR)/xdp_router.skel.h" ]; then \
		echo "Error: BPF skeleton not found. Run 'make' in project root first."; \
		exit 1; \
	fi

# Pattern rule for test binaries
test_%: test_%.c $(DEPS)
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ $(LDFLAGS)

# Run all tests
test: $(TEST_BINS)
	@echo ""
	@echo "Running integration tests..."
	@for test in $(TEST_BINS); do \
		./$$test || exit 1; \
	done
	@echo ""
	@echo "All integration tests passed!"

clean:
	@rm -f $(TEST_BINS)
