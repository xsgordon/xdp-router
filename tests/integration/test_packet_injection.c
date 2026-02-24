// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * Integration tests for XDP packet processing using bpf_prog_test_run()
 *
 * These tests load the actual BPF program and execute it on test packets
 * without requiring attachment to a real network interface.
 */

#include <arpa/inet.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include "test_harness.h"
#include "../common/packet_builder.h"

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

/* Helper MAC addresses */
static uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static uint8_t dst_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
static uint8_t mcast_mac[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x01};
static uint8_t bcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/* Helper IPv6 addresses */
static uint8_t ipv6_src[16] = {
	0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};
static uint8_t ipv6_dst[16] = {
	0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};

/*
 * Test: BPF program loads successfully
 */
static int test_bpf_load(void)
{
	struct bpf_test_ctx ctx;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load successfully");

	ASSERT_GT(ctx.prog_fd, 0, "Program FD should be valid");
	ASSERT_GT(ctx.stats_fd, 0, "Stats map FD should be valid");
	ASSERT_GT(ctx.drop_stats_fd, 0, "Drop stats map FD should be valid");
	ASSERT_GT(ctx.config_fd, 0, "Config map FD should be valid");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Empty packet is dropped
 *
 * Note: bpf_prog_test_run() rejects packets < 14 bytes with -EINVAL.
 * This test verifies the kernel's validation, not our XDP program.
 */
static int test_empty_packet(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	unsigned char pkt[1] = {0};
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* bpf_prog_test_run() should reject empty packets */
	err = run_xdp_test(&ctx, pkt, 0, &ret_val, &duration);
	ASSERT_NE(err, 0, "Empty packet should be rejected by test runner");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Truncated Ethernet header is dropped
 *
 * Note: bpf_prog_test_run() requires minimum packet size (14 bytes).
 * This test verifies kernel validation.
 */
static int test_truncated_ethernet(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	unsigned char pkt[10] = {0};  /* Less than sizeof(struct ethhdr) = 14 */
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* bpf_prog_test_run() should reject packets < 14 bytes */
	err = run_xdp_test(&ctx, pkt, sizeof(pkt), &ret_val, &duration);
	ASSERT_NE(err, 0, "Truncated packet should be rejected by test runner");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Valid IPv4 packet passes through
 */
static int test_ipv4_valid(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build a valid IPv4 packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
			  inet_addr("192.168.1.2"), IPPROTO_ICMP, 64);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");

	/* XDP program performs FIB lookup which returns bpf_redirect() */
	/* Valid actions: PASS (no route), DROP (blackhole), REDIRECT (found route) */
	ASSERT(ret_val == XDP_PASS || ret_val == XDP_DROP || ret_val == XDP_REDIRECT,
	       "IPv4 packet should pass/drop/redirect (got %d)", ret_val);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: IPv4 packet with TTL=0 is passed to kernel
 */
static int test_ipv4_ttl_zero(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build IPv4 packet with TTL=0 */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
			  inet_addr("192.168.1.2"), IPPROTO_ICMP, 0);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "TTL=0 should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: IPv4 packet with TTL=1 is passed to kernel
 */
static int test_ipv4_ttl_one(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build IPv4 packet with TTL=1 */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
			  inet_addr("192.168.1.2"), IPPROTO_ICMP, 1);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "TTL=1 should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: IPv4 fragmented packet is passed to kernel
 */
static int test_ipv4_fragment(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build fragmented IPv4 packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
			  inet_addr("192.168.1.2"), IPPROTO_ICMP, 64);

	/* Set MF (More Fragments) flag */
	packet_set_ipv4_frag(&pkt, 0, 1, 0);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "Fragmented packet should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Valid IPv6 packet passes through
 */
static int test_ipv6_valid(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build a valid IPv6 packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, ipv6_src, ipv6_dst, IPPROTO_ICMPV6, 64);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");

	/* XDP program performs FIB lookup which returns bpf_redirect() */
	/* Valid actions: PASS (no route), DROP (blackhole), REDIRECT (found route) */
	ASSERT(ret_val == XDP_PASS || ret_val == XDP_DROP || ret_val == XDP_REDIRECT,
	       "IPv6 packet should pass/drop/redirect (got %d)", ret_val);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: IPv6 packet with hop_limit=0 is passed to kernel
 */
static int test_ipv6_hop_limit_zero(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build IPv6 packet with hop_limit=0 */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, ipv6_src, ipv6_dst, IPPROTO_ICMPV6, 0);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "hop_limit=0 should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: IPv6 packet with hop_limit=1 is passed to kernel
 */
static int test_ipv6_hop_limit_one(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build IPv6 packet with hop_limit=1 */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, ipv6_src, ipv6_dst, IPPROTO_ICMPV6, 1);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "hop_limit=1 should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: ARP packet is passed to kernel
 */
static int test_arp_pass(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build ARP packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_ARP));
	/* Add minimal ARP payload */
	memcpy(pkt.data + pkt.len, "\x00\x01\x08\x00\x06\x04\x00\x01", 8);
	pkt.len += 8;

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "ARP should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Multicast packet is passed to kernel
 */
static int test_multicast_pass(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build multicast packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, mcast_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
			  inet_addr("224.0.0.1"), IPPROTO_ICMP, 64);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "Multicast should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Broadcast packet is passed to kernel
 */
static int test_broadcast_pass(void)
{
	struct bpf_test_ctx ctx;
	__u32 ret_val, duration;
	struct test_packet pkt;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Build broadcast packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, bcast_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
			  inet_addr("192.168.1.255"), IPPROTO_ICMP, 64);

	err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
	ASSERT_EQ(err, 0, "Test run should succeed");
	ASSERT_EQ(ret_val, XDP_PASS, "Broadcast should pass to kernel");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

int main(void)
{
	printf("\n");
	printf("=== XDP Router Integration Tests: Packet Injection ===\n");
	printf("\n");

	/* Setup tests */
	RUN_TEST(test_bpf_load);

	/* Edge case tests */
	RUN_TEST(test_empty_packet);
	RUN_TEST(test_truncated_ethernet);

	/* IPv4 tests */
	RUN_TEST(test_ipv4_valid);
	RUN_TEST(test_ipv4_ttl_zero);
	RUN_TEST(test_ipv4_ttl_one);
	RUN_TEST(test_ipv4_fragment);

	/* IPv6 tests */
	RUN_TEST(test_ipv6_valid);
	RUN_TEST(test_ipv6_hop_limit_zero);
	RUN_TEST(test_ipv6_hop_limit_one);

	/* Protocol tests */
	RUN_TEST(test_arp_pass);
	RUN_TEST(test_multicast_pass);
	RUN_TEST(test_broadcast_pass);

	PRINT_TEST_SUMMARY();
}
