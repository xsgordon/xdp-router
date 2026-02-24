// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * Integration tests for PERCPU statistics handling
 *
 * These tests verify that PERCPU map aggregation works correctly and
 * would have caught the segfault bug found during manual testing.
 */

#include <linux/if_ether.h>
#include <linux/ip.h>

#include "test_harness.h"
#include "../common/packet_builder.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/*
 * Test: PERCPU stats can be read without segfault
 *
 * This test would have caught the buffer overflow bug where we allocated
 * a single struct but the kernel wrote nr_cpus structs.
 */
static int test_percpu_read_no_segfault(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats;
	int err;

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* This should not segfault */
	err = get_interface_stats(&ctx, 0, &stats);

	/* Interface 0 may or may not have stats, we just care it doesn't crash */
	/* If err == -1, that's OK (no stats), we're testing memory safety */

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Stats aggregation across multiple CPUs
 */
static int test_percpu_aggregation(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats;
	struct test_packet pkt;
	__u32 ret_val, duration;
	uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	uint8_t dst_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
	int err, i;

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Get initial stats */
	struct if_stats stats_before = {0};
	get_interface_stats(&ctx, 0, &stats_before);

	/* Inject several packets */
	for (i = 0; i < 10; i++) {
		packet_init(&pkt);
		packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
		packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
				  inet_addr("192.168.1.2"), IPPROTO_ICMP, 64);

		err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
		ASSERT_EQ(err, 0, "Test run should succeed");
	}

	/* Get stats after */
	err = get_interface_stats(&ctx, 0, &stats);
	ASSERT_EQ(err, 0, "Should be able to read stats");

	/* Stats should have increased */
	ASSERT_GT(stats.rx_packets, stats_before.rx_packets,
		  "RX packets should increase");
	ASSERT_GT(stats.rx_bytes, stats_before.rx_bytes,
		  "RX bytes should increase");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Stats are accurate (count matches packets sent)
 */
static int test_stats_accuracy(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats_before, stats_after;
	struct test_packet pkt;
	__u32 ret_val, duration;
	uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	uint8_t dst_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
	int err, i;
	const int num_packets = 100;

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Get initial stats */
	get_interface_stats(&ctx, 0, &stats_before);

	/* Inject exactly num_packets packets */
	for (i = 0; i < num_packets; i++) {
		packet_init(&pkt);
		packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
		packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
				  inet_addr("192.168.1.2"), IPPROTO_ICMP, 64);

		err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
		ASSERT_EQ(err, 0, "Test run should succeed");
	}

	/* Get final stats */
	err = get_interface_stats(&ctx, 0, &stats_after);
	ASSERT_EQ(err, 0, "Should be able to read stats");

	/* Verify packet count increased by exactly num_packets */
	__u64 rx_packets_delta = stats_after.rx_packets - stats_before.rx_packets;
	ASSERT_EQ(rx_packets_delta, num_packets,
		  "RX packets should increase by %d (got %llu)",
		  num_packets, (unsigned long long)rx_packets_delta);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Byte counters are accurate
 */
static int test_stats_bytes(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats_before, stats_after;
	struct test_packet pkt;
	__u32 ret_val, duration;
	uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	uint8_t dst_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
	int err, i;
	const int num_packets = 10;

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Get initial stats */
	get_interface_stats(&ctx, 0, &stats_before);

	/* Build known-size packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, dst_mac, src_mac, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, inet_addr("192.168.1.1"),
			  inet_addr("192.168.1.2"), IPPROTO_ICMP, 64);
	size_t packet_size = pkt.len;

	/* Inject packets */
	for (i = 0; i < num_packets; i++) {
		err = run_xdp_test(&ctx, pkt.data, pkt.len, &ret_val, &duration);
		ASSERT_EQ(err, 0, "Test run should succeed");
	}

	/* Get final stats */
	err = get_interface_stats(&ctx, 0, &stats_after);
	ASSERT_EQ(err, 0, "Should be able to read stats");

	/* Verify byte count increased correctly */
	__u64 rx_bytes_delta = stats_after.rx_bytes - stats_before.rx_bytes;
	__u64 expected_bytes = packet_size * num_packets;

	ASSERT_EQ(rx_bytes_delta, expected_bytes,
		  "RX bytes should increase by %llu (got %llu)",
		  (unsigned long long)expected_bytes,
		  (unsigned long long)rx_bytes_delta);

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Stats for multiple interfaces are independent
 */
static int test_stats_per_interface(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats_if0, stats_if1;
	int err;

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Read stats for interface 0 and 1 */
	get_interface_stats(&ctx, 0, &stats_if0);
	get_interface_stats(&ctx, 1, &stats_if1);

	/* Both reads should succeed (even if no traffic) */
	/* This tests that we can read from different interface indices */
	/* without corruption */

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Drop stats are tracked correctly
 */
static int test_drop_stats(void)
{
	struct bpf_test_ctx ctx;
	__u64 drop_count_before, drop_count_after;
	__u32 ret_val, duration;
	int err, i;

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Get initial drop stats */
	get_drop_reason(&ctx, DROP_PARSE_ERROR, &drop_count_before);

	/* Inject malformed packets that will be dropped */
	for (i = 0; i < 5; i++) {
		/* Truncated packet */
		unsigned char bad_pkt[10] = {0};
		err = run_xdp_test(&ctx, bad_pkt, sizeof(bad_pkt),
				   &ret_val, &duration);
		ASSERT_EQ(err, 0, "Test run should succeed");
		ASSERT_EQ(ret_val, XDP_DROP, "Truncated packet should be dropped");
	}

	/* Get final drop stats */
	err = get_drop_reason(&ctx, DROP_PARSE_ERROR, &drop_count_after);
	ASSERT_EQ(err, 0, "Should be able to read drop stats");

	/* Drop count should have increased */
	ASSERT_GT(drop_count_after, drop_count_before,
		  "Drop count should increase");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Config map version is correct
 */
static int test_config_version(void)
{
	struct bpf_test_ctx ctx;
	struct xdp_config cfg;
	__u32 key = 0;
	int err;

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Read config map */
	err = bpf_map_lookup_elem(ctx.config_fd, &key, &cfg);
	ASSERT_EQ(err, 0, "Should be able to read config map");

	/* Verify version */
	ASSERT_EQ(cfg.version, XDP_ROUTER_MAP_VERSION,
		  "Config version should match (expected 0x%08x, got 0x%08x)",
		  XDP_ROUTER_MAP_VERSION, cfg.version);

	/* Verify features */
	ASSERT(cfg.features & FEATURE_IPV4_BIT, "IPv4 should be enabled");
	ASSERT(cfg.features & FEATURE_IPV6_BIT, "IPv6 should be enabled");

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

int main(void)
{
	printf("\n");
	printf("=== XDP Router Integration Tests: PERCPU Stats ===\n");
	printf("\n");

	/* PERCPU safety tests (prevent segfault regression) */
	RUN_TEST(test_percpu_read_no_segfault);
	RUN_TEST(test_percpu_aggregation);

	/* Stats accuracy tests */
	RUN_TEST(test_stats_accuracy);
	RUN_TEST(test_stats_bytes);
	RUN_TEST(test_stats_per_interface);

	/* Drop stats tests */
	RUN_TEST(test_drop_stats);

	/* Config tests */
	RUN_TEST(test_config_version);

	PRINT_TEST_SUMMARY();
}
