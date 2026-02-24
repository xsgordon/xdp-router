// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * Integration tests for XDP attach/detach functionality
 *
 * These tests verify that the XDP program can be correctly attached to
 * and detached from network interfaces. This would have caught the
 * XDP API misuse bug found during manual testing.
 *
 * Note: These tests require root privileges or CAP_NET_ADMIN + CAP_BPF.
 */

/* clang-format off */
#include <arpa/inet.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/ip.h>
#include <net/if.h>

#include "test_harness.h"
#include "../common/packet_builder.h"
/* clang-format on */

/* XDP action return codes */
#ifndef XDP_ABORTED
#define XDP_ABORTED 0
#endif
#ifndef XDP_DROP
#define XDP_DROP 1
#endif
#ifndef XDP_PASS
#define XDP_PASS 2
#endif
#ifndef XDP_TX
#define XDP_TX 3
#endif
#ifndef XDP_REDIRECT
#define XDP_REDIRECT 4
#endif

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* XDP attach/detach wrappers (same as in CLI) */
#if LIBBPF_MAJOR_VERSION >= 1
static int xdp_attach_wrapper(int ifindex, int prog_fd, __u32 flags)
{
	return bpf_xdp_attach(ifindex, prog_fd, flags, NULL);
}

static int xdp_detach_wrapper(int ifindex)
{
	return bpf_xdp_detach(ifindex, 0, NULL);
}
#else
static int xdp_attach_wrapper(int ifindex, int prog_fd, __u32 flags)
{
	return bpf_set_link_xdp_fd(ifindex, prog_fd, flags);
}

static int xdp_detach_wrapper(int ifindex)
{
	return bpf_set_link_xdp_fd(ifindex, -1, 0);
}
#endif

/*
 * Test: Attach XDP program to loopback interface
 *
 * This is the core functionality test that would have caught
 * the XDP API bug (using bpf_prog_attach instead of bpf_xdp_attach).
 */
static int test_attach_to_loopback(void)
{
	struct bpf_test_ctx ctx;
	int ifindex, err;
	__u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_SKB_MODE;

	/* Check for required privileges */
	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Get loopback interface index */
	ifindex = if_nametoindex("lo");
	ASSERT_GT(ifindex, 0, "Loopback interface should exist");

	/* Attach XDP program */
	err = xdp_attach_wrapper(ifindex, ctx.prog_fd, xdp_flags);
	ASSERT_EQ(err, 0, "Should attach to loopback (err=%d)", err);

	/* Detach for cleanup */
	xdp_detach_wrapper(ifindex);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Detach XDP program from interface
 */
static int test_detach_from_loopback(void)
{
	struct bpf_test_ctx ctx;
	int ifindex, err;
	__u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_SKB_MODE;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	ifindex = if_nametoindex("lo");
	ASSERT_GT(ifindex, 0, "Loopback interface should exist");

	/* Attach first */
	err = xdp_attach_wrapper(ifindex, ctx.prog_fd, xdp_flags);
	ASSERT_EQ(err, 0, "Should attach to loopback");

	/* Detach */
	err = xdp_detach_wrapper(ifindex);
	ASSERT_EQ(err, 0, "Should detach from loopback (err=%d)", err);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Double attach should fail with UPDATE_IF_NOEXIST flag
 */
static int test_double_attach_fails(void)
{
	struct bpf_test_ctx ctx;
	int ifindex, err;
	__u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_SKB_MODE;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	ifindex = if_nametoindex("lo");
	ASSERT_GT(ifindex, 0, "Loopback interface should exist");

	/* First attach should succeed */
	err = xdp_attach_wrapper(ifindex, ctx.prog_fd, xdp_flags);
	ASSERT_EQ(err, 0, "First attach should succeed");

	/* Second attach should fail */
	err = xdp_attach_wrapper(ifindex, ctx.prog_fd, xdp_flags);
	ASSERT_NE(err, 0, "Second attach should fail with UPDATE_IF_NOEXIST");

	/* Cleanup */
	xdp_detach_wrapper(ifindex);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Attach to invalid interface should fail
 */
static int test_attach_invalid_interface(void)
{
	struct bpf_test_ctx ctx;
	int err;
	__u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_SKB_MODE;
	int invalid_ifindex = 99999; /* Very unlikely to exist */

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Attach to invalid interface should fail */
	err = xdp_attach_wrapper(invalid_ifindex, ctx.prog_fd, xdp_flags);
	ASSERT_NE(err, 0, "Attach to invalid interface should fail");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Detach from interface with no program should succeed or be harmless
 */
static int test_detach_when_not_attached(void)
{
	int ifindex;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	ifindex = if_nametoindex("lo");
	ASSERT_GT(ifindex, 0, "Loopback interface should exist");

	/* Detach when nothing is attached (should not crash) */
	/* This may return 0 or -ENOENT, either is acceptable */
	/* We're mainly testing it doesn't crash or cause issues */
	(void)xdp_detach_wrapper(ifindex);

	TEST_PASS();
}

/*
 * Test: Attach and verify maps are accessible
 */
static int test_maps_accessible_after_attach(void)
{
	struct bpf_test_ctx ctx;
	struct xdp_config cfg;
	int ifindex, err;
	__u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_SKB_MODE;
	__u32 key = 0;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	ifindex = if_nametoindex("lo");
	ASSERT_GT(ifindex, 0, "Loopback interface should exist");

	/* Attach */
	err = xdp_attach_wrapper(ifindex, ctx.prog_fd, xdp_flags);
	ASSERT_EQ(err, 0, "Should attach to loopback");

	/* Verify we can read config map */
	err = bpf_map_lookup_elem(ctx.config_fd, &key, &cfg);
	ASSERT_EQ(err, 0, "Should be able to read config map after attach");
	ASSERT_EQ(cfg.version, XDP_ROUTER_MAP_VERSION, "Version should match");

	/* Cleanup */
	xdp_detach_wrapper(ifindex);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Attach and process packet via bpf_prog_test_run
 *
 * This tests that after attaching, the program can still be tested
 * via bpf_prog_test_run(). Note: bpf_prog_test_run() doesn't update
 * interface stats because it doesn't set ingress_ifindex properly.
 */
static int test_attach_and_process_packet(void)
{
	struct bpf_test_ctx ctx;
	struct test_packet pkt;
	uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	uint8_t dst_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
	int ifindex, err;
	__u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_SKB_MODE;
	__u32 ret_val, duration;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	ifindex = if_nametoindex("lo");
	ASSERT_GT(ifindex, 0, "Loopback interface should exist");

	/* Attach */
	err = xdp_attach_wrapper(ifindex, ctx.prog_fd, xdp_flags);
	ASSERT_EQ(err, 0, "Should attach to loopback");

	/* Process a packet via bpf_prog_test_run */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"), inet_addr("192.168.1.2"), IPPROTO_ICMP,
	                  64);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Packet processing should succeed");

	/* Verify packet was processed with a valid XDP action */
	/* XDP_ABORTED=0, XDP_DROP=1, XDP_PASS=2, XDP_TX=3, XDP_REDIRECT=4 */
	ASSERT(ret_val <= XDP_REDIRECT, "Should return valid XDP action (got %u, expected 0-4)",
	       ret_val);

	/* Cleanup */
	xdp_detach_wrapper(ifindex);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

int main(void)
{
	printf("\n");
	printf("=== XDP Router Integration Tests: Attach/Detach ===\n");
	printf("\n");

	/* Core attach/detach tests */
	RUN_TEST(test_attach_to_loopback);
	RUN_TEST(test_detach_from_loopback);

	/* Error case tests */
	RUN_TEST(test_double_attach_fails);
	RUN_TEST(test_attach_invalid_interface);
	RUN_TEST(test_detach_when_not_attached);

	/* Functional tests */
	RUN_TEST(test_maps_accessible_after_attach);
	RUN_TEST(test_attach_and_process_packet);

	PRINT_TEST_SUMMARY();
}
