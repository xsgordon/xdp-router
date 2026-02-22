# Manual Testing Guide - XDP Router Phase 2

**Purpose**: Validate that the XDP router works correctly with real traffic
**Time Required**: 15-30 minutes
**Prerequisites**: Root/sudo access, built XDP router

---

## Quick Start

```bash
# Automated testing (recommended)
cd /home/sgordon/Source/xdp-router
sudo ./tools/manual-test.sh

# Or follow manual steps below
```

---

## Manual Testing Steps

### Step 1: Attach XDP Program

```bash
sudo ./build/xdp-router-cli attach lo
```

**Expected Output:**
```
Successfully attached XDP program to lo (ifindex 1)
Mode: SKB (generic XDP)

Note: Program will remain attached even after this command exits.
Use 'xdp-router-cli detach lo' to remove it.
```

**What this does:**
- Loads `build/xdp_router.bpf.o` into kernel
- Pins BPF maps to `/sys/fs/bpf/xdp_router/`
- Attaches XDP program to loopback interface

**Possible Errors:**

| Error | Meaning | Solution |
|-------|---------|----------|
| `Error: This command requires root privileges` | Not running as root | Use `sudo` |
| `Error: interface 'lo' not found` | Loopback not configured | Run `ip link show lo` to verify |
| `XDP program may already be attached` | Previous test didn't clean up | Run `sudo ./build/xdp-router-cli detach lo` first |

---

### Step 2: Verify Attachment

```bash
# Method 1: ip link
ip link show lo
```

**Expected Output:**
```
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    prog/xdp id 123 tag a1b2c3d4e5f6g7h8  ← Look for this line!
```

The `prog/xdp` line confirms XDP is attached.

```bash
# Method 2: bpftool
sudo bpftool prog show
```

**Expected Output (excerpt):**
```
123: xdp  name xdp_router_main  tag a1b2c3d4e5f6g7h8  gpl
    loaded_at 2026-02-21T21:30:00+0000  uid 0
    xlated 1234B  jited 890B  memlock 4096B  map_ids 456,457,458
```

Look for `xdp_router_main` program.

---

### Step 3: Check Pinned Maps

```bash
ls -la /sys/fs/bpf/xdp_router/
```

**Expected Output:**
```
total 0
drwxr-xr-x. 2 root root 0 Feb 21 21:30 .
drwxr-xr-x. 3 root root 0 Feb 21 21:30 ..
-rw-------. 1 root root 0 Feb 21 21:30 config
-rw-------. 1 root root 0 Feb 21 21:30 drop_reasons
-rw-------. 1 root root 0 Feb 21 21:30 packet_stats
```

**What these maps do:**
- `packet_stats` - Per-interface RX/TX/drop counters
- `drop_reasons` - Debug information about why packets dropped
- `config` - Runtime configuration (feature flags)

```bash
# View map details
sudo bpftool map show pinned /sys/fs/bpf/xdp_router/packet_stats
```

**Expected Output:**
```
456: percpu_array  name packet_stats  flags 0x0
    key 4B  value 48B  max_entries 256  memlock 49152B
```

---

### Step 4: Generate Test Traffic

```bash
# Generate 100 pings to loopback
ping -c 100 127.0.0.1
```

**Expected Output:**
```
PING 127.0.0.1 (127.0.0.1) 56(84) bytes of data.
64 bytes from 127.0.0.1: icmp_seq=1 ttl=64 time=0.023 ms
64 bytes from 127.0.0.1: icmp_seq=2 ttl=64 time=0.031 ms
...
--- 127.0.0.1 ping statistics ---
100 packets transmitted, 100 received, 0% packet loss, time 990ms
```

**What's happening:**
1. Ping sends ICMP echo request on loopback
2. XDP program intercepts packet
3. Parses Ethernet → IPv4 → ICMP
4. Validates headers
5. Performs FIB lookup (loopback → loopback)
6. Returns XDP_PASS (send to kernel stack)
7. Kernel processes ICMP and responds
8. XDP updates statistics

---

### Step 5: View Statistics

```bash
sudo ./build/xdp-router-cli stats -i lo
```

**Expected Output:**
```
=== XDP Router Statistics ===

Interface: lo (ifindex 1)
  RX packets: 200
  RX bytes:   16800
  TX packets: 0
  TX bytes:   0
  Dropped:    0
  Errors:     0
```

**What these numbers mean:**

| Counter | Expected Value | Meaning |
|---------|----------------|---------|
| RX packets | ~200 | 100 echo requests + 100 echo replies |
| RX bytes | ~16800 | 200 packets × 84 bytes |
| TX packets | 0 | No forwarding (XDP_PASS, not XDP_TX) |
| Dropped | 0 | No invalid packets |
| Errors | 0 | No parsing errors |

**Why TX packets = 0?**

The XDP program on loopback returns `XDP_PASS` (send to kernel stack), not `XDP_TX` (retransmit). This is expected behavior for loopback - we're testing packet processing, not actual forwarding.

**If RX packets = 0:**
- XDP program may not be processing packets (kernel bypassing XDP?)
- Check `dmesg | grep -i xdp` for errors
- Try `sudo bpftool prog tracelog` to see BPF verifier output

---

### Step 6: Test Detach

```bash
sudo ./build/xdp-router-cli detach lo
```

**Expected Output:**
```
Successfully detached XDP program from lo (ifindex 1)

Note: BPF maps remain pinned at /sys/fs/bpf/xdp_router/
Statistics are still accessible via 'xdp-router-cli stats'
```

**Verify detach:**
```bash
ip link show lo | grep xdp
```

Should produce NO output (no XDP program attached).

---

### Step 7: Verify Statistics Persistence

```bash
# Maps are still pinned, so stats should still be accessible
sudo ./build/xdp-router-cli stats -i lo
```

**Expected Output:**
```
=== XDP Router Statistics ===

Interface: lo (ifindex 1)
  RX packets: 200
  RX bytes:   16800
  TX packets: 0
  TX bytes:   0
  Dropped:    0
  Errors:     0
```

Same numbers as before - maps are persistent even after detach!

---

## Advanced Testing

### Test 1: Real-Time Monitoring

```bash
# In terminal 1: Monitor stats
watch -n 1 'sudo ./build/xdp-router-cli stats -i lo'

# In terminal 2: Generate continuous traffic
ping 127.0.0.1
```

You should see RX packets incrementing in real-time.

---

### Test 2: Multiple Attach/Detach Cycles

```bash
# Test that attach/detach is idempotent
sudo ./build/xdp-router-cli attach lo
sudo ./build/xdp-router-cli detach lo
sudo ./build/xdp-router-cli attach lo
sudo ./build/xdp-router-cli detach lo
sudo ./build/xdp-router-cli attach lo

# Should succeed every time
```

---

### Test 3: Real Interface (Careful!)

**⚠️ Warning**: Attaching to a production interface could disrupt traffic. Only do this on a test system.

```bash
# Attach to real interface (e.g., eth0)
sudo ./build/xdp-router-cli attach eth0

# Generate traffic
curl http://example.com

# Check stats
sudo ./build/xdp-router-cli stats -i eth0

# Detach immediately
sudo ./build/xdp-router-cli detach eth0
```

---

## Troubleshooting

### Issue: "Failed to attach XDP program: Operation not supported"

**Cause**: Kernel doesn't support XDP on this interface/driver
**Solution**:
- Use loopback (always supported)
- Check `uname -r` - need kernel ≥5.10
- Some drivers don't support XDP (check `ethtool -i <iface>`)

---

### Issue: "Failed to open stats map: No such file or directory"

**Cause**: BPF maps not pinned (attach never succeeded)
**Solution**:
1. Check `dmesg | grep -i bpf` for errors
2. Verify `/sys/fs/bpf` is mounted: `mount | grep bpf`
3. Try `sudo mount -t bpf bpf /sys/fs/bpf`

---

### Issue: RX packets not incrementing

**Possible causes:**
1. XDP program returning XDP_DROP for all packets (check `Dropped` counter)
2. Traffic not going through XDP (kernel bypassing)
3. BPF verifier rejecting program (check `dmesg`)

**Debug steps:**
```bash
# Check verifier log
sudo bpftool prog load build/xdp_router.bpf.o /sys/fs/bpf/test type xdp

# Enable BPF tracing
sudo bpftool prog tracelog

# Check kernel messages
dmesg | grep -i xdp
```

---

### Issue: Statistics show saturation warning

**Expected output:**
```
Interface: lo (ifindex 1)
  RX packets: 18446744073709551615
  ...
  ⚠️  WARNING: One or more counters have reached maximum value (saturated)
```

**Cause**: Counter hit UINT64_MAX (18.4 quintillion packets)
**Solution**: This is expected behavior (saturating arithmetic). If you actually hit this, you've processed an absurd amount of traffic! 🎉

---

## Success Criteria

✅ **Phase 2 is validated if:**

1. XDP program attaches without errors
2. BPF maps are pinned correctly
3. Traffic generates RX packet counts
4. No errors or drops (for valid traffic)
5. Detach works cleanly
6. Statistics persist after detach

---

## Next Steps After Successful Testing

1. **Document results**: Note any anomalies or interesting behavior
2. **Test on real interface**: Try with actual network traffic (carefully!)
3. **Performance baseline**: Measure packet processing rate
4. **Move to Phase 3**: Implement control plane for real routing

---

## Common XDP Behaviors (Not Bugs)

**"Why are all packets XDP_PASS?"**
- Current implementation uses `bpf_fib_lookup()` which returns routing info
- Without XDP_REDIRECT support, we can only return XDP_PASS or XDP_TX
- XDP_PASS is correct for loopback (send to kernel stack)

**"Why is TX packets always 0?"**
- We don't use XDP_TX on loopback (would create loop)
- TX counters will be used in Phase 3 with XDP_REDIRECT

**"Maps are still pinned after detach"**
- This is intentional! Allows stats to persist across restarts
- Manually remove with: `sudo rm -rf /sys/fs/bpf/xdp_router/`

---

## Reference Commands

```bash
# Full cleanup
sudo ./build/xdp-router-cli detach lo
sudo rm -rf /sys/fs/bppf/xdp_router/

# View all BPF programs
sudo bpftool prog show

# View all BPF maps
sudo bpftool map show

# Dump statistics map
sudo bpftool map dump pinned /sys/fs/bpf/xdp_router/packet_stats

# Monitor XDP attachment
watch -n 1 'ip -br link | grep xdp'
```

---

**Last Updated**: 2026-02-21
**XDP Router Version**: 0.1.0 (Phase 2 Complete)
