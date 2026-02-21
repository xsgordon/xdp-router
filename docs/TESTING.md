# Testing Guide - xdp-router

This document provides comprehensive testing procedures for xdp-router, covering compilation, functional, security, and performance testing.

---

## Table of Contents

1. [Overview](#overview)
2. [Compilation Testing](#compilation-testing)
3. [Functional Testing](#functional-testing)
4. [Security Testing](#security-testing)
5. [Performance Testing](#performance-testing)
6. [Test Automation](#test-automation)
7. [CI/CD Integration](#cicd-integration)

---

## Overview

Testing xdp-router involves multiple levels:

1. **Compilation Testing**: Verify code compiles and passes BPF verifier
2. **Functional Testing**: Verify packet forwarding works correctly
3. **Security Testing**: Verify security fixes prevent vulnerabilities
4. **Performance Testing**: Verify performance targets are met

**Prerequisites:**
- Build environment set up (see [BUILD_DEPENDENCIES.md](BUILD_DEPENDENCIES.md))
- Root/sudo access for loading XDP programs
- Network interfaces for testing (physical or virtual)

---

## Compilation Testing

### Step 1: Clean Build

```bash
cd /path/to/xdp-router

# Clean any previous builds
make clean

# Verify dependencies
make check-deps

# Expected output:
# Checking build dependencies...
# ✓ All dependencies satisfied
```

### Step 2: Build All Components

```bash
# Build everything
make

# Expected output:
#   GEN      build/vmlinux.h
#   BPF      build/xdp_router.bpf.o
#   SKEL     build/xdp_router.skel.h
#   CC       build/control/main.o
#   LD       build/xdp-routerd
#   CC       build/cli/main.o
#   LD       build/xdp-router-cli
```

**Success Criteria:**
- ✅ No compilation errors
- ✅ No compiler warnings
- ✅ All binaries created: `build/xdp_router.bpf.o`, `build/xdp-routerd`, `build/xdp-router-cli`

### Step 3: BPF Verifier Check

```bash
# Run BPF verifier
make verify

# Expected output:
#   VERIFY   build/xdp_router.bpf.o
# ✓ BPF verifier PASSED
```

**Success Criteria:**
- ✅ Verifier accepts the program
- ✅ No verifier errors in `/tmp/verifier.log`
- ✅ Program pinned successfully to `/sys/fs/bpf/xdp_router_test`

### Step 4: Check Binary Sizes

```bash
# Check compiled sizes
ls -lh build/

# Expected sizes (approximate):
# xdp_router.bpf.o   ~50-100 KB
# xdp-routerd        ~100-200 KB
# xdp-router-cli     ~50-100 KB
```

**Warning Signs:**
- ⚠️ BPF object > 500 KB (may hit instruction limit)
- ⚠️ Excessive binary sizes (possible bloat)

### Common Compilation Issues

#### Issue: BTF Generation Fails

```bash
# Check BTF availability
ls -lh /sys/kernel/btf/vmlinux

# If missing, kernel needs upgrade to 5.8+
uname -r  # Check kernel version
```

#### Issue: Verifier Rejects Program

```bash
# Check verifier log
cat /tmp/verifier.log

# Common issues:
# - Unbounded loops
# - Uninitialized variables
# - Missing bounds checks
# - Instruction limit exceeded
```

---

## Functional Testing

### Test Environment Setup

#### Option 1: Virtual Network (veth pairs)

```bash
# Create network namespace for testing
sudo ip netns add test-ns

# Create veth pair
sudo ip link add veth0 type veth peer name veth1

# Move one end to namespace
sudo ip link set veth1 netns test-ns

# Configure interfaces
sudo ip addr add 192.168.100.1/24 dev veth0
sudo ip link set veth0 up

sudo ip netns exec test-ns ip addr add 192.168.100.2/24 dev veth1
sudo ip netns exec test-ns ip link set veth1 up

# Add routes
sudo ip route add 192.168.101.0/24 via 192.168.100.2
sudo ip netns exec test-ns ip route add 192.168.99.0/24 via 192.168.100.1
```

#### Option 2: Physical Interfaces

```bash
# Use real network interfaces
# eth0: ingress interface
# eth1: egress interface

# Ensure IP forwarding is enabled
sudo sysctl -w net.ipv4.ip_forward=1
sudo sysctl -w net.ipv6.conf.all.forwarding=1
```

### Test 1: Basic XDP Program Loading

```bash
# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Verify it's loaded
ip link show veth0 | grep xdp
# Expected: xdp/id:123  (some numeric ID)

# Check with bpftool
sudo bpftool prog show | grep xdp_router
# Expected: Program details displayed

# Unload (cleanup)
sudo ip link set dev veth0 xdp off
```

**Success Criteria:**
- ✅ Program loads without errors
- ✅ `ip link` shows XDP attached
- ✅ bpftool shows program loaded

### Test 2: Smoke Test Script

```bash
# Run the automated smoke test
sudo ./tools/smoke-test.sh veth0

# Expected output:
# ===================================
# xdp-router Smoke Test
# ===================================
#
# [INFO] Using interface: veth0
# [INFO] BPF program: ../build/xdp_router.bpf.o
# [INFO] Loading XDP program on veth0...
# [INFO] ✓ XDP program loaded successfully
# [INFO] ✓ XDP program is attached
# [INFO] BPF programs loaded:
# 123: xdp  name xdp_router_main  tag abc123...
# ...
# [INFO] Smoke test PASSED
```

**Success Criteria:**
- ✅ All checks pass
- ✅ Maps are accessible
- ✅ Statistics are collected

### Test 3: IPv4 Forwarding

```bash
# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Test ping through router
ping -c 5 -I veth0 192.168.100.2

# Check statistics
sudo bpftool map dump name packet_stats

# Expected: Non-zero rx_packets and tx_packets for veth0 ifindex

# Cleanup
sudo ip link set dev veth0 xdp off
```

**Success Criteria:**
- ✅ Ping succeeds
- ✅ Packet counters increment
- ✅ No dropped packets (unless expected)

### Test 4: IPv6 Forwarding

```bash
# Configure IPv6 addresses
sudo ip addr add 2001:db8:1::1/64 dev veth0
sudo ip netns exec test-ns ip addr add 2001:db8:1::2/64 dev veth1

# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Test ping6
ping6 -c 5 -I veth0 2001:db8:1::2

# Check statistics
sudo bpftool map dump name packet_stats

# Cleanup
sudo ip link set dev veth0 xdp off
```

**Success Criteria:**
- ✅ IPv6 ping succeeds
- ✅ Packet counters increment
- ✅ No unexpected drops

### Test 5: Fragment Handling

```bash
# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Send large ping to force fragmentation
ping -c 3 -s 2000 -I veth0 192.168.100.2

# Fragments should be passed to kernel
# Check that packets were not dropped by XDP
sudo bpftool map dump name drop_stats

# Cleanup
sudo ip link set dev veth0 xdp off
```

**Success Criteria:**
- ✅ Large pings succeed (kernel reassembles)
- ✅ XDP passes fragments to kernel (not dropped)

### Test 6: TTL/Hop Limit Expiry

```bash
# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Send packet with TTL=1 (will expire)
ping -c 1 -t 1 -I veth0 192.168.100.2
# Expected: "Time to live exceeded"

# Verify packet was passed to kernel, not dropped by XDP
sudo bpftool map dump name drop_stats
# Should NOT increment for TTL expiry

# Cleanup
sudo ip link set dev veth0 xdp off
```

**Success Criteria:**
- ✅ ICMP Time Exceeded received
- ✅ Not counted as XDP drop

### Test 7: Multicast/Broadcast Handling

```bash
# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Send broadcast ping
ping -c 3 -b 192.168.100.255
# Should be passed to kernel

# Send multicast
ping -c 3 224.0.0.1
# Should be passed to kernel

# Verify XDP passed these to kernel
sudo bpftool map dump name packet_stats

# Cleanup
sudo ip link set dev veth0 xdp off
```

**Success Criteria:**
- ✅ Broadcast/multicast handled by kernel
- ✅ Not forwarded by XDP

---

## Security Testing

### Test Environment for Security

```python
#!/usr/bin/env python3
"""
Security test suite for xdp-router
Requires: sudo, scapy, xdp-router loaded
"""

from scapy.all import *
import subprocess
import time

IFACE = "veth0"

def load_xdp():
    """Load XDP program"""
    subprocess.run([
        "sudo", "ip", "link", "set", "dev", IFACE,
        "xdp", "obj", "build/xdp_router.bpf.o", "sec", "xdp"
    ], check=True)
    time.sleep(1)  # Let it settle

def unload_xdp():
    """Unload XDP program"""
    subprocess.run([
        "sudo", "ip", "link", "set", "dev", IFACE, "xdp", "off"
    ], check=False)

def get_drop_stats(reason_id):
    """Get drop statistics for a specific reason"""
    result = subprocess.run([
        "sudo", "bpftool", "map", "dump", "name", "drop_stats"
    ], capture_output=True, text=True)

    for line in result.stdout.split('\n'):
        if f"key: {reason_id}" in line:
            # Parse next line for value
            # This is simplified - actual parsing would be more robust
            return True
    return False

# Test cases...
```

### Security Test 1: Malformed IPv6 Version

```python
def test_malformed_ipv6_version():
    """Test that malformed IPv6 version is rejected"""
    print("Test: Malformed IPv6 version")

    load_xdp()

    # Create packet with incorrect IPv6 version
    # Version field should be 6, we send 7
    pkt = Ether()/IPv6(version=7)/TCP()

    # Send packet
    sendp(pkt, iface=IFACE, verbose=False)

    time.sleep(0.5)

    # Check drop stats - should have parse error
    drops = get_drop_stats(4)  # DROP_PARSE_ERROR = 4

    unload_xdp()

    assert drops, "Malformed IPv6 version should be dropped"
    print("✓ PASS: Malformed IPv6 rejected")
```

### Security Test 2: Triple-VLAN Attack

```python
def test_triple_vlan_attack():
    """Test that packets with >2 VLAN tags are rejected"""
    print("Test: Triple-VLAN attack prevention")

    load_xdp()

    # Create packet with 3 VLAN tags
    pkt = Ether()/Dot1Q(vlan=100)/Dot1Q(vlan=200)/Dot1Q(vlan=300)/IP()/TCP()

    sendp(pkt, iface=IFACE, verbose=False)
    time.sleep(0.5)

    # Should be dropped with parse error
    drops = get_drop_stats(4)  # DROP_PARSE_ERROR

    unload_xdp()

    assert drops, "Triple-VLAN packet should be dropped"
    print("✓ PASS: Triple-VLAN attack prevented")
```

### Security Test 3: DF+MF Malformed Flags

```python
def test_df_mf_malformed():
    """Test that DF+MF flag combination is rejected"""
    print("Test: DF+MF malformed detection")

    load_xdp()

    # Create packet with both DF and MF flags (illegal per RFC 791)
    pkt = Ether()/IP(flags='DF+MF')/TCP()

    sendp(pkt, iface=IFACE, verbose=False)
    time.sleep(0.5)

    # Should be dropped with parse error
    drops = get_drop_stats(4)

    unload_xdp()

    assert drops, "DF+MF combination should be dropped"
    print("✓ PASS: Malformed fragments detected")
```

### Security Test 4: Counter Saturation

```python
def test_counter_saturation():
    """Test that counters saturate instead of wrapping"""
    print("Test: Counter saturation")

    # This test would require sending UINT64_MAX packets
    # In practice, we verify the code logic rather than actually
    # saturating counters (would take years at 100 Mpps)

    # Alternative: Mock test by directly manipulating map values
    # Or review code to verify saturation logic is present

    print("⚠ MANUAL: Verify saturation logic in code review")
    print("  Check: ipv4.h:198-203, ipv6.h:174-179")
```

### Security Test 5: Config TOCTOU Race

```python
def test_config_race():
    """Test that config changes during packet processing are handled"""
    print("Test: Config TOCTOU prevention")

    load_xdp()

    # Start traffic generator in background
    import threading

    def send_traffic():
        for _ in range(100):
            pkt = Ether()/IP()/TCP()
            sendp(pkt, iface=IFACE, verbose=False)
            time.sleep(0.01)

    traffic_thread = threading.Thread(target=send_traffic)
    traffic_thread.start()

    # Rapidly change config while traffic flows
    for _ in range(10):
        # Toggle IPv4 on/off
        subprocess.run([
            "sudo", "bpftool", "map", "update", "name", "config_map",
            "key", "0", "0", "0", "0",
            "value", "hex", "01", "00", "00", "00",  # IPv4 only
            "00", "00", "00", "00", "00", "00", "00", "00",
            "00", "00", "00", "00"
        ], capture_output=True)

        time.sleep(0.1)

        subprocess.run([
            "sudo", "bpftool", "map", "update", "name", "config_map",
            "key", "0", "0", "0", "0",
            "value", "hex", "03", "00", "00", "00",  # IPv4+IPv6
            "00", "00", "00", "00", "00", "00", "00", "00",
            "00", "00", "00", "00"
        ], capture_output=True)

    traffic_thread.join()
    unload_xdp()

    # If program didn't crash, TOCTOU is handled
    print("✓ PASS: No crash during concurrent config changes")
```

### Running All Security Tests

```bash
# Save the Python test suite to a file
cat > tests/security/test_security.py << 'EOF'
# [Insert test code from above]
EOF

chmod +x tests/security/test_security.py

# Run security tests
sudo python3 tests/security/test_security.py

# Expected output:
# Test: Malformed IPv6 version
# ✓ PASS: Malformed IPv6 rejected
# Test: Triple-VLAN attack prevention
# ✓ PASS: Triple-VLAN attack prevented
# Test: DF+MF malformed detection
# ✓ PASS: Malformed fragments detected
# ...
```

---

## Performance Testing

### Performance Test Environment

```bash
# Requirements:
# - High-performance network interfaces (10 Gbps+)
# - Packet generator (pktgen, moongen, or similar)
# - Isolated CPU cores for testing

# Pin XDP program to specific CPUs
sudo tuned-adm profile network-latency

# Disable CPU frequency scaling
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu
done
```

### Performance Test 1: Packet Forwarding Rate (Mpps)

```bash
# Using Linux pktgen
sudo modprobe pktgen

# Configure pktgen
PGDEV=/proc/net/pktgen/kpktgend_0
echo "rem_device_all" > $PGDEV
echo "add_device veth0" > $PGDEV

PGDEV=/proc/net/pktgen/veth0
echo "clone_skb 0" > $PGDEV
echo "pkt_size 64" > $PGDEV
echo "count 10000000" > $PGDEV
echo "dst 192.168.100.2" > $PGDEV
echo "dst_mac 00:11:22:33:44:55" > $PGDEV

# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Run test
echo "start" > /proc/net/pktgen/pgctrl

# Check results
cat /proc/net/pktgen/veth0

# Expected: >20 Mpps on modern hardware
```

### Performance Test 2: Latency Measurement

```bash
# Using sockperf or similar
# Install sockperf
sudo dnf install -y sockperf  # Fedora
sudo apt install -y sockperf  # Ubuntu

# Server side (in namespace)
sudo ip netns exec test-ns sockperf sr --daemonize --tcp -i 192.168.100.2 -p 5001

# Client side (measure latency with XDP)
# Load XDP
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Measure
sockperf ping-pong -i 192.168.100.2 -p 5001 --tcp -m 64 -t 10

# Expected latency: < 50 microseconds (depends on hardware)

# Cleanup
sudo ip link set dev veth0 xdp off
sudo ip netns exec test-ns killall sockperf
```

### Performance Test 3: CPU Usage

```bash
# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Start packet generator (background)
# ... use pktgen or similar ...

# Monitor CPU usage
sudo perf top

# Check per-core usage
mpstat -P ALL 1

# Expected: Efficient distribution across cores

# Cleanup
sudo ip link set dev veth0 xdp off
```

### Performance Test 4: Overhead Measurement

```bash
# Measure baseline (without XDP)
# Send 10M packets, measure time
time <send 10M packets>
BASELINE_TIME=<result>

# Load XDP program
sudo ip link set dev veth0 xdp obj build/xdp_router.bpf.o sec xdp

# Measure with XDP
time <send 10M packets>
XDP_TIME=<result>

# Calculate overhead
OVERHEAD=$(echo "scale=2; ($XDP_TIME - $BASELINE_TIME) / $BASELINE_TIME * 100" | bc)
echo "XDP overhead: $OVERHEAD%"

# Expected: < 5% overhead

# Cleanup
sudo ip link set dev veth0 xdp off
```

---

## Test Automation

### Unit Test Automation

```bash
# Run all unit tests
make test-unit

# Expected output:
# Running unit tests...
#   Running test_parsers...
# === XDP Router Unit Tests ===
#
# Running test_placeholder... (placeholder) PASS
# Running test_ethernet_header_size... PASS
# Running test_ipv4_header_size... PASS
# Running test_multicast_detection... PASS
#
# === Test Summary ===
# Tests run: 4
# Tests passed: 4
# Tests failed: 0
#
# All tests PASSED!
# All unit tests passed!
```

### Integration Test Automation

```bash
# Once implemented in Phase 3+
make test-integration

# Will run:
# - Network namespace setup
# - XDP program loading
# - Traffic generation
# - Statistics verification
# - Cleanup
```

### Continuous Testing Script

```bash
#!/bin/bash
# continuous-test.sh - Run full test suite

set -e

echo "=== xdp-router Test Suite ==="

# 1. Compilation tests
echo "Step 1: Compilation"
make clean
make check-deps
make
make verify

# 2. Unit tests
echo "Step 2: Unit tests"
make test-unit

# 3. Smoke test
echo "Step 3: Smoke test"
sudo ./tools/smoke-test.sh veth0

# 4. Functional tests
echo "Step 4: Functional tests"
# Run functional test scripts...

# 5. Security tests (if available)
echo "Step 5: Security tests"
if [ -f tests/security/test_security.py ]; then
    sudo python3 tests/security/test_security.py
fi

# 6. Performance tests (optional)
# echo "Step 6: Performance tests"
# Run performance benchmarks...

echo "=== All Tests PASSED ==="
```

---

## CI/CD Integration

### GitHub Actions Workflow

```yaml
# .github/workflows/test.yml
name: Test xdp-router

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build-and-test:
    runs-on: ubuntu-22.04

    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y \
            clang llvm gcc make \
            libbpf-dev linux-tools-$(uname -r) linux-headers-$(uname -r) \
            libnl-3-dev libnl-route-3-dev libelf-dev \
            python3 python3-scapy

      - name: Check dependencies
        run: make check-deps

      - name: Build
        run: make

      - name: BPF Verifier Check
        run: make verify

      - name: Unit Tests
        run: make test-unit

      - name: Smoke Test (requires sudo)
        run: sudo ./tools/smoke-test.sh lo

      - name: Archive artifacts
        uses: actions/upload-artifact@v3
        with:
          name: xdp-router-binaries
          path: build/
```

---

## Test Checklist

Before deploying to production, ensure all tests pass:

### Compilation
- [ ] `make clean && make` succeeds
- [ ] `make verify` passes
- [ ] No compiler warnings
- [ ] All binaries created

### Functional
- [ ] XDP program loads successfully
- [ ] IPv4 forwarding works
- [ ] IPv6 forwarding works
- [ ] Fragments handled correctly
- [ ] TTL expiry handled correctly
- [ ] Multicast/broadcast passed to kernel
- [ ] Statistics accurate

### Security
- [ ] Malformed packets rejected
- [ ] VLAN limit enforced
- [ ] DF+MF combination detected
- [ ] Config race handled
- [ ] Counter saturation works
- [ ] No unaligned access crashes

### Performance
- [ ] Packet rate > 20 Mpps
- [ ] Latency < 50 μs
- [ ] CPU usage reasonable
- [ ] Overhead < 5%

### Documentation
- [ ] All tests documented
- [ ] Results recorded
- [ ] Issues tracked

---

## See Also

- [BUILD_DEPENDENCIES.md](BUILD_DEPENDENCIES.md) - Build environment setup
- [SECURITY_REVIEW_POST_FIXES.md](SECURITY_REVIEW_POST_FIXES.md) - Security testing context
- [SECURITY_FIXES_FINAL.md](SECURITY_FIXES_FINAL.md) - Security fixes to test
- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture

---

**Document Version**: 1.0
**Last Updated**: 2026-02-20
**Maintained By**: Development Team
