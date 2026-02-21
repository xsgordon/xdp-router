/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __XDP_ROUTER_PARSER_IPV4_H
#define __XDP_ROUTER_PARSER_IPV4_H

#include <linux/ip.h>
#include <bpf/bpf_endian.h>

#include "common/parser.h"

/* IPv4 fragment flags and offset mask (in network byte order) */
#define IP_DF		0x4000	/* Don't Fragment flag */
#define IP_MF		0x2000	/* More Fragments flag */
#define IP_OFFSET	0x1FFF	/* Fragment offset mask */

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

	/*
	 * Bounds check for full header (including options)
	 * Check arithmetic bounds before pointer arithmetic to prevent overflow
	 */
	__u32 hdr_len = iph->ihl * 4;
	if (hdr_len > (pctx->data_end - l3_start))
		return -1;

	pctx->iph = iph;
	pctx->protocol = iph->protocol;
	pctx->ttl = iph->ttl;
	pctx->is_ipv4 = 1;
	pctx->l4_offset = pctx->l3_offset + hdr_len;

	/*
	 * Validate fragment flags.
	 * DF (Don't Fragment) and MF (More Fragments) are mutually exclusive
	 * per RFC 791. This combination is illegal and indicates:
	 * - Malformed packet
	 * - Network fuzzing/scanning
	 * - Potential attack attempt
	 */
	{
		__u16 frag = bpf_ntohs(iph->frag_off);

		if ((frag & IP_DF) && (frag & IP_MF)) {
			/* Illegal flag combination */
			return -1;
		}

		/* Check for fragments (offset != 0 or MF flag set) */
		if (frag & (IP_OFFSET | IP_MF))
			pctx->is_fragment = 1;
	}

	return 0;
}

#endif /* __XDP_ROUTER_PARSER_IPV4_H */
