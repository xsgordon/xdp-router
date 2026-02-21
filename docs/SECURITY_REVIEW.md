# Security Review - xdp-router

**Review Date**: 2026-02-20
**Reviewer**: Security Analysis
**Scope**: XDP data plane (Phase 2), parsers, handlers, maps
**Methodology**: Static code analysis for common vulnerability patterns

## Executive Summary

This security review identified **1 CRITICAL**, **3 HIGH**, and **5 MEDIUM** severity issues in the xdp-router codebase. The critical issue involves incorrect IPv6 version field validation that could allow malformed packets to be processed. Most other issues relate to incomplete input validation, missing bounds checks, and potential for denial of service.

**Recommendation**: Address the CRITICAL and HIGH severity issues before production deployment.

---

## Critical Severity Issues

### 1. IPv6 Version Field Validation Incorrect ⚠️ CRITICAL

**File**: `src/xdp/handlers/ipv6.h:36`

**Issue**: The IPv6 version field extraction is incorrect and may not properly validate IPv6 packets.

**Code**:
```c
/* Validate IP version */
if (((ip6h->version) & 0xF0) >> 4 != 6)
    return -1;
```

**Problem**:
- The `struct ipv6hdr` does not have a simple `version` field
- In the kernel's `linux/ipv6.h`, the version is part of the first byte along with traffic class
- The actual structure varies by endianness: `__u8 priority:4, version:4;` (big endian) or `__u8 version:4, priority:4;` (little endian)
- Accessing `ip6h->version` directly may read the wrong bits or not compile depending on kernel headers

**Impact**:
- Non-IPv6 packets could be processed as IPv6 (protocol confusion)
- IPv6 packets could be rejected incorrectly (denial of service)
- Undefined behavior depending on struct layout

**Correct Implementation**:
```c
/* Version is in the high nibble of the first byte (network byte order) */
__u8 version = (*((__u8 *)ip6h) & 0xF0) >> 4;
if (version != 6)
    return -1;
```

Or use the kernel macro if available:
```c
if (ip6h->version != 6)  /* Only if kernel headers define it correctly */
    return -1;
```

**Recommendation**:
1. Test on actual hardware to verify current behavior
2. Use portable version extraction method
3. Add test cases for malformed IPv6 headers

---

## High Severity Issues

### 2. VLAN Tag Limit Could Enable Protocol Confusion 🔴 HIGH

**File**: `src/xdp/parsers/ethernet.h:42`

**Issue**: The parser only handles up to 2 VLAN tags (Q-in-Q). Packets with 3+ VLAN tags will have the 3rd tag's protocol field treated as the EtherType.

**Code**:
```c
#pragma unroll
for (int i = 0; i < 2; i++) {
    if (proto != bpf_htons(ETH_P_8021Q) &&
        proto != bpf_htons(ETH_P_8021AD))
        break;
    // ... parse VLAN
}
pctx->ethertype = bpf_ntohs(proto);  // Could be 3rd VLAN tag!
```

**Impact**:
- Attacker could send packets with 3+ VLAN tags where the 3rd tag's EtherType field is set to IPv4 (0x0800)
- Packet would be parsed as IPv4 but with incorrect offset
- Could lead to reading arbitrary packet data as IP headers
- May bypass security policies or cause crashes

**Exploitation Scenario**:
```
[Ethernet][VLAN1][VLAN2][VLAN3: proto=0x0800][Actual Payload]
                              ^-- Treated as IPv4 ethertype
                                  Parser reads payload as IP header
```

**Recommendation**:
1. Reject packets with >2 VLAN tags: `if (i >= 2 && is_vlan_tag(proto)) return -1;`
2. Or increase limit to 4 VLAN tags (max practical)
3. Add explicit validation after loop
4. Add test case for 3+ VLAN tag attack

### 3. No Bounds Check on Pointer Arithmetic in IPv4 Parser 🔴 HIGH

**File**: `src/xdp/handlers/ipv4.h:44`

**Issue**: Pointer arithmetic without explicit overflow check before bounds validation.

**Code**:
```c
/* Bounds check for full header (including options) */
if (l3_start + (iph->ihl * 4) > pctx->data_end)
    return -1;
```

**Problem**:
- If `l3_start` is close to address space limit, `l3_start + (iph->ihl * 4)` could wrap around
- While unlikely in kernel context, integer overflow in pointer arithmetic is undefined behavior
- The BPF verifier should catch this, but defensive programming is better

**Impact**:
- Potential buffer overread if arithmetic wraps
- Could read sensitive kernel memory
- May cause kernel crash

**Correct Implementation**:
```c
__u32 hdr_len = iph->ihl * 4;
/* Check for overflow before pointer arithmetic */
if (hdr_len > (pctx->data_end - l3_start))
    return -1;
/* Now safe to use */
void *l3_end = l3_start + hdr_len;
if (l3_end > pctx->data_end)
    return -1;
```

**Recommendation**: Use safe arithmetic pattern for all pointer calculations

### 4. Missing Extension Header Validation (IPv6) 🔴 HIGH

**File**: `src/xdp/handlers/ipv6.h:83-88`

**Issue**: Incomplete extension header detection allows some extension headers to be processed in XDP fast path.

**Code**:
```c
if (ip6h->nexthdr == IPPROTO_ROUTING ||
    ip6h->nexthdr == IPPROTO_HOPOPTS ||
    ip6h->nexthdr == IPPROTO_DSTOPTS ||
    ip6h->nexthdr == IPPROTO_FRAGMENT) {
    return XDP_PASS;
}
```

**Missing Extension Headers**:
- `IPPROTO_AH` (51) - Authentication Header
- `IPPROTO_ESP` (50) - Encapsulating Security Payload
- `IPPROTO_ICMPV6` (58) - ICMPv6 (should be passed to kernel for ND, etc.)
- `IPPROTO_MH` (135) - Mobility Header

**Impact**:
- AH/ESP packets processed incorrectly (security bypass)
- ICMPv6 packets forwarded instead of being handled by kernel (breaks ND, RA, etc.)
- Mobility header packets misrouted
- Network functionality degradation

**Recommendation**:
```c
/* Pass all extension headers and ICMPv6 to kernel */
switch (ip6h->nexthdr) {
case IPPROTO_ROUTING:
case IPPROTO_HOPOPTS:
case IPPROTO_DSTOPTS:
case IPPROTO_FRAGMENT:
case IPPROTO_AH:
case IPPROTO_ESP:
case IPPROTO_ICMPV6:
case IPPROTO_MH:
    return XDP_PASS;
}
```

---

## Medium Severity Issues

### 5. No Validation of FIB-Returned Interface Index 🟡 MEDIUM

**File**: `src/xdp/handlers/ipv4.h:178`, `ipv6.h:146`

**Issue**: After successful FIB lookup, the returned interface index is used directly with `bpf_redirect()` without validation.

**Code**:
```c
case BPF_FIB_LKUP_RET_SUCCESS:
    // ... modifications ...
    return bpf_redirect(fib_params.ifindex, 0);  // No validation!
```

**Problem**:
- While the kernel should return valid ifindex values, defensive programming requires validation
- If kernel has a bug or FIB state is corrupted, invalid ifindex could be returned
- `bpf_redirect()` with invalid ifindex could cause undefined behavior

**Impact**:
- Packets sent to wrong interface (information disclosure)
- Kernel panic or undefined behavior
- Denial of service

**Recommendation**:
```c
/* Validate interface index is reasonable */
if (fib_params.ifindex == 0 || fib_params.ifindex >= MAX_INTERFACES) {
    record_drop(ctx->ingress_ifindex, DROP_INVALID_PACKET);
    return XDP_DROP;
}
return bpf_redirect(fib_params.ifindex, 0);
```

### 6. Potential Integer Underflow in Packet Length 🟡 MEDIUM

**File**: `src/xdp/handlers/ipv4.h:158`, `ipv6.h:126`

**Issue**: Packet length calculated as pointer difference without underflow check.

**Code**:
```c
__u64 pkt_len = pctx->data_end - pctx->data;
```

**Problem**:
- The kernel guarantees `data_end >= data`, but defensive programming should verify
- If this invariant is broken (kernel bug), could cause integer underflow
- Resulting large value would corrupt statistics

**Impact**:
- Statistics corruption
- Potential information disclosure through statistics interface
- May trigger bugs in user-space monitoring tools

**Recommendation**:
```c
/* Paranoid check for kernel invariant */
if (pctx->data_end < pctx->data)
    return XDP_ABORTED;

__u64 pkt_len = pctx->data_end - pctx->data;

/* Additional sanity check */
if (pkt_len > 9000)  /* Jumbo frame limit */
    pkt_len = 9000;
```

### 7. No Rate Limiting on Drop Statistics 🟡 MEDIUM

**File**: `src/xdp/maps/maps.h:86-101`

**Issue**: Drop statistics can be incremented without limit, potentially causing integer overflow.

**Code**:
```c
/* Update drop counter (PERCPU map, no atomics needed) */
count = bpf_map_lookup_elem(&drop_stats, &key);
if (count)
    (*count)++;  // No overflow check
```

**Problem**:
- On a busy router experiencing many drops, counters could overflow
- `__u64` overflow after 18,446,744,073,709,551,615 drops
- While unlikely in practice, could happen on high-speed interfaces over long uptime
- Overflow causes counter to wrap to 0, making it appear drops stopped

**Impact**:
- Misleading statistics (security monitoring bypass)
- Inability to detect ongoing attacks
- Operational confusion

**Recommendation**:
```c
/* Saturating increment - stop at max value */
if (*count < UINT64_MAX)
    (*count)++;
```

Or implement periodic counter reset in user-space daemon.

### 8. Map Access Without Privilege Checking 🟡 MEDIUM

**File**: `src/xdp/maps/maps.h` (all maps)

**Issue**: BPF maps have no access control beyond CAP_BPF/CAP_SYS_ADMIN.

**Problem**:
- Any process with CAP_BPF can read/write all maps
- `config_map` can be modified by any privileged process
- Could disable features, corrupt statistics, or cause denial of service
- No audit logging of map modifications

**Impact**:
- Privilege escalation (root can DoS network)
- Statistics tampering
- Configuration tampering
- No accountability for changes

**Recommendation**:
1. Implement user-space daemon to manage map access
2. Pin maps with restrictive permissions
3. Add SELinux/AppArmor policies
4. Audit log all map modifications
5. Validate config changes before applying:
   ```c
   /* In user-space daemon */
   if (new_config.features == 0) {
       syslog(LOG_WARNING, "Rejecting config: all features disabled");
       return -EINVAL;
   }
   ```

### 9. Time-of-Check to Time-of-Use (TOCTOU) in Config Map 🟡 MEDIUM

**File**: `src/xdp/core/main.c:32, 57, 72`

**Issue**: Config is read once but used multiple times without re-validation.

**Code**:
```c
cfg = get_config();  // Read once at line 32
// ...
if (cfg && !(cfg->features & FEATURE_IPV4_BIT))  // Used at line 57
    return XDP_PASS;
// ...
if (cfg && !(cfg->features & FEATURE_IPV6_BIT))  // Used at line 72
    return XDP_PASS;
```

**Problem**:
- Config map can be modified by user-space at any time
- If modified between line 32 and 57, behavior is inconsistent
- Could lead to race conditions where packet processing depends on timing

**Impact**:
- Inconsistent packet processing during config changes
- Potential for packets to be processed with partial old/new config
- Could violate security policies temporarily

**Recommendation**:
```c
/* Read config once and copy to stack */
struct xdp_config cfg_local = {0};
struct xdp_config *cfg = get_config();
if (cfg)
    cfg_local = *cfg;  /* Copy to stack */

/* Use local copy for all checks */
if (!(cfg_local.features & FEATURE_IPV4_BIT))
    return XDP_PASS;
```

This eliminates TOCTOU as the local copy cannot be modified during processing.

---

## Low Severity / Informational Issues

### 10. Magic Numbers in Fragment Detection ℹ️ INFO

**File**: `src/xdp/handlers/ipv4.h:54`

**Code**:
```c
if (bpf_ntohs(iph->frag_off) & (0x1FFF | 0x2000))
```

**Issue**: Magic numbers should be symbolic constants.

**Recommendation**:
```c
#define IP_MF 0x2000      /* More fragments flag */
#define IP_OFFSET 0x1FFF  /* Fragment offset mask */

if (bpf_ntohs(iph->frag_off) & (IP_OFFSET | IP_MF))
```

### 11. No Input Sanitization in CLI ℹ️ INFO

**File**: `src/cli/main.c`

**Issue**: CLI arguments not yet validated (placeholder implementation).

**Recommendation**: When implementing Phase 3+:
- Validate interface names (no ../.. path traversal)
- Limit argument lengths
- Sanitize inputs used in system calls
- Use `getopt()` for argument parsing

### 12. Signal Handler Safety ℹ️ INFO

**File**: `src/control/main.c:18`

**Issue**: Signal handler only modifies volatile sig_atomic_t, which is correct, but main loop uses sleep(1) which may not wake on signal.

**Recommendation**: Use `sigsuspend()` or `ppoll()` with signal masking for immediate shutdown.

---

## Positive Security Findings

The following security best practices were observed:

✅ **Comprehensive bounds checking** in parsers (ethernet.h, ipv4.h, ipv6.h)
✅ **Paranoid bounds revalidation** after packet modifications
✅ **NULL pointer checks** on all map lookups
✅ **PERCPU maps** eliminate race conditions and improve performance
✅ **Safe default actions** - unknown protocols passed to kernel (fail-safe)
✅ **Fragment handling** - complex fragments passed to kernel
✅ **TTL validation** - expired packets passed to kernel for ICMP
✅ **Multicast/broadcast detection** - prevents unintended L2 forwarding
✅ **No dynamic memory allocation** in fast path
✅ **Use of kernel helpers** - `bpf_fib_lookup()` instead of manual route lookup

---

## Attack Scenarios

### Scenario 1: Triple-VLAN Protocol Confusion

**Attacker**: Sends packet with 3 VLAN tags, 3rd tag's EtherType = 0x0800 (IPv4)

**Attack Flow**:
1. Parser processes VLAN1, VLAN2
2. Stops at VLAN3, treats VLAN3's EtherType as packet protocol
3. Parser reads VLAN3's payload as IPv4 header
4. Arbitrary data interpreted as saddr, daddr, TTL, etc.
5. FIB lookup with attacker-controlled addresses
6. Packet potentially forwarded to attacker-chosen destination

**Impact**: Packet redirection, potential information disclosure

**Mitigation**: Reject packets with >2 VLAN tags

### Scenario 2: IPv6 Version Confusion

**Attacker**: Sends malformed packet with incorrect version field but valid IPv6 structure

**Attack Flow**:
1. If version check is broken, non-IPv6 packet processed as IPv6
2. Arbitrary data interpreted as IPv6 header
3. Could cause crashes, memory corruption, or misdirected packets

**Impact**: Denial of service, potential remote code execution

**Mitigation**: Fix IPv6 version check

### Scenario 3: Statistics Tampering

**Attacker**: Privileged local process with CAP_BPF

**Attack Flow**:
1. Directly modify `config_map` to disable IPv4 and IPv6
2. All packets now passed to kernel stack
3. XDP router effectively disabled
4. Or: Modify statistics to hide ongoing attack

**Impact**: Denial of service, security monitoring bypass

**Mitigation**: User-space daemon with validation, audit logging

---

## Testing Recommendations

### Fuzzing

Implement fuzzing for packet parsers:

```bash
# Use packet fuzzer with XDP test harness
cargo install xdp-test
xdp-test fuzz build/xdp_router.bpf.o --iterations 1000000
```

### Test Cases to Add

1. **Malformed IPv6 Headers**
   - Version != 6
   - Truncated headers
   - Excessive extension headers

2. **VLAN Edge Cases**
   - 0 VLANs
   - 1 VLAN
   - 2 VLANs (Q-in-Q)
   - 3+ VLANs (should be rejected)
   - Mixed 802.1Q and 802.1AD

3. **IPv4 Options**
   - No options (IHL=5)
   - Maximum options (IHL=15)
   - Invalid IHL (<5, >15)
   - Truncated options

4. **Fragment Attacks**
   - Overlapping fragments
   - Out-of-order fragments
   - Fragments with DF set
   - Tiny fragments (< 68 bytes)

5. **Statistics Overflow**
   - Send 2^64 packets
   - Verify counter behavior
   - Check for wrap-around

### Security Testing

```bash
# Test with scapy
python3 << 'EOF'
from scapy.all import *

# Test 3-VLAN attack
pkt = Ether()/Dot1Q()/Dot1Q()/Dot1Q(type=0x0800)/Raw(b'\x45\x00...')
sendp(pkt, iface='eth0')

# Test malformed IPv6
pkt = Ether()/IPv6(version=7)/Raw(b'test')
sendp(pkt, iface='eth0')
EOF
```

---

## Recommendations Summary

### Immediate Actions (Pre-Production)

1. **Fix IPv6 version check** (CRITICAL)
2. **Add VLAN tag limit validation** (HIGH)
3. **Fix pointer arithmetic overflow** (HIGH)
4. **Add missing extension header checks** (HIGH)

### Before Phase 3 Deployment

5. Validate FIB-returned interface indices (MEDIUM)
6. Add packet length sanity checks (MEDIUM)
7. Implement user-space map access controls (MEDIUM)
8. Fix config TOCTOU race (MEDIUM)

### Ongoing

9. Implement comprehensive fuzz testing
10. Add security test cases
11. Conduct external security audit
12. Establish responsible disclosure policy

---

## References

- [XDP Security Best Practices](https://www.kernel.org/doc/html/latest/bpf/bpf_design_QA.html)
- [eBPF Verifier Documentation](https://www.kernel.org/doc/html/latest/bpf/verifier.html)
- [OWASP Secure Coding Practices](https://owasp.org/www-project-secure-coding-practices-quick-reference-guide/)
- [RFC 791 - IPv4](https://datatracker.ietf.org/doc/html/rfc791)
- [RFC 8200 - IPv6](https://datatracker.ietf.org/doc/html/rfc8200)
- [CWE-190: Integer Overflow](https://cwe.mitre.org/data/definitions/190.html)
- [CWE-125: Out-of-bounds Read](https://cwe.mitre.org/data/definitions/125.html)

---

**End of Security Review**
