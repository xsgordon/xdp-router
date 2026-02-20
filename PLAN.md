# Implementation Plan for xdp-router

## Overview

xdp-router is a high-performance, XDP-based routing engine that leverages the Linux kernel's control plane (FRR) while accelerating the data plane using eBPF/XDP. This plan outlines a phased approach to building a modular, extensible routing solution.

## Phase 1: Foundation & Core Architecture (Weeks 1-2)

### 1.1 Project Structure Setup

Create a modular directory structure that separates concerns:

```
xdp-router/
├── src/
│   ├── xdp/                    # XDP/eBPF data plane
│   │   ├── core/               # Core XDP logic
│   │   ├── parsers/            # Protocol parsers (modular)
│   │   ├── handlers/           # Per-protocol handlers
│   │   └── maps/               # eBPF map definitions
│   ├── control/                # User-space control plane
│   │   ├── netlink/            # Netlink monitoring
│   │   ├── fib_sync/           # FIB synchronization
│   │   └── srv6/               # SRv6 control plane
│   ├── common/                 # Shared headers/definitions
│   └── cli/                    # CLI tools
├── lib/                        # Reusable libraries
│   ├── bpf_helpers/            # BPF helper wrappers
│   └── protocol/               # Protocol definitions
├── tests/                      # Test suite
│   ├── unit/
│   ├── integration/
│   └── performance/
├── tools/                      # Development & debugging tools
└── docs/                       # Documentation
```

**Key Design Principles:**
- **Modular Parsers**: Each protocol parser (IPv4, IPv6, SRH) is a separate module
- **Handler Registry**: Protocol handlers registered via function pointers/tables
- **Map Abstraction**: Unified interface for eBPF map operations
- **Plugin Architecture**: Easy to add new protocols without modifying core

### 1.2 Core eBPF Map Design

Design extensible map structure:

```c
// Core maps (always present)
BPF_MAP_DEF(packet_stats)         // Per-interface packet counters
BPF_MAP_DEF(drop_reasons)         // Debug: why packets dropped
BPF_MAP_DEF(config_map)           // Runtime configuration/feature flags

// Feature maps (conditionally compiled)
BPF_MAP_DEF(srv6_local_sids)      // SRv6 MySID table
BPF_MAP_DEF(srv6_policies)        // SRv6 encap policies
BPF_MAP_DEF(route_stats)          // Per-route statistics
BPF_MAP_DEF(neighbor_cache)       // Optional: custom neighbor cache
```

**Modularity Approach:**
- Use compile-time flags to include/exclude feature maps
- Versioned map structures for backward compatibility
- Clear documentation for map key/value schemas

---

## Phase 2: Basic Data Plane (Weeks 3-4)

### 2.1 XDP Core Pipeline

Implement a modular packet processing pipeline:

```
XDP Program Flow:
┌─────────────────┐
│  XDP Entry      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Parser Layer   │ ◄── Pluggable parsers
│  - Ethernet     │
│  - IPv4/IPv6    │
│  - SRH (future) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Handler Dispatch│ ◄── Protocol-specific handlers
│ (switch/table)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  FIB Lookup     │ ◄── bpf_fib_lookup() wrapper
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Action Layer   │ ◄── XDP_PASS, XDP_REDIRECT, XDP_DROP
└─────────────────┘
```

**Implementation Strategy:**
- **Parser Framework**: Define `struct parser_ctx` that handlers can extend
- **Handler Interface**: Standard function signature for all handlers
- **Return Codes**: Unified error/action code system

Example modular handler:

```c
// Handler function signature
typedef int (*proto_handler_fn)(struct xdp_md *ctx, struct parser_ctx *pctx);

// Handler registry
struct proto_handler {
    __u16 ethertype;
    proto_handler_fn handler;
};

// Handlers can be added without modifying core
static struct proto_handler handlers[] = {
    {ETH_P_IP,   handle_ipv4},
    {ETH_P_IPV6, handle_ipv6},
    // Easy to add: {ETH_P_MPLS, handle_mpls},
};
```

### 2.2 IPv4/IPv6 Forwarding

- Implement `handle_ipv4()` and `handle_ipv6()` using `bpf_fib_lookup()`
- TTL decrement and validation
- L2 rewrite (MAC address update)
- ICMP error generation (punted to kernel)
- Statistics collection

**Modularity:**
- Separate IPv4 and IPv6 into different source files
- Shared common functions (TTL check, checksum update) in `common/`

---

## Phase 3: Control Plane Integration (Weeks 5-6)

### 3.1 Netlink Daemon Architecture

Design a modular control plane daemon:

```
xdp-routerd Components:
├── Main Event Loop (epoll/libbpf)
├── Netlink Subsystem
│   ├── Route Monitor (RTM_NEWROUTE/DELROUTE)
│   ├── Neighbor Monitor (RTM_NEWNEIGH/DELNEIGH)
│   └── Link Monitor (RTM_NEWLINK/DELLINK)
├── BPF Map Manager
│   ├── Map Update API
│   └── Atomic Updates
├── SRv6 Manager (future)
│   └── MySID Table Sync
└── Telemetry/Stats Collector
```

**Key Modularity Features:**
- **Event Handlers**: Register handlers for different Netlink message types
- **Plugin System**: Ability to load protocol-specific control plane modules
- **Configuration**: YAML/JSON config file for feature enablement

Example handler registration:

```c
struct nl_handler {
    int msg_type;
    int (*process)(struct nlmsghdr *nlh, void *ctx);
};

// Easy to extend
static struct nl_handler nl_handlers[] = {
    {RTM_NEWROUTE, process_route_update},
    {RTM_DELROUTE, process_route_delete},
    {RTM_NEWNEIGH, process_neighbor_update},
    // Future: {RTM_NEWSEG6, process_srv6_update},
};
```

### 3.2 FIB Synchronization Strategy

**Decision Point**: When to use `bpf_fib_lookup()` vs. custom maps?

- **Default**: Use `bpf_fib_lookup()` for standard L3 forwarding (zero maintenance)
- **Custom Maps**: Only for features not supported by kernel:
  - SRv6 encapsulation policies
  - Custom QoS/ACL integration
  - Per-route statistics (requires pre-lookup)

**Implementation:**
- Build abstraction layer that tries `bpf_fib_lookup()` first
- Fall back to custom map lookup only for special cases
- Control plane daemon populates custom maps from Netlink events

---

## Phase 4: SRv6 Support (Weeks 7-9)

### 4.1 SRv6 Parser Module

Add SRv6 Routing Header (SRH) parser:

```c
// src/xdp/parsers/srv6.c
int parse_srv6(struct xdp_md *ctx, struct parser_ctx *pctx);

// Modular integration
#ifdef FEATURE_SRV6
    if (nexthdr == IPPROTO_ROUTING) {
        return parse_srv6(ctx, pctx);
    }
#endif
```

**Modularity:**
- Compile-time flag: `FEATURE_SRV6`
- Runtime flag: Check config_map for SRv6 enablement
- Zero overhead when disabled

### 4.2 SRv6 Data Plane Actions

Implement SRv6 behaviors as modular handlers:

```c
// src/xdp/handlers/srv6_end.c
int srv6_end_action(struct xdp_md *ctx, struct srv6_ctx *sctx);

// src/xdp/handlers/srv6_encap.c
int srv6_encap_action(struct xdp_md *ctx, struct srv6_policy *policy);

// src/xdp/handlers/srv6_decap.c
int srv6_decap_action(struct xdp_md *ctx);
```

**SRv6 Lookup Flow:**
1. Check if DA is in `srv6_local_sids` map
2. If match, execute corresponding SRv6 function (End, End.DT4, End.DX6, etc.)
3. If no match, proceed to standard FIB lookup

**Key Design:**
- Each SRv6 function is a separate handler
- Easy to add new SRv6 behaviors (End.DT4, End.DT6, End.DX4, End.DX6)
- SID table managed by control plane daemon

### 4.3 SRv6 Control Plane

Extend Netlink daemon:

```c
// src/control/srv6/srv6_manager.c
- Monitor RTM_NEWSEG6LOCAL for MySID updates
- Parse seg6_local attributes
- Update srv6_local_sids map
- Handle SRv6 policy updates (encap rules)
```

**Integration with FRR:**
- FRR configures SRv6 via `seg6local` iproute2 interface
- Kernel stores in FIB
- xdp-routerd reads via Netlink and programs eBPF maps

---

## Phase 5: IS-IS Integration (Weeks 10-11)

### 5.1 IS-IS Control Plane Handling

**Strategy**: IS-IS is handled entirely by FRR; xdp-router only needs to:
1. **Punt IS-IS packets to kernel**: Detect IS-IS PDUs and return `XDP_PASS`
2. **Forward routes learned via IS-IS**: Already handled by FIB sync

**Implementation:**

```c
// src/xdp/handlers/isis.c
int handle_isis(struct xdp_md *ctx, struct parser_ctx *pctx) {
    // IS-IS uses Ethertype 0x22F3 or LLC/SNAP
    // Always punt to kernel for FRR to process
    return XDP_PASS;
}
```

**Modularity:**
- Add to parser: detect IS-IS by Ethertype or LLC
- Register handler that always punts
- No data plane complexity needed

### 5.2 IS-IS Testing

- Set up FRR with IS-IS enabled
- Verify routes populate kernel FIB
- Verify xdp-router forwards traffic based on IS-IS routes
- Test control plane punt (IS-IS PDUs reach FRR)

---

## Phase 6: Observability & Tooling (Weeks 12-13)

### 6.1 Statistics Collection

Implement comprehensive stats:

```c
struct route_stats {
    __u64 packets;
    __u64 bytes;
    __u64 last_seen;
};

struct srv6_sid_stats {
    __u64 packets;
    __u64 bytes;
    __u64 drops;  // e.g., SL=0 drops
};
```

**Map Design:**
- Per-interface stats (ingress/egress)
- Per-route stats (optional, enabled via config)
- Per-SID stats (for SRv6)
- Drop reason counters

### 6.2 CLI Tool

Build modular CLI:

```bash
xdp-router-cli stats                    # Overall statistics
xdp-router-cli stats --interface eth0   # Per-interface
xdp-router-cli srv6 sids                # Show local SIDs
xdp-router-cli srv6 policies            # Show encap policies
xdp-router-cli debug drop-reasons       # Why packets dropped
xdp-router-cli config set feature srv6 on/off
```

**Implementation:**
- Use `libbpf` to read maps
- Pretty-print tables (consider using `libbpf-tools` style)
- JSON output option for automation

### 6.3 Debugging Tools

```bash
xdp-router-debug trace         # Enable bpf_trace_printk
xdp-router-debug dump-packet   # Dump specific packets to ring buffer
xdp-router-debug verify-fib    # Compare XDP FIB with kernel FIB
```

---

## Phase 7: Performance & Testing (Weeks 14-16)

### 7.1 Performance Benchmarking

**Setup:**
- Use `TRex` or `MoonGen` for packet generation
- Measure MPPS for various packet sizes
- Test with/without SRv6 encapsulation
- Profile using `perf` and eBPF tracing

**Targets (from PRD):**
- >20 MPPS per core baseline
- <5% overhead for SRv6 encap
- <10ms route update latency

### 7.2 Test Suite

**Unit Tests:**
- Parser tests (validate header parsing)
- Handler logic tests (using BPF skeletons)

**Integration Tests:**
- FRR integration (BGP, OSPF, IS-IS)
- SRv6 end-to-end (encap → transit → decap)
- Failover scenarios

**Performance Tests:**
- Throughput benchmarks
- Latency measurements
- Resource utilization

---

## Phase 8: Advanced Features (Weeks 17+)

### 8.1 Hardware Offload Support

- Test with XDP offload mode on supported NICs
- Ensure program fits within NIC eBPF limitations
- Benchmark offload vs. native mode

### 8.2 Additional SRv6 Functions

Add more SRv6 behaviors as modular handlers:
- End.DT4 / End.DT6 (decap + L3 lookup)
- End.DX2 / End.DX4 / End.DX6 (cross-connect)
- End.B6.Encaps (binding SID)

### 8.3 Enhanced Observability

- Integration with Prometheus/Grafana
- BPF ring buffer for packet sampling
- Flow tracking (optional)

---

## Modularity Design Patterns

### 1. Feature Flags

```c
// common/features.h
enum features {
    FEATURE_IPV4        = 1 << 0,
    FEATURE_IPV6        = 1 << 1,
    FEATURE_SRV6        = 1 << 2,
    FEATURE_STATS       = 1 << 3,
    FEATURE_HW_OFFLOAD  = 1 << 4,
};

// Runtime check
if (config->features & FEATURE_SRV6) {
    process_srv6(ctx);
}
```

### 2. Parser Chain

```c
struct parser {
    const char *name;
    int (*parse)(struct xdp_md *ctx, struct parser_ctx *pctx);
    struct parser *next;  // Linked list
};

// Easy to add new parsers
register_parser(&ipv4_parser);
register_parser(&ipv6_parser);
register_parser(&srv6_parser);
register_parser(&mpls_parser);  // Future
```

### 3. Handler Plugins

```c
// Plugin interface
struct xdp_plugin {
    const char *name;
    int version;
    int (*init)(void);
    int (*process)(struct xdp_md *ctx, struct parser_ctx *pctx);
    void (*cleanup)(void);
};

// Plugins can be loaded/unloaded
load_plugin(&srv6_plugin);
load_plugin(&ipsec_plugin);  // Future
```

### 4. Configuration Schema

```yaml
# xdp-router.yaml
features:
  ipv4: true
  ipv6: true
  srv6:
    enabled: true
    behaviors: [End, End.DT4, End.DT6]
  stats:
    per_route: false
    per_sid: true

interfaces:
  - name: eth0
    mode: native  # or offload
  - name: eth1
    mode: native

control_plane:
  netlink:
    buffer_size: 8192
  srv6:
    poll_interval_ms: 100
```

---

## Technology Stack

- **XDP/eBPF**: Kernel 5.10+ (for better BTF and CO-RE support)
- **libbpf**: Modern BPF library (CO-RE, skeletons)
- **FRR**: 8.0+ (for SRv6 support)
- **Netlink**: libnl-3 or raw netlink sockets
- **Build**: Makefile + optional CMake for complex builds
- **Testing**: pytest (integration), BPF selftests (unit)
- **CI/CD**: GitHub Actions with kernel VMs

---

## Dependencies & Prerequisites

1. **Kernel**: Linux 5.10+ with XDP support
2. **Compiler**: clang 10+ with BPF target support
3. **Libraries**:
   - libbpf (>= 0.5)
   - libnl-3
   - libelf
4. **Tools**:
   - bpftool
   - iproute2 with seg6 support
   - FRR (for control plane)

---

## Risk Mitigation

### Technical Risks

1. **bpf_fib_lookup() Limitations**
   - **Mitigation**: Build fallback custom FIB implementation
   - **Decision Point**: Evaluate in Phase 2

2. **SRv6 Performance Overhead**
   - **Mitigation**: Optimize SRH parsing, use XDP offload
   - **Benchmark Early**: Phase 4

3. **Netlink Scalability**
   - **Mitigation**: Batch updates, use efficient data structures
   - **Test**: Large FIB (1M+ routes)

### Operational Risks

1. **FRR Compatibility**
   - **Mitigation**: Document tested FRR versions
   - **CI**: Test against multiple FRR versions

2. **Kernel Version Fragmentation**
   - **Mitigation**: Use CO-RE (Compile Once, Run Everywhere)
   - **Fallback**: Ship precompiled BPF for common kernels

---

## Success Metrics

1. **Performance**: Achieve >20 MPPS on 4-core system
2. **Latency**: <10ms route update propagation
3. **Correctness**: Pass 100% of integration tests with FRR
4. **Modularity**: Add new protocol with <500 lines of code
5. **Documentation**: Complete API docs and user guide

---

## Deliverables

### Phase 1-2
- Project skeleton with build system
- Basic IPv4/IPv6 forwarding
- Initial test suite

### Phase 3-4
- Netlink daemon with FIB sync
- SRv6 End action support
- Performance baseline

### Phase 5-6
- IS-IS integration verified
- CLI tools and observability
- Documentation

### Phase 7-8
- Performance targets met
- Full test coverage
- Hardware offload support (if available)
