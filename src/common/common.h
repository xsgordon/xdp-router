/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_COMMON_H
#define __XDP_ROUTER_COMMON_H

#include <linux/types.h>
#include <linux/if_vlan.h>
#include <bpf/bpf_endian.h>

/* BPF doesn't have standard library, define constants we need */
#ifndef UINT64_MAX
#define UINT64_MAX ((__u64)-1)
#endif

#ifndef AF_INET
#define AF_INET 2
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

#ifndef IPV6_FLOWINFO_MASK
#define IPV6_FLOWINFO_MASK bpf_htonl(0x0FFFFFFF)
#endif

/* Project version */
#define XDP_ROUTER_VERSION_MAJOR 0
#define XDP_ROUTER_VERSION_MINOR 1
#define XDP_ROUTER_VERSION_PATCH 0

/*
 * BPF Map API Version
 *
 * Format: 0xMMmmpppp (M=major, m=minor, p=patch)
 * Example: 0.1.0 = 0x00010000
 *
 * MUST increment when:
 * - Map key/value structure changes (MAJOR)
 * - Map added/removed (MINOR)
 * - Map semantics change (MINOR)
 */
#define XDP_ROUTER_MAP_VERSION \
	((XDP_ROUTER_VERSION_MAJOR << 24) | \
	 (XDP_ROUTER_VERSION_MINOR << 16) | \
	 (XDP_ROUTER_VERSION_PATCH))

/* Feature flags - compile time */
#ifdef FEATURE_IPV4
#define HAS_IPV4 1
#else
#define HAS_IPV4 0
#endif

#ifdef FEATURE_IPV6
#define HAS_IPV6 1
#else
#define HAS_IPV6 0
#endif

#ifdef FEATURE_SRV6
#define HAS_SRV6 1
#else
#define HAS_SRV6 0
#endif

/* Feature flags - runtime bitmap */
enum xdp_router_features {
	FEATURE_IPV4_BIT = 1 << 0,
	FEATURE_IPV6_BIT = 1 << 1,
	FEATURE_SRV6_BIT = 1 << 2,
	FEATURE_STATS_BIT = 1 << 3,
	FEATURE_HW_OFFLOAD_BIT = 1 << 4,
};

/* Protocol types for handler dispatch */
enum protocol_type {
	PROTO_UNKNOWN = 0,
	PROTO_IPV4,
	PROTO_IPV6,
	PROTO_SRV6,
	PROTO_ISIS,
	PROTO_ARP,
	PROTO_MAX,
};

/* Drop reasons for debugging */
enum drop_reason {
	DROP_NONE = 0,
	DROP_INVALID_PACKET,
	DROP_TTL_EXCEEDED,
	DROP_NO_ROUTE,
	DROP_PARSE_ERROR,
	DROP_SRV6_SL_EXCEEDED,
	DROP_CHECKSUM_ERROR,
	DROP_MAX,
};

/*
 * Configuration structure
 *
 * IMPORTANT: Version field must be first for compatibility checking.
 * CLI tools MUST verify version matches before accessing other fields.
 */
struct xdp_config {
	__u32 version;		/* Map API version (XDP_ROUTER_MAP_VERSION) */
	__u32 features;		/* Feature bitmap */
	__u32 log_level;	/* Debug verbosity */
	__u32 max_srv6_sids;	/* SRv6 SID limit */
};

/* Per-interface statistics */
struct if_stats {
	__u64 rx_packets;
	__u64 rx_bytes;
	__u64 tx_packets;
	__u64 tx_bytes;
	__u64 dropped;
	__u64 errors;
};

/* Common return codes */
#define XDP_ROUTER_OK 0
#define XDP_ROUTER_ERROR -1
#define XDP_ROUTER_EINVAL -2
#define XDP_ROUTER_ENOMEM -3
#define XDP_ROUTER_ENOTFOUND -4

/* Maximum supported interfaces */
#define MAX_INTERFACES 256

/* Maximum drop reasons */
#define MAX_DROP_REASONS 64

/* Packet size limits */
#define MAX_JUMBO_FRAME_SIZE 9000	/* Maximum jumbo frame size for stats sanity check */
#define MAX_VLAN_TAGS 2			/* Maximum VLAN tags to parse (prevent attacks) */

#ifndef __stringify
#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)
#endif

#endif /* __XDP_ROUTER_COMMON_H */
