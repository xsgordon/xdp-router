# xdp-router Architecture

## Table of Contents

1. [System Overview](#system-overview)
2. [Component Architecture](#component-architecture)
3. [Data Flow](#data-flow)
4. [XDP Data Plane](#xdp-data-plane)
5. [Control Plane](#control-plane)
6. [eBPF Maps](#ebpf-maps)
7. [Modularity & Extensibility](#modularity--extensibility)
8. [Performance Considerations](#performance-considerations)
9. [Security Considerations](#security-considerations)

---

## System Overview

xdp-router is a hybrid routing architecture that combines:
- **Linux Kernel Control Plane**: FRR populates the kernel FIB via standard Netlink
- **XDP Data Plane**: eBPF programs perform fast-path packet forwarding at the NIC driver layer
- **User-Space Control Daemon**: Synchronizes kernel state to eBPF maps for advanced features

### Design Philosophy

1. **Kernel-Native**: Leverage existing Linux networking infrastructure
2. **Modular**: Easy to add new protocols and features
3. **Observable**: Comprehensive statistics and debugging
4. **Performant**: Target >20 MPPS per core
5. **Standard Tooling**: Works with standard Linux tools (ip, tcpdump, ping)

### Comparison with DPDK Approach

| Aspect | DPDK (e.g., Grout) | xdp-router |
|--------|-------------------|------------|
| Kernel Integration | Bypass kernel entirely | Cooperate with kernel |
| NIC Management | DPDK PMD drivers | Standard Linux drivers |
| Routing Table | Custom implementation | Kernel FIB + eBPF maps |
| Control Plane | Custom or complex integration | FRR via Netlink |
| Tooling | DPDK-specific tools | Standard Linux tools |
| Operational Complexity | High | Low |

---

## Component Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────┐    ┌──────────────┐    ┌──────────────┐          │
│  │   FRR    │    │ xdp-routerd  │    │ xdp-router-  │          │
│  │          │    │   (daemon)   │    │     cli      │          │
│  │ BGP/OSPF │    │              │    │              │          │
│  │  IS-IS   │    │  - Netlink   │    │  - Stats     │          │
│  │  SRv6    │    │  - Map Mgmt  │    │  - Debug     │          │
│  └────┬─────┘    └──────┬───────┘    └──────┬───────┘          │
│       │                 │                    │                   │
├───────┼─────────────────┼────────────────────┼───────────────────┤
│       │                 │                    │                   │
│       ▼                 ▼                    ▼                   │
│  ┌─────────────────────────────────────────────────┐            │
│  │          Kernel Space (Linux Kernel)            │            │
│  │                                                  │            │
│  │  ┌──────────────┐      ┌─────────────────────┐ │            │
│  │  │  Routing     │      │   eBPF Maps         │ │            │
│  │  │  Table (FIB) │      │                     │ │            │
│  │  │              │      │  - srv6_local_sids  │ │            │
│  │  │  - IPv4 LPM  │      │  - srv6_policies    │ │            │
│  │  │  - IPv6 LPM  │      │  - packet_stats     │ │            │
│  │  │  - SRv6 SIDs │      │  - config_map       │ │            │
│  │  └──────────────┘      └─────────────────────┘ │            │
│  │                                                  │            │
│  │  ┌────────────────────────────────────────────┐ │            │
│  │  │      Network Stack (skb path)              │ │            │
│  │  │  - Used for control plane (BGP, IS-IS)     │ │            │
│  │  │  - Fallback for punted packets             │ │            │
│  │  └────────────────────────────────────────────┘ │            │
│  │                                                  │            │
│  │  ┌────────────────────────────────────────────┐ │            │
│  │  │      XDP Hook (NIC Driver Layer)           │ │            │
│  │  │                                             │ │            │
│  │  │  ┌───────────────────────────────────────┐ │ │            │
│  │  │  │      xdp-router BPF Program           │ │ │            │
│  │  │  │                                        │ │ │            │
│  │  │  │  1. Parse (Eth, IP, SRH)              │ │ │            │
│  │  │  │  2. Classify (IPv4/IPv6/SRv6)         │ │ │            │
│  │  │  │  3. Lookup (bpf_fib_lookup/maps)      │ │ │            │
│  │  │  │  4. Action (REDIRECT/PASS/DROP)       │ │ │            │
│  │  │  │                                        │ │ │            │
│  │  │  └───────────────────────────────────────┘ │ │            │
│  │  └────────────────────────────────────────────┘ │            │
│  └─────────────────────────────────────────────────┘            │
│                           │                                      │
└───────────────────────────┼──────────────────────────────────────┘
                            │
                            ▼
                  ┌──────────────────┐
                  │   NIC Hardware   │
                  │                  │
                  │  (XDP Offload    │
                  │   capable)       │
                  └──────────────────┘
```

---

## Data Flow

### Packet Ingress Flow

```
Packet Arrives → NIC → XDP Hook
                         │
                         ▼
                    ┌─────────┐
                    │  Parse  │
                    │ Headers │
                    └────┬────┘
                         │
                         ▼
              ┌──────────────────────┐
              │  Protocol Classify   │
              │  (IPv4/IPv6/IS-IS)   │
              └──────────┬───────────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
        ▼                ▼                ▼
   ┌─────────┐     ┌──────────┐    ┌──────────┐
   │ Control │     │   IPv4   │    │   IPv6   │
   │ Plane   │     │ Forwarding│    │Forwarding│
   │ Packet? │     └────┬─────┘    └────┬─────┘
   └────┬────┘          │               │
        │               │               │
        │               ▼               ▼
        │        ┌──────────────────────────┐
        │        │   SRv6 Lookup?           │
        │        │   (Check DA in MySID)    │
        │        └──────┬───────────────────┘
        │               │
        │      ┌────────┴─────────┐
        │      │                  │
        │      ▼                  ▼
        │  ┌───────┐        ┌──────────┐
        │  │ SRv6  │        │   FIB    │
        │  │ End   │        │  Lookup  │
        │  │Action │        │(bpf_fib_ │
        │  └───┬───┘        │ lookup)  │
        │      │            └────┬─────┘
        │      │                 │
        │      └────────┬────────┘
        │               │
        ▼               ▼
   ┌────────┐    ┌──────────┐
   │ XDP_   │    │  XDP_    │
   │ PASS   │    │REDIRECT  │
   └───┬────┘    └────┬─────┘
       │              │
       ▼              ▼
    Kernel       Egress NIC
     Stack
```

### Control Plane Flow

```
FRR (BGP/OSPF/IS-IS)
       │
       │ Netlink (RTM_NEWROUTE, etc.)
       ▼
  Kernel FIB
       │
       │ Netlink (multicast groups)
       ▼
  xdp-routerd
       │
       ├─► Read FIB via bpf_fib_lookup() ──► No action needed
       │                                     (kernel handles it)
       │
       └─► Custom features (SRv6)
           │
           ├─► Parse RTM_NEWSEG6LOCAL
           ├─► Extract SID, action
           └─► Update srv6_local_sids map
                      │
                      ▼
               eBPF Map in Kernel
                      │
                      ▼
               XDP Program reads map
```

---

## XDP Data Plane

### Packet Processing Pipeline

The XDP program is structured as a modular pipeline:

#### 1. Parser Layer

```c
struct parser_ctx {
    void *data;
    void *data_end;

    struct ethhdr *eth;
    struct iphdr *iph;
    struct ipv6hdr *ip6h;
    struct ipv6_sr_hdr *srh;

    __u16 ethertype;
    __u8 protocol;
    __u16 l3_offset;
    __u16 l4_offset;
};

// Parsers return:
// 0 = success, continue pipeline
// <0 = error, drop packet
// XDP_PASS/DROP/REDIRECT = take action immediately
```

**Modular Parsers:**
- `parse_ethernet()` - Always runs first
- `parse_ipv4()` - If ethertype == ETH_P_IP
- `parse_ipv6()` - If ethertype == ETH_P_IPV6
- `parse_srv6()` - If IPv6 nexthdr == IPPROTO_ROUTING
- `parse_isis()` - If ethertype == ETH_P_ISIS

#### 2. Handler Dispatch

```c
// Handler registry - easy to extend
static int (*handlers[])(struct xdp_md *, struct parser_ctx *) = {
    [PROTO_IPV4] = handle_ipv4,
    [PROTO_IPV6] = handle_ipv6,
    [PROTO_ISIS] = handle_isis,
    [PROTO_SRV6] = handle_srv6,
};

int dispatch(struct xdp_md *ctx, struct parser_ctx *pctx) {
    if (pctx->proto < MAX_PROTO && handlers[pctx->proto])
        return handlers[pctx->proto](ctx, pctx);
    return XDP_PASS;
}
```

#### 3. Lookup Layer

Two lookup mechanisms:

**A. Kernel FIB Lookup (Default)**
```c
int fib_lookup(struct xdp_md *ctx, struct parser_ctx *pctx) {
    struct bpf_fib_lookup fib_params = {};

    // Fill in src/dst IP, ifindex, etc.
    int rc = bpf_fib_lookup(ctx, &fib_params, sizeof(fib_params), 0);

    if (rc == BPF_FIB_LKUP_RET_SUCCESS) {
        // Update MACs, decrement TTL
        return XDP_REDIRECT;
    }
    return XDP_PASS;  // Let kernel handle
}
```

**B. Custom Map Lookup (SRv6, etc.)**
```c
int srv6_lookup(struct parser_ctx *pctx) {
    struct srv6_sid_key key = {
        .sid = pctx->ip6h->daddr,
    };

    struct srv6_sid_action *action = bpf_map_lookup_elem(&srv6_local_sids, &key);
    if (action) {
        return action->handler(ctx, pctx, action);
    }
    return -1;  // Not found
}
```

#### 4. Action Layer

```c
enum xdp_action {
    XDP_ABORTED = 0,   // Error, drop
    XDP_DROP,          // Intentional drop
    XDP_PASS,          // Send to kernel
    XDP_TX,            // Bounce back same interface
    XDP_REDIRECT,      // Send to different interface
};
```

### Feature Flags

Compile-time and runtime feature control:

```c
// Compile-time (Makefile)
#ifdef FEATURE_SRV6
    #include "handlers/srv6.h"
#endif

// Runtime (config_map)
struct config {
    __u32 features;  // Bitmap of enabled features
};

if (config->features & FEATURE_SRV6_BIT) {
    srv6_lookup(pctx);
}
```

---

## Control Plane

### xdp-routerd Daemon

The daemon runs in user-space and performs:

1. **Netlink Monitoring**: Subscribe to kernel routing events
2. **Map Synchronization**: Update eBPF maps based on kernel state
3. **Configuration Management**: Read config files, expose control API
4. **Telemetry**: Export statistics to monitoring systems

#### Architecture

```c
struct xdp_routerd {
    int netlink_fd;         // Netlink socket
    int bpf_fd;             // BPF map file descriptors
    struct event_loop *el;  // epoll-based event loop

    struct nl_handler *nl_handlers;    // Netlink message handlers
    struct map_manager *map_mgr;       // BPF map management
    struct srv6_manager *srv6_mgr;     // SRv6-specific logic
    struct stats_collector *stats;     // Telemetry
};
```

#### Main Event Loop

```c
while (running) {
    epoll_wait(epfd, events, MAX_EVENTS, timeout);

    for each event:
        if (event.fd == netlink_fd)
            process_netlink_event();
        else if (event.fd == signal_fd)
            handle_signal();
        else if (event.fd == timer_fd)
            collect_statistics();
}
```

#### Netlink Handler Example

```c
int process_route_update(struct nlmsghdr *nlh, void *ctx) {
    struct rtmsg *rtm = NLMSG_DATA(nlh);

    // Parse route attributes
    parse_route_attributes(nlh, &route);

    // Check if this is a SRv6 route
    if (route.encap_type == LWTUNNEL_ENCAP_SEG6) {
        return srv6_manager_add_policy(srv6_mgr, &route);
    }

    // Standard routes handled by kernel FIB
    // No action needed for bpf_fib_lookup()
    return 0;
}
```

### FRR Integration

xdp-router is **passive** with respect to FRR:

1. FRR runs independently, using kernel's routing stack
2. FRR populates kernel FIB via standard Netlink
3. xdp-router observes FIB changes via Netlink multicast
4. For standard routes: XDP uses `bpf_fib_lookup()` (zero config)
5. For SRv6/advanced features: xdp-routerd programs eBPF maps

**No modifications to FRR required!**

---

## eBPF Maps

### Core Maps

#### 1. packet_stats

```c
struct packet_stats {
    __u64 rx_packets;
    __u64 rx_bytes;
    __u64 tx_packets;
    __u64 tx_bytes;
    __u64 dropped;
};

// Key: interface index
// Value: struct packet_stats
BPF_MAP_DEF(packet_stats, BPF_MAP_TYPE_PERCPU_ARRAY, __u32, struct packet_stats, MAX_IFACES);
```

#### 2. drop_reasons

```c
enum drop_reason {
    DROP_INVALID_PACKET = 1,
    DROP_TTL_EXCEEDED,
    DROP_NO_ROUTE,
    DROP_SRV6_SL_EXCEEDED,
    // ...
};

// Key: drop_reason
// Value: count
BPF_MAP_DEF(drop_reasons, BPF_MAP_TYPE_PERCPU_ARRAY, __u32, __u64, MAX_DROP_REASONS);
```

#### 3. config_map

```c
struct xdp_config {
    __u32 features;          // Feature bitmap
    __u32 log_level;         // Debug verbosity
    __u32 max_srv6_sids;     // Limits
};

// Key: 0 (singleton)
// Value: struct xdp_config
BPF_MAP_DEF(config_map, BPF_MAP_TYPE_ARRAY, __u32, struct xdp_config, 1);
```

### SRv6 Maps

#### 4. srv6_local_sids

```c
struct srv6_sid_key {
    struct in6_addr sid;
};

struct srv6_sid_action {
    __u32 action;  // End, End.DT4, End.DX6, etc.
    __u32 table;   // For DT functions
    __u32 oif;     // For DX functions
};

// Key: struct srv6_sid_key
// Value: struct srv6_sid_action
BPF_MAP_DEF(srv6_local_sids, BPF_MAP_TYPE_HASH,
            struct srv6_sid_key, struct srv6_sid_action, MAX_SIDS);
```

#### 5. srv6_policies

```c
struct srv6_policy_key {
    struct in6_addr dst_prefix;
    __u32 prefix_len;
};

struct srv6_policy {
    __u32 num_segments;
    struct in6_addr segments[MAX_SEGMENTS];
};

// Key: struct srv6_policy_key
// Value: struct srv6_policy
BPF_MAP_DEF(srv6_policies, BPF_MAP_TYPE_LPM_TRIE,
            struct srv6_policy_key, struct srv6_policy, MAX_POLICIES);
```

### Map Update Strategy

- **Atomic Updates**: Use map-in-map for atomic swap during updates
- **Batch Updates**: Use `bpf_map_update_batch()` for bulk updates
- **Versioning**: Include version field to detect stale reads

---

## Modularity & Extensibility

### Adding a New Protocol

Example: Adding MPLS support

**1. Create Parser**

```c
// src/xdp/parsers/mpls.c
int parse_mpls(struct xdp_md *ctx, struct parser_ctx *pctx) {
    // Parse MPLS header
    // Update pctx->protocol = PROTO_MPLS
    return 0;
}
```

**2. Create Handler**

```c
// src/xdp/handlers/mpls.c
int handle_mpls(struct xdp_md *ctx, struct parser_ctx *pctx) {
    // Perform MPLS label lookup
    // Pop/swap/push labels
    return XDP_REDIRECT;
}
```

**3. Register Handler**

```c
// src/xdp/core/main.c
#ifdef FEATURE_MPLS
    #include "parsers/mpls.h"
    #include "handlers/mpls.h"
    handlers[PROTO_MPLS] = handle_mpls;
#endif
```

**4. Add Control Plane Support** (if needed)

```c
// src/control/mpls/mpls_manager.c
int process_mpls_route(struct nlmsghdr *nlh) {
    // Parse MPLS route from Netlink
    // Update MPLS map
}
```

**Total**: ~500 lines of code for a new protocol!

### Plugin System (Future)

```c
// Plugin interface
struct xdp_plugin {
    const char *name;
    int version;

    // Lifecycle
    int (*init)(struct xdp_router *router);
    void (*cleanup)(void);

    // Packet processing
    int (*process)(struct xdp_md *ctx, struct parser_ctx *pctx);

    // Control plane
    int (*handle_netlink)(struct nlmsghdr *nlh);
};

// Plugin registration
register_plugin(&mpls_plugin);
register_plugin(&ipsec_plugin);
```

---

## Performance Considerations

### Design Decisions for Performance

1. **Early Filtering**: Drop/redirect packets at XDP layer before skb allocation
2. **Per-CPU Maps**: Use `BPF_MAP_TYPE_PERCPU_*` to avoid lock contention
3. **Inline Functions**: Mark critical functions `__always_inline`
4. **Loop Unrolling**: Use `#pragma unroll` for bounded loops
5. **Minimal Branching**: Reduce branch mispredictions in hot path

### Critical Path Optimization

```c
// Fast path: Common case (IPv4, route in FIB)
SEC("xdp")
int xdp_router_main(struct xdp_md *ctx) {
    // Bounds check once
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // Parse Ethernet (inline, ~10 instructions)
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_DROP;

    // Fast path: IPv4
    if (eth->h_proto == bpf_htons(ETH_P_IP)) {
        return handle_ipv4_fast(ctx, eth, data_end);
    }

    // Slow path: Other protocols
    return handle_generic(ctx);
}
```

### Memory Access Patterns

- **Sequential Access**: Parse headers sequentially (cache-friendly)
- **Prefetch**: Use `__builtin_prefetch()` for predictable accesses
- **Alignment**: Ensure structures are properly aligned

### XDP Modes

| Mode | Description | Performance | Use Case |
|------|-------------|-------------|----------|
| Generic | Run XDP in kernel after skb allocation | Slow | Testing only |
| Native | Run XDP in NIC driver before skb | Fast | Production |
| Offload | Run XDP on NIC hardware | Fastest | High performance |

### Performance Monitoring

```c
// Lightweight performance counters
struct perf_stats {
    __u64 cycles_total;
    __u64 packets_processed;
    __u64 cache_misses;  // Via perf events
};
```

---

## Security Considerations

### eBPF Program Safety

1. **Verifier Compliance**: All BPF programs must pass kernel verifier
2. **Bounds Checking**: Explicit checks before all pointer dereferences
3. **Loop Bounds**: All loops must be bounded (or use bpf_loop)
4. **No Unbounded Recursion**: Avoid deep call chains

### Map Access Control

```c
// Restrict map access to privileged users
// Set in control daemon
bpf_obj_get_info_by_fd(map_fd, &info, &info_len);
// Validate permissions before updates
```

### Attack Mitigation

1. **DDoS Protection**: Rate limiting via maps
2. **Packet Validation**: Drop malformed packets early
3. **Resource Limits**: Bounds on map sizes, SID counts
4. **Audit Logging**: Log suspicious activity

### Secure Boot Integration

```bash
# Sign BPF programs for secure boot environments
llvm-strip -g xdp_router.o
bpftool prog load xdp_router.o /sys/fs/bpf/xdp_router \
    type xdp pinmaps /sys/fs/bpf/maps
```

---

## Deployment Model

### Single Host Deployment

```
┌──────────────────────────┐
│    Host (Linux Server)   │
│                          │
│  FRR → Kernel FIB        │
│         ↓                │
│  xdp-routerd → eBPF Maps │
│         ↓                │
│  XDP Program (eth0)      │
└──────────────────────────┘
```

### Multi-Interface Deployment

```
┌────────────────────────────────┐
│    Host (Router/Firewall)      │
│                                │
│  eth0 (XDP) ←→ XDP Program     │
│  eth1 (XDP) ←→ XDP Program     │
│  eth2 (XDP) ←→ XDP Program     │
│                                │
│  Shared eBPF Maps (all ifaces) │
└────────────────────────────────┘
```

### Service Provider Fabric

```
            ┌─────────┐
            │  TOR    │ (xdp-router + IS-IS)
            └────┬────┘
                 │
        ┌────────┼────────┐
        │        │        │
   ┌────┴───┐ ┌─┴────┐ ┌─┴────┐
   │ Spine1 │ │Spine2│ │Spine3│ (xdp-router + BGP + SRv6)
   └────────┘ └──────┘ └──────┘
```

---

## Future Enhancements

1. **P4-style Pipeline**: Table-based forwarding with programmable actions
2. **Flow Caching**: Short-term flow cache for connection tracking
3. **QoS Integration**: Traffic shaping in XDP
4. **Telemetry**: Streaming telemetry via ring buffers
5. **Control Plane API**: gRPC/REST API for programmatic control
6. **Multi-Tenancy**: VRF support, network namespaces
7. **Encryption**: IPsec/WireGuard acceleration in XDP

---

## References

- [XDP Tutorial](https://github.com/xdp-project/xdp-tutorial)
- [Linux Kernel XDP Documentation](https://www.kernel.org/doc/html/latest/networking/xdp.html)
- [libbpf Documentation](https://libbpf.readthedocs.io/)
- [FRR Documentation](https://docs.frrouting.org/)
- [SRv6 Architecture](https://datatracker.ietf.org/doc/html/rfc8402)
- [IS-IS Protocol](https://datatracker.ietf.org/doc/html/rfc1142)
