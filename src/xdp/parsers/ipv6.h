/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_PARSER_IPV6_H
#define __XDP_ROUTER_PARSER_IPV6_H

#include <linux/ipv6.h>
#include <bpf/bpf_endian.h>

#include "common/parser.h"

/*
 * Parse IPv6 header
 *
 * Extracts IPv6 base header and validates basic fields.
 * Note: Does NOT parse extension headers in this phase.
 *
 * Returns: 0 on success, -1 on parse error
 */
static __always_inline int parse_ipv6(struct parser_ctx *pctx)
{
	struct ipv6hdr *ip6h;
	void *l3_start = pctx->data + pctx->l3_offset;

	/* Bounds check for IPv6 header */
	ip6h = l3_start;
	if ((void *)(ip6h + 1) > pctx->data_end)
		return -1;

	/*
	 * Validate IP version
	 * The version field is in the high nibble of the first byte.
	 * We must read it directly from the packet as struct layout varies.
	 */
	__u8 version = (*(__u8 *)ip6h & 0xF0) >> 4;
	if (version != 6)
		return -1;

	pctx->ip6h = ip6h;
	pctx->protocol = ip6h->nexthdr;
	pctx->ttl = ip6h->hop_limit;
	pctx->is_ipv6 = 1;
	pctx->l4_offset = pctx->l3_offset + sizeof(*ip6h);

	/*
	 * Note: Extension header parsing not implemented in Phase 2.
	 * For packets with extension headers, we pass to kernel.
	 * Phase 4 (SRv6) will add full extension header parsing.
	 */

	return 0;
}

#endif /* __XDP_ROUTER_PARSER_IPV6_H */
