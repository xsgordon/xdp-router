// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include "common/common.h"
#include "common/parser.h"

/* Include maps and handlers */
#include "xdp/maps/maps.h"
#include "xdp/parsers/ethernet.h"
#include "xdp/handlers/ipv4.h"
#include "xdp/handlers/ipv6.h"

char LICENSE[] SEC("license") = "GPL";

/* Main XDP program entry point */
SEC("xdp")
int xdp_router_main(struct xdp_md *ctx)
{
	struct xdp_config cfg_local = {0};
	struct xdp_config *cfg;
	struct parser_ctx pctx = {};
	int rc;

	/*
	 * Get runtime configuration and copy to stack atomically.
	 * This prevents TOCTOU race conditions where config could be
	 * modified by user-space between checks.
	 *
	 * Use bpf_probe_read() instead of struct assignment to ensure
	 * atomic copy. Simple assignment (*cfg) may be compiled into
	 * multiple load instructions, creating a TOCTOU window.
	 */
	cfg = get_config();
	if (cfg) {
		if (bpf_probe_read(&cfg_local, sizeof(cfg_local), cfg) != 0) {
			/* Read failed - use safe defaults */
			cfg_local.features = FEATURE_IPV4_BIT;
		}
	} else {
		/* No config: fail-closed with minimal features */
		cfg_local.features = FEATURE_IPV4_BIT;
	}

	/* Initialize parser context */
	pctx.data = (void *)(long)ctx->data;
	pctx.data_end = (void *)(long)ctx->data_end;

	/*
	 * Paranoid validation: verify kernel invariant data_end >= data.
	 * Must be checked BEFORE any use of these pointers.
	 * If this ever fails, it indicates a severe kernel bug.
	 */
	if (pctx.data_end < pctx.data)
		return XDP_ABORTED;

	/*
	 * Validate ingress interface index.
	 * We validate FIB-returned ifindex, so we should also validate
	 * kernel-provided ingress ifindex for consistency.
	 */
	if (ctx->ingress_ifindex >= MAX_INTERFACES)
		return XDP_ABORTED;

	/* Parse Ethernet header */
	rc = parse_ethernet(&pctx);
	if (rc < 0) {
		record_drop(ctx->ingress_ifindex, DROP_PARSE_ERROR);
		return XDP_DROP;
	}

	/*
	 * Pass multicast and broadcast frames to kernel.
	 * Multicast/broadcast bit is the LSB of the first octet.
	 * This includes control plane traffic (routing protocols, ARP, etc.)
	 */
	if (pctx.eth->h_dest[0] & 0x01)
		return XDP_PASS;

	/* Dispatch based on EtherType */
	switch (pctx.ethertype) {
	case ETH_P_IP:
		/* Check if IPv4 is enabled (runtime config) */
		if (!(cfg_local.features & FEATURE_IPV4_BIT))
			return XDP_PASS;

		/* Parse IPv4 header */
		rc = parse_ipv4(&pctx);
		if (rc < 0) {
			record_drop(ctx->ingress_ifindex, DROP_PARSE_ERROR);
			return XDP_DROP;
		}

		/* Handle IPv4 forwarding */
		return handle_ipv4(ctx, &pctx);

	case ETH_P_IPV6:
		/* Check if IPv6 is enabled (runtime config) */
		if (!(cfg_local.features & FEATURE_IPV6_BIT))
			return XDP_PASS;

		/* Parse IPv6 header */
		rc = parse_ipv6(&pctx);
		if (rc < 0) {
			record_drop(ctx->ingress_ifindex, DROP_PARSE_ERROR);
			return XDP_DROP;
		}

		/* Handle IPv6 forwarding */
		return handle_ipv6(ctx, &pctx);

	case ETH_P_ARP:
		/* Pass ARP to kernel */
		return XDP_PASS;

	default:
		/* Unknown protocol - pass to kernel */
		return XDP_PASS;
	}
}
