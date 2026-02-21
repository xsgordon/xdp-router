/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_HANDLER_IPV6_H
#define __XDP_ROUTER_HANDLER_IPV6_H

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "common/parser.h"
#include "common/common.h"
#include "xdp/maps/maps.h"
#include "xdp/parsers/ipv6.h"

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

	/*
	 * Check hop limit.
	 * Hop limit <= 1 means packet should not be forwarded. Pass to kernel
	 * to generate ICMPv6 Time Exceeded message. Don't count as "dropped"
	 * since packet is passed to kernel stack, not dropped.
	 */
	if (ip6h->hop_limit <= 1)
		return XDP_PASS;

	/*
	 * Pass packets with extension headers or special protocols to kernel.
	 * This includes:
	 * - Extension headers (routing, hop-by-hop, destination options, fragments)
	 * - IPsec (AH, ESP) - must be processed by kernel IPsec stack
	 * - ICMPv6 - needed for Neighbor Discovery, Router Advertisement, etc.
	 * - Mobility Header - for Mobile IPv6
	 *
	 * Phase 4 will add SRv6 support with selective extension header parsing.
	 */
	switch (ip6h->nexthdr) {
	case IPPROTO_ROUTING:
	case IPPROTO_HOPOPTS:
	case IPPROTO_DSTOPTS:
	case IPPROTO_FRAGMENT:
	case IPPROTO_AH:	/* IPsec Authentication Header */
	case IPPROTO_ESP:	/* IPsec Encapsulating Security Payload */
	case IPPROTO_ICMPV6:	/* ICMPv6 (ND, RA, etc.) */
	case IPPROTO_MH:	/* Mobility Header */
		return XDP_PASS;
	}

	/* Set up FIB lookup parameters */
	fib_params.family = AF_INET6;

	/*
	 * Extract flow info with safe unaligned access.
	 * IPv6 header may not be 4-byte aligned (e.g., after 14-byte Ethernet
	 * header). Direct cast to __be32* causes alignment faults on ARM/MIPS.
	 * Use memcpy for portable, safe access.
	 */
	{
		__be32 first_word;
		__builtin_memcpy(&first_word, ip6h, sizeof(first_word));
		fib_params.flowinfo = first_word & IPV6_FLOWINFO_MASK;
	}

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
		/*
		 * Validate returned interface index.
		 * While kernel should return valid values, defensive programming
		 * requires validation to prevent undefined behavior.
		 */
		if (fib_params.ifindex == 0 || fib_params.ifindex >= MAX_INTERFACES) {
			record_drop(ctx->ingress_ifindex, DROP_INVALID_PACKET);
			return XDP_DROP;
		}

		/* Decrement hop limit (no checksum in IPv6) */
		ip6h->hop_limit--;

		/* Update L2 addresses */
		__builtin_memcpy(eth->h_dest, fib_params.dmac, ETH_ALEN);
		__builtin_memcpy(eth->h_source, fib_params.smac, ETH_ALEN);

		/*
		 * Paranoid bounds check after modifications.
		 * In theory not needed since we didn't change packet size,
		 * but good practice for production BPF programs.
		 */
		if ((void *)(eth + 1) > (void *)(long)ctx->data_end)
			return XDP_ABORTED;

		/* Update statistics with saturating arithmetic */
		{
			struct if_stats *stats;
			__u64 pkt_len;

			pkt_len = pctx->data_end - pctx->data;

			/*
			 * Sanity check: cap at jumbo frame size to prevent
			 * statistics corruption from kernel bugs.
			 */
			if (pkt_len > 9000)
				pkt_len = 9000;

			/* Ingress stats with saturation */
			{
				__u32 ingress_if = ctx->ingress_ifindex;
				stats = bpf_map_lookup_elem(&packet_stats, &ingress_if);
				if (stats) {
					if (stats->rx_packets < UINT64_MAX)
						stats->rx_packets++;
					if (stats->rx_bytes < UINT64_MAX - pkt_len)
						stats->rx_bytes += pkt_len;
					else
						stats->rx_bytes = UINT64_MAX;
				}
			}

			/* Egress stats with saturation */
			stats = bpf_map_lookup_elem(&packet_stats,
						    &fib_params.ifindex);
			if (stats) {
				if (stats->tx_packets < UINT64_MAX)
					stats->tx_packets++;
				if (stats->tx_bytes < UINT64_MAX - pkt_len)
					stats->tx_bytes += pkt_len;
				else
					stats->tx_bytes = UINT64_MAX;
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
