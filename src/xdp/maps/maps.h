/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_MAPS_H
#define __XDP_ROUTER_MAPS_H

#include <linux/types.h>
#include <bpf/bpf_helpers.h>

#include "common/common.h"

/* Maximum map sizes */
#define MAX_INTERFACES		256
#define MAX_DROP_REASONS	64

/*
 * Packet statistics per interface
 * Key: interface index (u32)
 * Value: struct if_stats
 */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, MAX_INTERFACES);
	__type(key, __u32);
	__type(value, struct if_stats);
} packet_stats SEC(".maps");

/*
 * Drop reason counters for debugging
 * Key: drop_reason enum value (u32)
 * Value: counter (u64)
 */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, MAX_DROP_REASONS);
	__type(key, __u32);
	__type(value, __u64);
} drop_stats SEC(".maps");

/*
 * Runtime configuration
 * Key: 0 (singleton)
 * Value: struct xdp_config
 *
 * Default: All features enabled
 * User-space can update this map to enable/disable features at runtime
 * without reloading the BPF program.
 *
 * Example (user-space):
 *   struct xdp_config cfg = {
 *       .features = FEATURE_IPV4_BIT | FEATURE_IPV6_BIT,
 *   };
 *   bpf_map_update_elem(config_map_fd, &key, &cfg, BPF_ANY);
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct xdp_config);
} config_map SEC(".maps");

/* Helper: Update packet statistics */
static __always_inline void update_stats(__u32 ifindex, __u64 bytes, int rx)
{
	struct if_stats *stats;

	stats = bpf_map_lookup_elem(&packet_stats, &ifindex);
	if (!stats)
		return;

	/*
	 * PERCPU maps: each CPU has its own copy, no concurrent access.
	 * Simple increments are correct and much faster than atomics.
	 * User-space aggregates across CPUs when reading.
	 *
	 * Use saturating arithmetic to prevent overflow wrap-around.
	 * While unlikely, counters could overflow on high-speed interfaces
	 * over long uptimes. Saturation is better than wrapping to 0.
	 */
	if (rx) {
		if (stats->rx_packets < UINT64_MAX)
			stats->rx_packets++;
		if (stats->rx_bytes < UINT64_MAX - bytes)
			stats->rx_bytes += bytes;
		else
			stats->rx_bytes = UINT64_MAX;
	} else {
		if (stats->tx_packets < UINT64_MAX)
			stats->tx_packets++;
		if (stats->tx_bytes < UINT64_MAX - bytes)
			stats->tx_bytes += bytes;
		else
			stats->tx_bytes = UINT64_MAX;
	}
}

/* Helper: Record drop reason */
static __always_inline void record_drop(__u32 ifindex, enum drop_reason reason)
{
	__u64 *count;
	__u32 key = reason;
	struct if_stats *stats;

	/*
	 * Update drop counter (PERCPU map, no atomics needed)
	 * Use saturating increment to prevent overflow.
	 */
	count = bpf_map_lookup_elem(&drop_stats, &key);
	if (count && *count < UINT64_MAX)
		(*count)++;

	/*
	 * Update interface drop counter (PERCPU map, no atomics needed)
	 * Use saturating increment to prevent overflow.
	 */
	stats = bpf_map_lookup_elem(&packet_stats, &ifindex);
	if (stats && stats->dropped < UINT64_MAX)
		stats->dropped++;
}

/* Helper: Get runtime configuration */
static __always_inline struct xdp_config *get_config(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&config_map, &key);
}

#endif /* __XDP_ROUTER_MAPS_H */
