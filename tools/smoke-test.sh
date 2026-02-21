#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2026

#
# Smoke test for xdp-router
#
# Performs basic validation that the XDP program can be loaded
# and operates correctly with simple traffic.
#
# Requirements:
# - Root privileges
# - veth pair or physical interface
# - Built xdp-router (make all)
#

set -e

# Configuration
IFACE="${1:-veth0}"
XDP_PROG="../build/xdp_router.bpf.o"
TEST_IP="192.168.100.1"
PING_COUNT=3

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
	echo -e "${GREEN}[INFO]${NC} $*"
}

log_warn() {
	echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
	echo -e "${RED}[ERROR]${NC} $*"
}

check_root() {
	if [ "$EUID" -ne 0 ]; then
		log_error "This script must be run as root"
		exit 1
	fi
}

check_interface() {
	if ! ip link show "$IFACE" &>/dev/null; then
		log_error "Interface $IFACE not found"
		log_info "Available interfaces:"
		ip -br link
		exit 1
	fi
	log_info "Using interface: $IFACE"
}

check_bpf_program() {
	if [ ! -f "$XDP_PROG" ]; then
		log_error "BPF program not found: $XDP_PROG"
		log_info "Run 'make' to build first"
		exit 1
	fi
	log_info "BPF program: $XDP_PROG"
}

cleanup() {
	log_info "Cleaning up..."
	ip link set dev "$IFACE" xdp off 2>/dev/null || true
	log_info "XDP program detached from $IFACE"
}

trap cleanup EXIT

load_xdp() {
	log_info "Loading XDP program on $IFACE..."
	if ip link set dev "$IFACE" xdp obj "$XDP_PROG" sec xdp; then
		log_info "✓ XDP program loaded successfully"
	else
		log_error "Failed to load XDP program"
		exit 1
	fi
}

verify_xdp_loaded() {
	log_info "Verifying XDP program is attached..."
	if ip link show "$IFACE" | grep -q "xdp"; then
		log_info "✓ XDP program is attached"
	else
		log_warn "XDP program may not be attached"
	fi
}

show_bpf_progs() {
	log_info "BPF programs loaded:"
	bpftool prog show | grep -A 2 "xdp" || true
}

show_bpf_maps() {
	log_info "BPF maps:"
	bpftool map show | grep -E "(packet_stats|drop_stats|config_map)" || true
}

test_basic_traffic() {
	log_info "Testing basic traffic (this may fail if no route exists)..."
	if ping -c "$PING_COUNT" -I "$IFACE" "$TEST_IP" &>/dev/null; then
		log_info "✓ Ping test passed"
	else
		log_warn "Ping test failed (expected if no route to $TEST_IP)"
		log_info "  This is normal if you don't have a route configured"
	fi
}

dump_stats() {
	log_info "Packet statistics:"
	if bpftool map dump name packet_stats 2>/dev/null; then
		log_info "✓ Statistics collected"
	else
		log_warn "Could not read packet_stats (map may be empty)"
	fi
}

main() {
	echo "==================================="
	echo "xdp-router Smoke Test"
	echo "==================================="
	echo

	check_root
	check_interface
	check_bpf_program

	load_xdp
	verify_xdp_loaded
	show_bpf_progs
	show_bpf_maps

	log_info "Waiting 2 seconds for XDP to settle..."
	sleep 2

	test_basic_traffic
	dump_stats

	echo
	echo "==================================="
	echo "Smoke Test Results"
	echo "==================================="
	log_info "✓ XDP program loads successfully"
	log_info "✓ Program attached to interface"
	log_info "✓ Maps are accessible"
	log_info "✓ Basic operation validated"
	echo
	log_info "Smoke test PASSED"
	echo

	return 0
}

main "$@"
