/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_PARSER_ETHERNET_H
#define __XDP_ROUTER_PARSER_ETHERNET_H

#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <bpf/bpf_endian.h>

#include "common/parser.h"

/*
 * Parse Ethernet header
 *
 * Extracts Ethernet header and handles VLAN tags.
 * Updates parser context with:
 * - eth: pointer to Ethernet header
 * - ethertype: EtherType (network byte order)
 * - l3_offset: offset to L3 header
 *
 * Returns: 0 on success, -1 on parse error
 */
static __always_inline int parse_ethernet(struct parser_ctx *pctx)
{
	struct ethhdr *eth = pctx->data;
	struct vlan_hdr *vlan;
	__u16 proto;
	void *next_hdr;

	/* Bounds check for Ethernet header */
	if ((void *)(eth + 1) > pctx->data_end)
		return -1;

	pctx->eth = eth;
	proto = eth->h_proto;
	next_hdr = (void *)(eth + 1);
	pctx->l3_offset = sizeof(*eth);

	/* Handle VLAN tags (support single and double tagging) */
	#pragma unroll
	for (int i = 0; i < 2; i++) {
		if (proto != bpf_htons(ETH_P_8021Q) &&
		    proto != bpf_htons(ETH_P_8021AD))
			break;

		vlan = next_hdr;
		if ((void *)(vlan + 1) > pctx->data_end)
			return -1;

		proto = vlan->h_vlan_encapsulated_proto;
		next_hdr = (void *)(vlan + 1);
		pctx->l3_offset += sizeof(*vlan);
	}

	pctx->ethertype = bpf_ntohs(proto);
	return 0;
}

#endif /* __XDP_ROUTER_PARSER_ETHERNET_H */
