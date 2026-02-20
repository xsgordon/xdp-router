/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_MAPS_H
#define __XDP_ROUTER_MAPS_H

#include <linux/types.h>
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
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct xdp_config);
} config_map SEC(".maps");

/* Helper: Update packet statistics */
static __always_inline void update_stats(__u32 ifindex, __u64 bytes, bool rx)
{
	struct if_stats *stats;

	stats = bpf_map_lookup_elem(&packet_stats, &ifindex);
	if (!stats)
		return;

	if (rx) {
		__sync_fetch_and_add(&stats->rx_packets, 1);
		__sync_fetch_and_add(&stats->rx_bytes, bytes);
	} else {
		__sync_fetch_and_add(&stats->tx_packets, 1);
		__sync_fetch_and_add(&stats->tx_bytes, bytes);
	}
}

/* Helper: Record drop reason */
static __always_inline void record_drop(__u32 ifindex, enum drop_reason reason)
{
	__u64 *count;
	__u32 key = reason;
	struct if_stats *stats;

	/* Update drop counter */
	count = bpf_map_lookup_elem(&drop_stats, &key);
	if (count)
		__sync_fetch_and_add(count, 1);

	/* Update interface drop counter */
	stats = bpf_map_lookup_elem(&packet_stats, &ifindex);
	if (stats)
		__sync_fetch_and_add(&stats->dropped, 1);
}

/* Helper: Get runtime configuration */
static __always_inline struct xdp_config *get_config(void)
{
	__u32 key = 0;
	return bpf_map_lookup_elem(&config_map, &key);
}

#endif /* __XDP_ROUTER_MAPS_H */
