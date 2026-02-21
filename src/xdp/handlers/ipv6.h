/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_HANDLER_IPV6_H
#define __XDP_ROUTER_HANDLER_IPV6_H

#include <linux/ipv6.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "common/parser.h"
#include "common/common.h"

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

	/* Validate IP version */
	if (((ip6h->version) & 0xF0) >> 4 != 6)
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

/*
 * Handle IPv6 packet forwarding
 *
 * Performs:
 * 1. Hop limit check and decrement
 * 2. FIB lookup via bpf_fib_lookup()
 * 3. L2 rewrite (MAC addresses)
 * 4. Redirect to output interface
 *
 * Returns: XDP action (PASS, DROP, REDIRECT)
 */
static __always_inline int handle_ipv6(struct xdp_md *ctx, struct parser_ctx *pctx)
{
	struct bpf_fib_lookup fib_params = {};
	struct ipv6hdr *ip6h = pctx->ip6h;
	struct ethhdr *eth = pctx->eth;
	int rc;

	/* Check hop limit */
	if (ip6h->hop_limit <= 1) {
		/* Hop limit expired - let kernel handle ICMPv6 */
		record_drop(ctx->ingress_ifindex, DROP_TTL_EXCEEDED);
		return XDP_PASS;
	}

	/*
	 * If packet has extension headers, pass to kernel for now.
	 * Phase 4 will add SRv6 support with full extension header parsing.
	 */
	if (ip6h->nexthdr == IPPROTO_ROUTING ||
	    ip6h->nexthdr == IPPROTO_HOPOPTS ||
	    ip6h->nexthdr == IPPROTO_DSTOPTS ||
	    ip6h->nexthdr == IPPROTO_FRAGMENT) {
		return XDP_PASS;
	}

	/* Set up FIB lookup parameters */
	fib_params.family = AF_INET6;
	fib_params.flowinfo = *(__be32 *)ip6h & IPV6_FLOWINFO_MASK;
	fib_params.l4_protocol = ip6h->nexthdr;
	fib_params.sport = 0;
	fib_params.dport = 0;
	fib_params.tot_len = bpf_ntohs(ip6h->payload_len);
	__builtin_memcpy(&fib_params.ipv6_src, &ip6h->saddr,
			 sizeof(fib_params.ipv6_src));
	__builtin_memcpy(&fib_params.ipv6_dst, &ip6h->daddr,
			 sizeof(fib_params.ipv6_dst));
	fib_params.ifindex = ctx->ingress_ifindex;

	/* Perform FIB lookup */
	rc = bpf_fib_lookup(ctx, &fib_params, sizeof(fib_params), 0);

	switch (rc) {
	case BPF_FIB_LKUP_RET_SUCCESS:
		/* Decrement hop limit (no checksum in IPv6) */
		ip6h->hop_limit--;

		/* Update L2 addresses */
		__builtin_memcpy(eth->h_dest, fib_params.dmac, ETH_ALEN);
		__builtin_memcpy(eth->h_source, fib_params.smac, ETH_ALEN);

		/* Update statistics - inlined to reduce map lookups */
		{
			struct if_stats *stats;
			__u64 pkt_len = pctx->data_end - pctx->data;

			/* Ingress stats */
			stats = bpf_map_lookup_elem(&packet_stats,
						    &ctx->ingress_ifindex);
			if (stats) {
				stats->rx_packets++;
				stats->rx_bytes += pkt_len;
			}

			/* Egress stats */
			stats = bpf_map_lookup_elem(&packet_stats,
						    &fib_params.ifindex);
			if (stats) {
				stats->tx_packets++;
				stats->tx_bytes += pkt_len;
			}
		}

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

#endif /* __XDP_ROUTER_HANDLER_IPV6_H */
