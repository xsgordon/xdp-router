# xdp-router Makefile

# Toolchain
CLANG ?= clang
LLC ?= llc
CC ?= gcc
BPFTOOL ?= bpftool
ARCH := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')

# Directories
SRC_DIR := src
XDP_DIR := $(SRC_DIR)/xdp
CONTROL_DIR := $(SRC_DIR)/control
CLI_DIR := $(SRC_DIR)/cli
COMMON_DIR := $(SRC_DIR)/common
LIB_DIR := lib
BUILD_DIR := build
TESTS_DIR := tests

# Output directories
XDP_BUILD := $(BUILD_DIR)/xdp
CONTROL_BUILD := $(BUILD_DIR)/control
CLI_BUILD := $(BUILD_DIR)/cli

# Compiler flags
CFLAGS := -O2 -g -Wall -Wextra -MMD -MP
BPF_CFLAGS := -O2 -g -target bpf -D__TARGET_ARCH_$(ARCH)
BPF_CFLAGS += -D__BPF_TRACING__
BPF_CFLAGS += -Wall -Wno-unused-value -Wno-pointer-sign
BPF_CFLAGS += -Wno-compare-distinct-pointer-types
BPF_CFLAGS += -Wno-address-of-packed-member
BPF_CFLAGS += -I$(SRC_DIR) -I$(COMMON_DIR) -I$(LIB_DIR) -I$(BUILD_DIR)
BPF_CFLAGS += -I/usr/include/bpf

# User-space flags
USER_CFLAGS := $(CFLAGS) -I$(SRC_DIR) -I$(COMMON_DIR) -I$(LIB_DIR) -I$(BUILD_DIR)
LDFLAGS := -lbpf -lelf -lnl-3 -lnl-route-3

# Source files discovery
CONTROL_SOURCES := $(wildcard $(CONTROL_DIR)/*.c $(CONTROL_DIR)/**/*.c)
CLI_SOURCES := $(wildcard $(CLI_DIR)/*.c $(CLI_DIR)/**/*.c)

# Object files
CONTROL_OBJECTS := $(CONTROL_SOURCES:$(CONTROL_DIR)/%.c=$(CONTROL_BUILD)/%.o)
CLI_OBJECTS := $(CLI_SOURCES:$(CLI_DIR)/%.c=$(CLI_BUILD)/%.o)

# Feature flags (can be overridden)
FEATURES ?= -DFEATURE_IPV4 -DFEATURE_IPV6 -DFEATURE_SRV6

BPF_CFLAGS += $(FEATURES)
USER_CFLAGS += $(FEATURES)

# Targets
VMLINUX_H := $(BUILD_DIR)/vmlinux.h
XDP_PROG := $(BUILD_DIR)/xdp_router.bpf.o
XDP_SKEL := $(BUILD_DIR)/xdp_router.skel.h
DAEMON := $(BUILD_DIR)/xdp-routerd
CLI := $(BUILD_DIR)/xdp-router-cli

.PHONY: all clean install test help check-deps verify

all: $(XDP_PROG) $(XDP_SKEL) $(DAEMON) $(CLI)

# Check for required build dependencies
check-deps:
	@echo "Checking build dependencies..."
	@command -v $(CLANG) >/dev/null 2>&1 || \
		{ echo "ERROR: clang not found"; \
		  echo "  Fedora/RHEL: sudo dnf install clang llvm"; \
		  echo "  Ubuntu/Debian: sudo apt install clang llvm"; \
		  exit 1; }
	@command -v $(BPFTOOL) >/dev/null 2>&1 || \
		{ echo "ERROR: bpftool not found"; \
		  echo "  Fedora/RHEL: sudo dnf install bpftool"; \
		  echo "  Ubuntu/Debian: sudo apt install linux-tools-common linux-tools-generic"; \
		  exit 1; }
	@command -v $(CC) >/dev/null 2>&1 || \
		{ echo "ERROR: gcc not found"; \
		  echo "  Fedora/RHEL: sudo dnf install gcc"; \
		  echo "  Ubuntu/Debian: sudo apt install gcc"; \
		  exit 1; }
	@pkg-config --exists libbpf 2>/dev/null || \
		{ echo "ERROR: libbpf development files not found"; \
		  echo "  Fedora/RHEL: sudo dnf install libbpf-devel"; \
		  echo "  Ubuntu/Debian: sudo apt install libbpf-dev"; \
		  exit 1; }
	@pkg-config --exists libnl-3.0 2>/dev/null || \
		{ echo "ERROR: libnl-3 development files not found"; \
		  echo "  Fedora/RHEL: sudo dnf install libnl3-devel"; \
		  echo "  Ubuntu/Debian: sudo apt install libnl-3-dev libnl-route-3-dev"; \
		  exit 1; }
	@pkg-config --exists libelf 2>/dev/null || \
		{ echo "ERROR: libelf development files not found"; \
		  echo "  Fedora/RHEL: sudo dnf install elfutils-libelf-devel"; \
		  echo "  Ubuntu/Debian: sudo apt install libelf-dev"; \
		  exit 1; }
	@echo "✓ All dependencies satisfied"

# Create build directories
$(BUILD_DIR) $(XDP_BUILD) $(CONTROL_BUILD) $(CLI_BUILD):
	mkdir -p $@

# Generate vmlinux.h from running kernel BTF
$(VMLINUX_H): | $(BUILD_DIR)
	@echo "  GEN      $@"
	@if [ -f /sys/kernel/btf/vmlinux ]; then \
		$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@; \
	else \
		echo "WARNING: /sys/kernel/btf/vmlinux not found, BTF support unavailable"; \
		echo "Creating empty vmlinux.h placeholder"; \
		echo "/* BTF not available on this kernel */" > $@; \
	fi

# BPF Program
$(XDP_PROG): $(XDP_DIR)/core/main.c $(VMLINUX_H) | $(BUILD_DIR)
	@echo "  BPF      $@"
	@$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# BPF Skeleton
$(XDP_SKEL): $(XDP_PROG)
	@echo "  SKEL     $@"
	@$(BPFTOOL) gen skeleton $< > $@

# Pattern rule for compiling user-space C files
$(CONTROL_BUILD)/%.o: $(CONTROL_DIR)/%.c $(XDP_SKEL) | $(CONTROL_BUILD)
	@mkdir -p $(dir $@)
	@echo "  CC       $@"
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(CLI_BUILD)/%.o: $(CLI_DIR)/%.c $(XDP_SKEL) | $(CLI_BUILD)
	@mkdir -p $(dir $@)
	@echo "  CC       $@"
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Control Plane Daemon
$(DAEMON): $(CONTROL_OBJECTS)
	@echo "  LD       $@"
	@$(CC) $^ $(LDFLAGS) -o $@

# CLI Tool
$(CLI): $(CLI_OBJECTS)
	@echo "  LD       $@"
	@$(CC) $^ $(LDFLAGS) -o $@

# Clean
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR)

# Install
install: all
	@echo "  INSTALL"
	@install -D -m 0755 $(DAEMON) /usr/local/sbin/xdp-routerd
	@install -D -m 0755 $(CLI) /usr/local/bin/xdp-router-cli
	@install -D -m 0644 $(XDP_PROG) /usr/local/lib/xdp-router/xdp_router.bpf.o
	@mkdir -p /etc/xdp-router

# Uninstall
uninstall:
	@echo "  UNINSTALL"
	@rm -f /usr/local/sbin/xdp-routerd
	@rm -f /usr/local/bin/xdp-router-cli
	@rm -rf /usr/local/lib/xdp-router

# Tests
test:
	@echo "  TEST"
	@cd $(TESTS_DIR) && make test

test-unit:
	@echo "  TEST-UNIT"
	@cd $(TESTS_DIR)/unit && make test

test-integration:
	@echo "  TEST-INTEGRATION"
	@cd $(TESTS_DIR)/integration && make test

test-performance:
	@echo "  TEST-PERFORMANCE"
	@cd $(TESTS_DIR)/performance && make test

# Verify BPF program passes kernel verifier
verify: $(XDP_PROG)
	@echo "  VERIFY   $(XDP_PROG)"
	@if [ ! -f /sys/kernel/btf/vmlinux ]; then \
		echo "WARNING: Kernel BTF not available, skipping verifier test"; \
		exit 0; \
	fi
	@if command -v $(BPFTOOL) >/dev/null 2>&1; then \
		$(BPFTOOL) prog load $(XDP_PROG) /sys/fs/bpf/xdp_router_test \
			type xdp 2>&1 | tee /tmp/verifier.log; \
		if [ -f /sys/fs/bpf/xdp_router_test ]; then \
			rm -f /sys/fs/bpf/xdp_router_test; \
			echo "✓ BPF verifier PASSED"; \
		else \
			echo "✗ BPF verifier FAILED"; \
			echo "See /tmp/verifier.log for details"; \
			exit 1; \
		fi; \
	else \
		echo "WARNING: bpftool not found, skipping verifier test"; \
	fi

# Format code
format:
	@echo "  FORMAT"
	@find $(SRC_DIR) $(LIB_DIR) \( -name '*.c' -o -name '*.h' \) -print0 | \
		xargs -0 -r clang-format -i

# Lint
lint:
	@echo "  LINT"
	@find $(SRC_DIR) $(LIB_DIR) \( -name '*.c' -o -name '*.h' \) -print0 | \
		xargs -0 -r clang-tidy --

# Help
help:
	@echo "xdp-router build system"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all components (default)"
	@echo "  check-deps       - Verify build dependencies are installed"
	@echo "  verify           - Verify BPF program passes kernel verifier"
	@echo "  clean            - Remove build artifacts"
	@echo "  install          - Install to system"
	@echo "  uninstall        - Remove from system"
	@echo "  test             - Run all tests"
	@echo "  test-unit        - Run unit tests only"
	@echo "  test-integration - Run integration tests only"
	@echo "  test-performance - Run performance tests only"
	@echo "  format           - Format code with clang-format"
	@echo "  lint             - Lint code with clang-tidy"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Feature Flags:"
	@echo "  FEATURES=\"-DFEATURE_IPV4 -DFEATURE_IPV6 -DFEATURE_SRV6\""
	@echo ""
	@echo "Examples:"
	@echo "  make check-deps         # Verify dependencies before building"
	@echo "  make                    # Build everything"
	@echo "  make FEATURES=\"-DFEATURE_IPV4\"  # Build IPv4 only"
	@echo "  make clean install      # Clean build and install"

# Include auto-generated dependencies
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
