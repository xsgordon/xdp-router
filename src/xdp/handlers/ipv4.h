/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_HANDLER_IPV4_H
#define __XDP_ROUTER_HANDLER_IPV4_H

#include <linux/ip.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "common/parser.h"
#include "common/common.h"

/*
 * Parse IPv4 header
 *
 * Extracts IPv4 header and validates basic fields.
 * Updates parser context with IPv4 header pointer and metadata.
 *
 * Returns: 0 on success, -1 on parse error
 */
static __always_inline int parse_ipv4(struct parser_ctx *pctx)
{
	struct iphdr *iph;
	void *l3_start = pctx->data + pctx->l3_offset;

	/* Bounds check for IPv4 header */
	iph = l3_start;
	if ((void *)(iph + 1) > pctx->data_end)
		return -1;

	/* Validate IP version */
	if (iph->version != 4)
		return -1;

	/* Validate header length */
	if (iph->ihl < 5)
		return -1;

	/* Bounds check for full header (including options) */
	if (l3_start + (iph->ihl * 4) > pctx->data_end)
		return -1;

	pctx->iph = iph;
	pctx->protocol = iph->protocol;
	pctx->ttl = iph->ttl;
	pctx->is_ipv4 = 1;
	pctx->l4_offset = pctx->l3_offset + (iph->ihl * 4);

	/* Check for fragments */
	if (bpf_ntohs(iph->frag_off) & (0x1FFF | 0x2000))
		pctx->is_fragment = 1;

	return 0;
}

/*
 * Update IPv4 checksum after TTL decrement
 *
 * Uses incremental checksum update (RFC 1624) instead of full recalculation.
 * This is much faster: ~HC' = ~HC + ~m + m'
 *
 * old_ttl: original TTL value
 * new_ttl: new TTL value (old_ttl - 1)
 */
static __always_inline void update_ipv4_checksum(struct iphdr *iph,
						  __u8 old_ttl, __u8 new_ttl)
{
	__u32 csum = ~bpf_ntohs(iph->check);

	/* Add old value */
	csum += ~((__u32)old_ttl << 8) & 0xFFFF;
	/* Add new value */
	csum += ((__u32)new_ttl << 8) & 0xFFFF;

	/* Fold carries */
	csum = (csum & 0xFFFF) + (csum >> 16);
	csum = (csum & 0xFFFF) + (csum >> 16);

	iph->check = bpf_htons((__u16)~csum);
}

/*
 * Handle IPv4 packet forwarding
 *
 * Performs:
 * 1. TTL check and decrement
 * 2. FIB lookup via bpf_fib_lookup()
 * 3. L2 rewrite (MAC addresses)
 * 4. Redirect to output interface
 *
 * Returns: XDP action (PASS, DROP, REDIRECT)
 */
static __always_inline int handle_ipv4(struct xdp_md *ctx, struct parser_ctx *pctx)
{
	struct bpf_fib_lookup fib_params = {};
	struct iphdr *iph = pctx->iph;
	struct ethhdr *eth = pctx->eth;
	__u8 old_ttl;
	int rc;

	/* Check TTL */
	if (iph->ttl <= 1) {
		/* TTL expired - let kernel handle ICMP */
		record_drop(ctx->ingress_ifindex, DROP_TTL_EXCEEDED);
		return XDP_PASS;
	}

	/* Set up FIB lookup parameters */
	fib_params.family = AF_INET;
	fib_params.tos = iph->tos;
	fib_params.l4_protocol = iph->protocol;
	fib_params.sport = 0;
	fib_params.dport = 0;
	fib_params.tot_len = bpf_ntohs(iph->tot_len);
	fib_params.ipv4_src = iph->saddr;
	fib_params.ipv4_dst = iph->daddr;
	fib_params.ifindex = ctx->ingress_ifindex;

	/* Perform FIB lookup */
	rc = bpf_fib_lookup(ctx, &fib_params, sizeof(fib_params), 0);

	switch (rc) {
	case BPF_FIB_LKUP_RET_SUCCESS:
		/* Decrement TTL and update checksum */
		old_ttl = iph->ttl;
		iph->ttl--;
		update_ipv4_checksum(iph, old_ttl, iph->ttl);

		/* Update L2 addresses */
		__builtin_memcpy(eth->h_dest, fib_params.dmac, ETH_ALEN);
		__builtin_memcpy(eth->h_source, fib_params.smac, ETH_ALEN);

		/* Update statistics */
		update_stats(ctx->ingress_ifindex,
			     pctx->data_end - pctx->data, true);
		update_stats(fib_params.ifindex,
			     pctx->data_end - pctx->data, false);

		/* Redirect to output interface */
		return bpf_redirect(fib_params.ifindex, 0);

	case BPF_FIB_LKUP_RET_BLACKHOLE:
	case BPF_FIB_LKUP_RET_UNREACHABLE:
	case BPF_FIB_LKUP_RET_PROHIBIT:
		/* Drop packet */
		record_drop(ctx->ingress_ifindex, DROP_NO_ROUTE);
		return XDP_DROP;

	case BPF_FIB_LKUP_RET_NOT_FWDED:
	case BPF_FIB_LKUP_RET_FWD_DISABLED:
	case BPF_FIB_LKUP_RET_UNSUPP_LWT:
	case BPF_FIB_LKUP_RET_NO_NEIGH:
	case BPF_FIB_LKUP_RET_FRAG_NEEDED:
	default:
		/* Pass to kernel for handling */
		return XDP_PASS;
	}
}

#endif /* __XDP_ROUTER_HANDLER_IPV4_H */
