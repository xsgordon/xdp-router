/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_HANDLER_IPV4_H
#define __XDP_ROUTER_HANDLER_IPV4_H

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "common/parser.h"
#include "common/common.h"
#include "xdp/maps/maps.h"
#include "xdp/parsers/ipv4.h"

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

	/*
	 * Check TTL.
	 * TTL <= 1 means packet should not be forwarded. Pass to kernel
	 * to generate ICMP Time Exceeded message. Don't count as "dropped"
	 * since packet is passed to kernel stack, not dropped.
	 */
	if (iph->ttl <= 1)
		return XDP_PASS;

	/*
	 * Pass fragmented packets to kernel for reassembly.
	 * We don't want to forward fragments in XDP because:
	 * 1. Can't determine L4 ports from non-first fragments
	 * 2. Reassembly is complex and not suitable for XDP
	 * 3. Kernel handles this correctly already
	 */
	if (pctx->is_fragment)
		return XDP_PASS;

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
		/*
		 * Validate returned interface index.
		 * While kernel should return valid values, defensive programming
		 * requires validation to prevent undefined behavior.
		 */
		if (fib_params.ifindex == 0 || fib_params.ifindex >= MAX_INTERFACES) {
			record_drop(ctx->ingress_ifindex, DROP_INVALID_PACKET);
			return XDP_DROP;
		}

		/* Decrement TTL and update checksum */
		old_ttl = iph->ttl;
		iph->ttl--;
		update_ipv4_checksum(iph, old_ttl, iph->ttl);

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

#endif /* __XDP_ROUTER_HANDLER_IPV4_H */
