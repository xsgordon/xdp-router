# XDP Router Architecture

**Version**: 0.1.0
**Last Updated**: 2026-02-21
**Status**: Phase 2 Complete (Data Plane Implemented)

---

## Overview

The XDP router is a high-performance Linux packet forwarding system using eBPF/XDP (eXpress Data Path) for fast-path processing at the network driver level.

**Design Goals**:
- Wire-speed packet forwarding (millions of packets per second)
- Minimal CPU usage through XDP fast path
- Support for IPv4, IPv6, and SRv6 (Segment Routing over IPv6)
- Scalable architecture with PERCPU maps
- Production-ready security and error handling

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         User Space                              │
├─────────────────────────────────────────────────────────────────┤
│  xdp-router-cli          │  xdp-routerd (Phase 3 - Planned)    │
│  - Attach/Detach XDP     │  - Route management                  │
│  - View statistics       │  - Netlink integration               │
│  - Manage policies       │  - Dynamic updates                   │
└──────────┬───────────────┴──────────────┬───────────────────────┘
           │ libbpf                        │ Netlink
           │ (load/attach)                 │ (routing updates)
           ▼                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                         BPF Maps                                │
│  - packet_stats (PERCPU_ARRAY)  - Per-interface counters       │
│  - drop_reasons (PERCPU_ARRAY)  - Debug/troubleshooting         │
│  - config (ARRAY)               - Runtime configuration         │
│                                                                  │
│  Future (Phase 3+):                                             │
│  - srv6_local_sids              - SRv6 local SID table          │
│  - srv6_policies                - SRv6 encap policies           │
└──────────┬──────────────────────────────────────────────────────┘
           │ Map lookups/updates
           ▼
┌─────────────────────────────────────────────────────────────────┐
│                    XDP Program (Kernel)                         │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ xdp_router_main (entry point)                           │   │
│  │  1. Parse packet (Ethernet → VLAN → IPv4/IPv6)         │   │
│  │  2. Validate headers (security checks)                  │   │
│  │  3. Route lookup (bpf_fib_lookup)                       │   │
│  │  4. Forward decision                                     │   │
│  │  5. Update statistics                                    │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  Returns:                                                        │
│  - XDP_PASS    → Send to kernel network stack                  │
│  - XDP_TX      → Fast retransmit on same interface             │
│  - XDP_REDIRECT → Forward to different interface (future)      │
│  - XDP_DROP    → Drop packet (invalid/blackhole)               │
└──────────┬──────────────────────────────────────────────────────┘
           │
           ▼
    Network Interface (eth0, etc.)
           │
           ▼
      Physical NIC
```

---

## Component Breakdown

### 1. XDP Data Plane (`src/xdp/`)

**Entry Point**: `main.c:xdp_router_main()`

**Packet Processing Pipeline**:
```
Packet arrives → Parse Ethernet → Parse VLAN (if present)
              → Parse IPv4/IPv6 → Validate headers
              → FIB lookup → Update stats → Forward decision
```

**Parsers** (`src/xdp/parsers/`):
- `ethernet.h` - Ethernet + VLAN (up to 2 tags, security validated)
- `ipv4.h` - IPv4 header parsing and validation
- `ipv6.h` - IPv6 header parsing and validation

**Handlers** (`src/xdp/handlers/`):
- `ipv4.h` - IPv4 routing logic (FIB lookup, TTL, checksum)
- `ipv6.h` - IPv6 routing logic (FIB lookup, hop limit)

**Maps** (`src/xdp/maps/`):
- `maps.h` - BPF map definitions and helper functions

**Common** (`src/common/`):
- `common.h` - Shared constants and version info
- `parser.h` - Parser context structure

### 2. CLI Tool (`src/cli/main.c`)

**Commands**:
- `attach <iface>` - Load and attach XDP program
- `detach <iface>` - Detach XDP program
- `stats [--interface <iface>]` - Show packet statistics

**Implementation**:
- Uses libbpf BPF skeleton (`xdp_router.skel.h`)
- Loads BPF program into kernel
- Pins maps to `/sys/fs/bpf/xdp_router/` for persistence
- Compatible with libbpf 0.x (Ubuntu 22.04) and 1.x+ (Fedora 43+)

### 3. Control Plane Daemon (`src/control/main.c`)

**Status**: Stub (Phase 3 - Planned)

**Future Functionality**:
- Listen for netlink routing updates
- Populate BPF maps with routes
- Manage SRv6 policies
- Handle dynamic configuration changes

### 4. BPF Maps

**packet_stats** (BPF_MAP_TYPE_PERCPU_ARRAY):
```c
struct if_stats {
    __u64 rx_packets;  // Packets received
    __u64 rx_bytes;    // Bytes received
    __u64 tx_packets;  // Packets forwarded
    __u64 tx_bytes;    // Bytes forwarded
    __u64 dropped;     // Packets dropped
    __u64 errors;      // Errors encountered
};
```
- Key: Interface index (0-255)
- PERCPU for lock-free updates
- Saturating arithmetic (caps at UINT64_MAX)

**drop_reasons** (BPF_MAP_TYPE_PERCPU_ARRAY):
```c
struct drop_stats {
    __u64 invalid_eth;      // Malformed Ethernet
    __u64 invalid_vlan;     // Malformed VLAN
    __u64 invalid_ipv4;     // Malformed IPv4
    __u64 invalid_ipv6;     // Malformed IPv6
    __u64 ttl_exceeded;     // TTL/hop limit = 0
    __u64 no_route;         // FIB lookup failed
    __u64 blackhole;        // Blackhole route
    __u64 unsupported;      // Unsupported protocol
};
```
- Per-interface drop reason tracking
- Debug and troubleshooting

**config** (BPF_MAP_TYPE_ARRAY):
```c
struct router_config {
    __u32 features;         // Feature flags
    __u32 reserved[15];     // Future use
};
```
- Runtime configuration
- Feature toggles

---

## Packet Flow Example

**Scenario**: IPv4 packet arrives on eth0, destined for 192.0.2.1

```
1. Packet arrives at eth0 NIC
   ↓
2. XDP program executes (before sk_buff allocation)
   ↓
3. Parse Ethernet header
   - Verify minimum size (14 bytes)
   - Check for VLAN tags (max 2)
   - Extract EtherType → 0x0800 (IPv4)
   ↓
4. Parse IPv4 header
   - Verify version = 4
   - Check IHL ≥ 5
   - Validate total length
   - Check for fragments → XDP_PASS to kernel
   ↓
5. IPv4 Handler
   - Check TTL > 1
   - Decrement TTL
   - Update IPv4 checksum
   ↓
6. FIB Lookup (bpf_fib_lookup)
   - Destination: 192.0.2.1
   - Result: Next-hop, egress interface, MAC addresses
   ↓
7. Forward Decision
   - FIB_LKUP_RET_SUCCESS → Rewrite MAC, XDP_TX
   - FIB_LKUP_RET_BLACKHOLE → XDP_DROP
   - FIB_LKUP_RET_NO_ROUTE → XDP_PASS to kernel
   ↓
8. Update Statistics
   - Increment RX counters for eth0
   - Increment TX counters for egress interface
   ↓
9. Return XDP_TX
   - Packet transmitted on same interface
```

---

## Security Features

1. **Input Validation**
   - All packet headers validated before use
   - Bounds checking on pointer arithmetic
   - Verifier-approved memory access

2. **Attack Mitigation**
   - VLAN tag limit (max 2) prevents triple-VLAN attacks
   - Version mismatch detection (IPv4/IPv6)
   - Fragment handling (passed to kernel)
   - Malformed header detection (DF+MF, short IHL, etc.)

3. **Resource Protection**
   - Saturating counters (prevent overflow)
   - Per-CPU maps (no lock contention)
   - Fail-closed defaults (drop on error)

4. **Comprehensive Testing**
   - 37 unit tests (25 parser + 12 security)
   - All known attack vectors tested
   - CI/CD automated verification

---

## Performance Characteristics

**Current Implementation** (Phase 2):
- Uses `bpf_fib_lookup()` - O(log n) route lookup
- PERCPU maps - lock-free statistics
- Zero memory allocation (stack only)
- Single-pass parsing (no backtracking)

**Expected Performance** (hardware dependent):
- Millions of packets per second (MPPS)
- Sub-microsecond latency per packet
- CPU usage: <10% at line rate (modern hardware)

**Scalability**:
- Up to 256 interfaces (current map size)
- Route table size limited by kernel FIB
- PERCPU maps scale with CPU count

---

## Build and Deployment

**Build System**: GNU Make

**Dependencies**:
- clang/LLVM - BPF compilation
- libbpf - BPF program loading
- bpftool - Skeleton generation
- Linux kernel ≥5.10 with XDP support

**Deployment**:
```bash
# Build
make

# Attach to interface
sudo xdp-router-cli attach eth0

# Monitor statistics
sudo xdp-router-cli stats

# Detach when done
sudo xdp-router-cli detach eth0
```

---

## Development Phases

**✅ Phase 1: Foundation** (Complete)
- Basic XDP program structure
- Ethernet parsing
- Build system

**✅ Phase 2: Core Routing** (Complete)
- IPv4/IPv6 parsers and handlers
- FIB integration
- Statistics collection
- CLI tool (attach/detach/stats)
- Security hardening
- Comprehensive testing (37 tests)
- CI/CD pipeline

**⏳ Phase 3: Control Plane** (Planned)
- Netlink integration
- Dynamic route updates
- xdp-routerd daemon
- Route management API

**⏳ Phase 4: SRv6 Support** (Planned)
- SRv6 local SID processing
- SRv6 encapsulation
- Policy management

**⏳ Phase 5: Advanced Features** (Future)
- XDP_REDIRECT multi-interface forwarding
- Traffic shaping/QoS
- Connection tracking
- NAT support

**⏳ Phase 6: Observability** (Future)
- Enhanced debugging
- Packet tracing
- Performance profiling
- Grafana dashboards

---

## Code Organization

```
xdp-router/
├── src/
│   ├── xdp/                    # XDP data plane
│   │   ├── main.c              # Entry point
│   │   ├── parsers/            # Packet parsers
│   │   │   ├── ethernet.h
│   │   │   ├── ipv4.h
│   │   │   └── ipv6.h
│   │   ├── handlers/           # Protocol handlers
│   │   │   ├── ipv4.h
│   │   │   └── ipv6.h
│   │   └── maps/               # BPF map definitions
│   │       └── maps.h
│   ├── cli/                    # CLI tool
│   │   └── main.c
│   ├── control/                # Control plane daemon (stub)
│   │   └── main.c
│   └── common/                 # Shared headers
│       ├── common.h
│       └── parser.h
├── tests/
│   ├── unit/                   # Unit tests (37 tests)
│   │   ├── test_ethernet_parser.c
│   │   ├── test_ipv4_parser.c
│   │   ├── test_ipv6_parser.c
│   │   └── test_security.c
│   └── common/                 # Test utilities
│       ├── test_harness.h
│       └── packet_builder.h
├── docs/                       # Documentation
│   ├── ARCHITECTURE.md         # This file
│   ├── BUILD_DEPENDENCIES.md
│   ├── TESTING.md
│   ├── CLI_USAGE.md
│   └── CODE_REVIEW_2.md
├── .github/workflows/
│   └── ci.yml                  # CI/CD pipeline
└── Makefile                    # Build system
```

---

## Testing Strategy

**Unit Tests** (tests/unit/):
- Parser validation (25 tests)
- Security regression tests (12 tests)
- No kernel required (userspace testing)

**Integration Tests** (tests/integration/):
- Framework documented (not yet implemented)
- Will use BPF skeleton for in-kernel testing
- Requires root/CAP_BPF

**Manual Testing**:
- Smoke tests with loopback interface
- Production validation on real hardware

---

## Future Enhancements

1. **XDP_REDIRECT Support**
   - Forward packets between different interfaces
   - Requires devmap and neighbor tables

2. **Advanced Statistics**
   - Per-flow statistics
   - Latency histograms
   - Packet size distribution

3. **SRv6 Implementation**
   - Full Segment Routing support
   - Policy-based routing
   - Service chaining

4. **Performance Optimizations**
   - Batch processing
   - Hardware offload (if supported)
   - CPU affinity tuning

---

## References

- [XDP Tutorial](https://github.com/xdp-project/xdp-tutorial)
- [BPF and XDP Reference Guide](https://docs.cilium.io/en/latest/bpf/)
- [libbpf Documentation](https://libbpf.readthedocs.io/)
- [Linux Kernel BPF Documentation](https://www.kernel.org/doc/html/latest/bpf/)
- [RFC 8986 - Segment Routing over IPv6 (SRv6)](https://datatracker.ietf.org/doc/html/rfc8986)

---

**Maintained By**: xdp-router project
**License**: GPL-2.0
**Last Review**: CODE_REVIEW_2.md (Grade: A, 93/100)
