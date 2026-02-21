# BPF Maps Reference

This document describes the BPF maps used by xdp-router for statistics, configuration, and debugging.

## Overview

xdp-router uses three BPF maps to provide runtime statistics, configuration, and debugging capabilities:

- **packet_stats**: Per-interface packet and byte counters
- **drop_stats**: Drop reason counters for debugging
- **config_map**: Runtime feature configuration

All maps can be accessed from user-space using `bpftool` or the libbpf API.

---

## packet_stats

**Purpose**: Track packet and byte counters per network interface.

**Type**: `BPF_MAP_TYPE_PERCPU_ARRAY`

**Key**: Interface index (`u32`)

**Value**: `struct if_stats`
```c
struct if_stats {
    __u64 rx_packets;  /* Received packets */
    __u64 rx_bytes;    /* Received bytes */
    __u64 tx_packets;  /* Transmitted packets (forwarded) */
    __u64 tx_bytes;    /* Transmitted bytes */
    __u64 dropped;     /* Dropped packets */
    __u64 errors;      /* Processing errors */
};
```

**Max Entries**: 256 interfaces

**Performance Note**: This is a PERCPU map, meaning each CPU core maintains its own copy of the statistics. User-space tools must aggregate values across all CPUs to get total counts. This design eliminates the need for expensive atomic operations in the fast path, improving performance by 10-20%.

### Usage Examples

**View all statistics:**
```bash
sudo bpftool map dump name packet_stats
```

**View statistics for a specific interface (e.g., eth0 with ifindex 2):**
```bash
# Get interface index
ip link show eth0 | grep -oP '(?<=^)\d+'

# Dump map and filter
sudo bpftool map dump name packet_stats | grep -A 8 "key: 2"
```

**Read from user-space (C):**
```c
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

struct if_stats {
    __u64 rx_packets;
    __u64 rx_bytes;
    __u64 tx_packets;
    __u64 tx_bytes;
    __u64 dropped;
    __u64 errors;
};

int map_fd = bpf_map__fd(skel->maps.packet_stats);
__u32 ifindex = 2; /* eth0 */
struct if_stats stats[MAX_CPUS];
struct if_stats total = {0};

/* Read PERCPU values */
if (bpf_map_lookup_elem(map_fd, &ifindex, stats) == 0) {
    /* Aggregate across CPUs */
    for (int cpu = 0; cpu < nr_cpus; cpu++) {
        total.rx_packets += stats[cpu].rx_packets;
        total.rx_bytes += stats[cpu].rx_bytes;
        total.tx_packets += stats[cpu].tx_packets;
        total.tx_bytes += stats[cpu].tx_bytes;
        total.dropped += stats[cpu].dropped;
        total.errors += stats[cpu].errors;
    }

    printf("Interface %u:\n", ifindex);
    printf("  RX: %llu packets, %llu bytes\n",
           total.rx_packets, total.rx_bytes);
    printf("  TX: %llu packets, %llu bytes\n",
           total.tx_packets, total.tx_bytes);
    printf("  Dropped: %llu, Errors: %llu\n",
           total.dropped, total.errors);
}
```

---

## drop_stats

**Purpose**: Track packet drop counts by reason for debugging and monitoring.

**Type**: `BPF_MAP_TYPE_PERCPU_ARRAY`

**Key**: Drop reason (`u32`, see `enum drop_reason` below)

**Value**: Counter (`u64`)

**Max Entries**: 64 drop reasons

**Drop Reasons:**
```c
enum drop_reason {
    DROP_NONE = 0,             /* No drop (not used) */
    DROP_INVALID_PACKET,       /* Malformed packet */
    DROP_TTL_EXCEEDED,         /* TTL/hop limit reached 0 */
    DROP_NO_ROUTE,             /* FIB lookup failed */
    DROP_PARSE_ERROR,          /* Failed to parse headers */
    DROP_SRV6_SL_EXCEEDED,     /* SRv6 segment list exceeded */
    DROP_CHECKSUM_ERROR,       /* Invalid checksum */
    DROP_MAX,                  /* Sentinel value */
};
```

### Usage Examples

**View all drop statistics:**
```bash
sudo bpftool map dump name drop_stats
```

**Monitor specific drop reason:**
```bash
# Watch for TTL exceeded drops (reason 2)
watch -n 1 'sudo bpftool map dump name drop_stats | grep -A 2 "key: 2"'
```

**Read from user-space (C):**
```c
int map_fd = bpf_map__fd(skel->maps.drop_stats);

/* Read TTL exceeded drops */
__u32 reason = DROP_TTL_EXCEEDED;
__u64 counts[MAX_CPUS];
__u64 total = 0;

if (bpf_map_lookup_elem(map_fd, &reason, counts) == 0) {
    for (int cpu = 0; cpu < nr_cpus; cpu++) {
        total += counts[cpu];
    }
    printf("TTL exceeded drops: %llu\n", total);
}
```

**Correlation with packet_stats:**

Drop counters provide detailed reasons, while `packet_stats.dropped` provides per-interface totals. Use both together for comprehensive debugging:

```bash
# Total drops across all interfaces
sudo bpftool map dump name packet_stats | grep dropped

# Break down by reason
sudo bpftool map dump name drop_stats
```

---

## config_map

**Purpose**: Runtime feature configuration without reloading the BPF program.

**Type**: `BPF_MAP_TYPE_ARRAY`

**Key**: 0 (singleton - only one config entry)

**Value**: `struct xdp_config`
```c
struct xdp_config {
    __u32 features;        /* Feature bitmap */
    __u32 log_level;       /* Debug verbosity (future use) */
    __u32 max_srv6_sids;   /* SRv6 SID limit (future use) */
    __u32 reserved;        /* Reserved for future use */
};
```

**Max Entries**: 1 (singleton)

**Feature Flags:**
```c
enum xdp_router_features {
    FEATURE_IPV4_BIT       = 1 << 0,  /* IPv4 forwarding */
    FEATURE_IPV6_BIT       = 1 << 1,  /* IPv6 forwarding */
    FEATURE_SRV6_BIT       = 1 << 2,  /* SRv6 processing (future) */
    FEATURE_STATS_BIT      = 1 << 3,  /* Statistics collection (future) */
    FEATURE_HW_OFFLOAD_BIT = 1 << 4,  /* Hardware offload hints (future) */
};
```

**Default Configuration**: All features enabled (features = 0xFFFFFFFF)

### Usage Examples

**View current configuration:**
```bash
sudo bpftool map dump name config_map
```

**Disable IPv6 at runtime:**
```bash
# Create config with only IPv4 enabled
printf '\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' | \
    sudo bpftool map update name config_map key 0 0 0 0 value hex -
```

**Update from user-space (C):**
```c
int map_fd = bpf_map__fd(skel->maps.config_map);
__u32 key = 0;

/* Read current config */
struct xdp_config cfg;
if (bpf_map_lookup_elem(map_fd, &key, &cfg) != 0) {
    /* Initialize with defaults if not found */
    cfg.features = FEATURE_IPV4_BIT | FEATURE_IPV6_BIT;
    cfg.log_level = 0;
    cfg.max_srv6_sids = 16;
    cfg.reserved = 0;
}

/* Disable IPv6 */
cfg.features &= ~FEATURE_IPV6_BIT;

/* Update map */
if (bpf_map_update_elem(map_fd, &key, &cfg, BPF_ANY) != 0) {
    perror("Failed to update config_map");
    return -1;
}

printf("Configuration updated. IPv6 disabled.\n");
```

**Enable feature at runtime:**
```c
/* Enable SRv6 processing */
cfg.features |= FEATURE_SRV6_BIT;
bpf_map_update_elem(map_fd, &key, &cfg, BPF_ANY);
```

**Check if feature is enabled (BPF side):**
```c
/* This is done automatically in main.c: */
struct xdp_config *cfg = get_config();
if (cfg && !(cfg->features & FEATURE_IPV4_BIT))
    return XDP_PASS;  /* IPv4 disabled, pass to kernel */
```

---

## Best Practices

### Statistics Collection

1. **Aggregation**: Always aggregate PERCPU map values across all CPUs for accurate totals.

2. **Polling Interval**: For monitoring, poll statistics every 1-5 seconds. More frequent polling provides better precision but increases overhead.

3. **Rate Calculation**: Calculate packet/byte rates by comparing samples:
   ```python
   pps = (packets_now - packets_prev) / time_delta
   bps = (bytes_now - bytes_prev) / time_delta
   ```

### Configuration Management

1. **Atomic Updates**: Update the entire `xdp_config` structure atomically to avoid inconsistent states.

2. **Validation**: Validate configuration before updating:
   ```c
   if (cfg.features == 0) {
       fprintf(stderr, "Warning: All features disabled\n");
   }
   ```

3. **Persistence**: Configuration is volatile and lost on program reload. User-space daemons should restore configuration after reload.

### Debugging

1. **Monitor drop_stats**: High drop counts indicate configuration or network issues:
   - `DROP_TTL_EXCEEDED`: Routing loops or high hop counts
   - `DROP_NO_ROUTE`: FIB not populated or routes missing
   - `DROP_PARSE_ERROR`: Malformed packets or parser bugs

2. **Compare interfaces**: Use `packet_stats` to identify which interfaces have traffic issues.

3. **Baseline measurements**: Collect baseline statistics under normal operation for comparison during troubleshooting.

---

## Map Pinning

Maps are not pinned to `/sys/fs/bpf` by default. The control plane daemon (xdp-routerd) handles map lifecycle.

**Manual pinning** (for development/debugging):
```bash
# Pin all maps
sudo bpftool prog load build/xdp_router.bpf.o /sys/fs/bpf/xdp_router \
    type xdp pinmaps /sys/fs/bpf/xdp_router

# Access pinned maps
sudo bpftool map show pinned /sys/fs/bpf/xdp_router/packet_stats
```

**Unpinning:**
```bash
sudo rm -rf /sys/fs/bpf/xdp_router
```

---

## Future Enhancements

The following map enhancements are planned for future phases:

- **Phase 4**: Add `srv6_cache` map for SRv6 segment list caching
- **Phase 5**: Add `route_cache` map for frequently accessed routes
- **Phase 6**: Add `interface_config` map for per-interface settings
- **Phase 7**: Add performance counters (cycles, cache misses)

See [PLAN.md](../PLAN.md) for the complete roadmap.

---

## Related Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture overview
- [PLAN.md](../PLAN.md) - Implementation roadmap
- [bpftool documentation](https://manpages.debian.org/testing/linux-manual-4.8/bpftool.8.en.html)
- [libbpf documentation](https://libbpf.readthedocs.io/)
