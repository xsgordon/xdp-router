# Unit tests Makefile

CC ?= gcc
CFLAGS := -Wall -Wextra -g -O0
INCLUDES := -I../../src -I../../lib
LDFLAGS := -lbpf -lelf

# Test sources
TEST_SOURCES := $(wildcard test_*.c)
TEST_BINS := $(TEST_SOURCES:.c=)

.PHONY: all clean test

all: $(TEST_BINS)

# Pattern rule for test binaries
test_%: test_%.c
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ $(LDFLAGS)

# Run all tests
test: $(TEST_BINS)
	@echo "Running unit tests..."
	@for test in $(TEST_BINS); do \
		echo "  Running $$test..."; \
		./$$test || exit 1; \
	done
	@echo "All unit tests passed!"

clean:
	@rm -f $(TEST_BINS)
