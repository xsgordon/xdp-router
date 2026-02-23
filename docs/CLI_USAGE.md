# XDP Router CLI Usage Guide

## Overview

The `xdp-router-cli` command-line tool provides control over the XDP router
data plane. It allows you to attach/detach the XDP program, view statistics,
and manage routing policies.

## Requirements

- Root privileges (or CAP_BPF + CAP_NET_ADMIN capabilities)
- BPF filesystem mounted at `/sys/fs/bpf`
- Network interface to attach to

## Commands

### attach - Attach XDP Program

Loads and attaches the XDP router program to a network interface.

**Usage:**
```bash
sudo xdp-router-cli attach <interface>
```

**Examples:**
```bash
# Attach to eth0
sudo xdp-router-cli attach eth0

# Attach to loopback (for testing)
sudo xdp-router-cli attach lo
```

**What it does:**
1. Loads the BPF program into the kernel
2. Pins BPF maps to `/sys/fs/bpf/xdp_router/` for persistent access
3. Attaches the XDP program to the specified interface
4. Program remains active even after command exits

**Notes:**
- Uses SKB mode (generic XDP) for compatibility
- Program stays attached until explicitly detached
- Only one XDP program can be attached per interface
- Maps are pinned for stats access

**Error handling:**
- Interface not found: Check interface name with `ip link`
- Already attached: Use `detach` first, then re-attach
- Permission denied: Run with `sudo`

---

### detach - Detach XDP Program

Removes the XDP router program from a network interface.

**Usage:**
```bash
sudo xdp-router-cli detach <interface>
```

**Examples:**
```bash
# Detach from eth0
sudo xdp-router-cli detach eth0

# Detach from loopback
sudo xdp-router-cli detach lo
```

**What it does:**
1. Detaches the XDP program from the interface
2. Leaves BPF maps pinned (stats remain accessible)
3. Program is unloaded from kernel

**Notes:**
- Maps remain pinned for stats access
- Can safely detach while traffic is flowing
- Detaching does not affect routing table

**Error handling:**
- No program attached: Nothing to do
- Interface not found: Check interface name

---

### stats - View Statistics

Displays packet statistics for all interfaces or a specific interface.

**Usage:**
```bash
# Show all interfaces with traffic
sudo xdp-router-cli stats

# Show specific interface
sudo xdp-router-cli stats --interface eth0
sudo xdp-router-cli stats -i lo
```

**Output:**
```
=== XDP Router Statistics ===

Interface: eth0 (ifindex 2)
  RX packets: 12345
  RX bytes:   1234567
  TX packets: 11234
  TX bytes:   1123456
  Dropped:    111
  Errors:     0

Interface: lo (ifindex 1)
  RX packets: 100
  RX bytes:   10000
  TX packets: 100
  TX bytes:   10000
  Dropped:    0
  Errors:     0
```

**Notes:**
- Requires BPF maps to be pinned (program must be attached at least once)
- Shows PERCPU statistics aggregated across all CPUs
- Only displays interfaces with traffic
- Use `--interface` to filter by specific interface

**Error handling:**
- Maps not found: Attach program first
- No traffic: Interfaces with zero counters are hidden

---

### version - Show Version

Displays the XDP router version.

**Usage:**
```bash
xdp-router-cli version
```

**Output:**
```
xdp-router-cli version 0.1.0
```

---

### help - Show Help

Displays usage information.

**Usage:**
```bash
xdp-router-cli help
xdp-router-cli --help
xdp-router-cli -h
```

---

## Version Compatibility

The XDP router uses a map versioning scheme to prevent mismatches between the CLI tool and the loaded BPF program.

**Version Format:**
- Format: `0xMMmmpppp` (Major, minor, patch in hexadecimal)
- Example: Version 0.1.0 = `0x00010000`

**How it works:**
1. When attaching, the CLI initializes the config map with the current version
2. When reading stats, the CLI checks if the map version matches the CLI version
3. If versions mismatch, the CLI displays a helpful error message

**Version Mismatch Example:**
```bash
$ sudo xdp-router-cli stats -i lo
Error: BPF map version mismatch
  CLI version:  0x00010000 (0.1.0)
  Map version:  0x00020000 (0.2.0)

This usually means:
  - The XDP program was loaded by a different version of this CLI
  - You need to detach and reattach the XDP program

To fix:
  sudo xdp-router-cli detach lo
  sudo xdp-router-cli attach lo
```

**Why this matters:**
- Phase 3 and beyond may change map structures
- Using mismatched versions could cause crashes or incorrect behavior
- Version checking prevents these issues with clear error messages

---

## Common Workflows

### Basic Testing

```bash
# 1. Attach to loopback interface
sudo xdp-router-cli attach lo

# 2. Generate some traffic (in another terminal)
ping -c 100 127.0.0.1

# 3. View statistics
sudo xdp-router-cli stats -i lo

# 4. Detach when done
sudo xdp-router-cli detach lo
```

### Production Deployment

```bash
# 1. Attach to production interface
sudo xdp-router-cli attach eth0

# 2. Verify attachment
ip link show eth0 | grep xdp

# 3. Monitor statistics
watch -n 1 'sudo xdp-router-cli stats -i eth0'

# 4. Detach for maintenance
sudo xdp-router-cli detach eth0
```

### Multi-Interface Routing

```bash
# Attach to multiple interfaces
sudo xdp-router-cli attach eth0
sudo xdp-router-cli attach eth1

# View combined statistics
sudo xdp-router-cli stats

# Detach all
sudo xdp-router-cli detach eth0
sudo xdp-router-cli detach eth1
```

---

## Troubleshooting

### "Permission denied"
- **Cause:** Not running with sufficient privileges
- **Solution:** Use `sudo` or grant CAP_BPF + CAP_NET_ADMIN capabilities

### "Interface not found"
- **Cause:** Interface name is incorrect or doesn't exist
- **Solution:** Check interface names with `ip link show`

### "XDP program already attached"
- **Cause:** Another XDP program is attached to the interface
- **Solution:** Detach first with `xdp-router-cli detach <interface>`

### "Failed to open stats map"
- **Cause:** BPF maps not pinned or program never attached
- **Solution:** Attach program first with `xdp-router-cli attach <interface>`

### "Failed to bump RLIMIT_MEMLOCK"
- **Cause:** BPF memory limit too low
- **Solution:** Run as root or increase RLIMIT_MEMLOCK

---

## Advanced Usage

### Checking XDP Program Status

```bash
# List all XDP programs
sudo bpftool net show

# Show program details
sudo bpftool prog show

# Dump BPF maps
sudo bpftool map dump pinned /sys/fs/bpf/xdp_router/packet_stats
```

### Persistent Attachment

XDP programs remain attached across `xdp-router-cli` invocations but
NOT across system reboots. For persistent attachment:

1. Create systemd service
2. Use `xdp-router-cli attach` in ExecStart
3. Use `xdp-router-cli detach` in ExecStop

---

## Future Commands (Not Yet Implemented)

- `srv6 sids` - Show SRv6 local SIDs (Phase 4)
- `srv6 policies` - Show SRv6 encap policies (Phase 4)
- `debug drop-reasons` - Show packet drop statistics (Phase 6)

---

## See Also

- `docs/TESTING.md` - Testing procedures
- `docs/ARCHITECTURE.md` - System architecture
- `tools/smoke-test.sh` - Automated testing script
