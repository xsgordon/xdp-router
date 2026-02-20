// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include "common/common.h"
#include "common/parser.h"

char LICENSE[] SEC("license") = "GPL";

/* BPF maps - will be populated in Phase 1 */

/* Main XDP program entry point */
SEC("xdp")
int xdp_router_main(struct xdp_md *ctx)
{
	/* This is a placeholder that will be implemented in Phase 2 */
	/* Current behavior: pass all packets to kernel */
	return XDP_PASS;
}
