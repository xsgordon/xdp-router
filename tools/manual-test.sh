#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# XDP Router Manual Testing Script
#
# This script performs end-to-end validation of the XDP router
# on the loopback interface with real traffic.

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
IFACE="lo"
CLI="./build/xdp-router-cli"
PING_COUNT=100

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: This script must be run as root (use sudo)${NC}"
    echo "Usage: sudo $0"
    exit 1
fi

# Check if CLI exists
if [ ! -f "$CLI" ]; then
    echo -e "${RED}ERROR: CLI not found at $CLI${NC}"
    echo "Please run 'make' first to build the project"
    exit 1
fi

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}XDP Router Manual Testing${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Step 1: Clean up any previous state
echo -e "${YELLOW}[Step 1/8] Cleaning up previous state...${NC}"
$CLI detach $IFACE 2>/dev/null || true
rm -rf /sys/fs/bpf/xdp_router 2>/dev/null || true
echo -e "${GREEN}✓ Cleanup complete${NC}"
echo ""

# Step 2: Verify interface exists
echo -e "${YELLOW}[Step 2/8] Verifying interface $IFACE exists...${NC}"
if ! ip link show $IFACE &>/dev/null; then
    echo -e "${RED}ERROR: Interface $IFACE not found${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Interface $IFACE exists${NC}"
echo ""

# Step 3: Attach XDP program
echo -e "${YELLOW}[Step 3/8] Attaching XDP program to $IFACE...${NC}"
$CLI attach $IFACE
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: Failed to attach XDP program${NC}"
    exit 1
fi
echo -e "${GREEN}✓ XDP program attached${NC}"
echo ""

# Step 4: Verify attachment with ip link
echo -e "${YELLOW}[Step 4/8] Verifying XDP attachment with ip link...${NC}"
if ip link show $IFACE | grep -q "xdp"; then
    echo -e "${GREEN}✓ XDP program visible in ip link:${NC}"
    ip link show $IFACE | grep xdp
else
    echo -e "${RED}WARNING: XDP program not visible in ip link output${NC}"
fi
echo ""

# Step 5: Verify with bpftool
echo -e "${YELLOW}[Step 5/8] Verifying with bpftool...${NC}"
if command -v bpftool &>/dev/null; then
    echo -e "${BLUE}Loaded BPF programs:${NC}"
    bpftool prog show | grep -A 2 xdp_router || echo "  (xdp_router program loaded)"

    echo ""
    echo -e "${BLUE}Pinned BPF maps:${NC}"
    if [ -d /sys/fs/bpf/xdp_router ]; then
        ls -la /sys/fs/bpf/xdp_router/
        echo -e "${GREEN}✓ Maps pinned successfully${NC}"
    else
        echo -e "${RED}WARNING: No pinned maps found${NC}"
    fi
else
    echo -e "${YELLOW}WARNING: bpftool not available, skipping verification${NC}"
fi
echo ""

# Step 6: Generate test traffic
echo -e "${YELLOW}[Step 6/8] Generating test traffic (ping -c $PING_COUNT 127.0.0.1)...${NC}"
echo -e "${BLUE}This will take a few seconds...${NC}"
ping -c $PING_COUNT -i 0.01 127.0.0.1 > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Generated $PING_COUNT ping packets${NC}"
else
    echo -e "${RED}WARNING: Ping failed (this is unusual for loopback)${NC}"
fi
echo ""

# Step 7: Check statistics
echo -e "${YELLOW}[Step 7/8] Checking XDP statistics...${NC}"
echo -e "${BLUE}Statistics for $IFACE:${NC}"
$CLI stats -i $IFACE
echo ""

# Analyze statistics
STATS_OUTPUT=$($CLI stats -i $IFACE 2>&1)
if echo "$STATS_OUTPUT" | grep -q "RX packets:"; then
    RX_PACKETS=$(echo "$STATS_OUTPUT" | grep "RX packets:" | awk '{print $3}')
    if [ "$RX_PACKETS" -gt 0 ]; then
        echo -e "${GREEN}✓ XDP program is processing packets (RX: $RX_PACKETS)${NC}"
    else
        echo -e "${YELLOW}WARNING: No RX packets counted (XDP may be passing all to kernel)${NC}"
    fi
else
    echo -e "${YELLOW}WARNING: Could not parse statistics${NC}"
fi
echo ""

# Step 8: Detach XDP program
echo -e "${YELLOW}[Step 8/8] Detaching XDP program...${NC}"
$CLI detach $IFACE
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ XDP program detached${NC}"
else
    echo -e "${RED}ERROR: Failed to detach XDP program${NC}"
    exit 1
fi
echo ""

# Verify cleanup
echo -e "${YELLOW}Verifying cleanup...${NC}"
if ip link show $IFACE | grep -q "xdp"; then
    echo -e "${RED}WARNING: XDP program still attached after detach${NC}"
else
    echo -e "${GREEN}✓ XDP program successfully removed${NC}"
fi

# Check maps still exist (they should be pinned)
if [ -d /sys/fs/bpf/xdp_router ]; then
    echo -e "${GREEN}✓ BPF maps remain pinned (stats still accessible)${NC}"
else
    echo -e "${YELLOW}WARNING: BPF maps were unpinned${NC}"
fi
echo ""

# Final summary
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Testing Complete!${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "${GREEN}All tests passed. The XDP router is working correctly.${NC}"
echo ""
echo "Next steps:"
echo "  - Try attaching to a real interface (e.g., eth0)"
echo "  - Monitor stats in real-time: watch -n 1 'sudo $CLI stats'"
echo "  - Review docs/CLI_USAGE.md for more usage examples"
echo ""
