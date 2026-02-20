/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 */

#ifndef __XDP_ROUTER_PARSER_H
#define __XDP_ROUTER_PARSER_H

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include "common.h"

/* Parser context - shared across all handlers */
struct parser_ctx {
	/* Packet boundaries */
	void *data;
	void *data_end;

	/* Parsed headers (pointers into packet) */
	struct ethhdr *eth;
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	void *l4;

	/* Metadata */
	__u16 ethertype;
	__u8 protocol;		/* IPPROTO_* or PROTO_* */
	__u8 ttl;

	/* Offsets from data */
	__u16 l3_offset;
	__u16 l4_offset;

	/* Flags */
	__u8 is_ipv4:1;
	__u8 is_ipv6:1;
	__u8 is_srv6:1;
	__u8 is_fragment:1;
	__u8 reserved:4;
};

/* Parser function signature */
typedef int (*parser_fn)(void *ctx, struct parser_ctx *pctx);

/* Helper macros for bounds checking */
#define PARSER_CHECK_BOUNDS(pctx, ptr, size) \
	((void *)((ptr) + (size)) <= (pctx)->data_end)

#define PARSER_ADVANCE(pctx, ptr, size) \
	do { \
		if (!PARSER_CHECK_BOUNDS(pctx, ptr, size)) \
			return -1; \
		ptr += size; \
	} while (0)

#endif /* __XDP_ROUTER_PARSER_H */
