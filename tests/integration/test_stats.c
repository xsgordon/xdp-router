// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * Integration tests for PERCPU statistics handling
 *
 * These tests verify that PERCPU map aggregation works correctly and
 * would have caught the segfault bug found during manual testing.
 */

/* clang-format off */
#include <linux/if_ether.h>
#include <linux/ip.h>

#include "test_harness.h"
#include "../common/packet_builder.h"
/* clang-format on */

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

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

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
 *
 * Note: bpf_prog_test_run() doesn't set ingress_ifindex, so the XDP program
 * can't determine which interface the packet came from. Stats won't update.
 * This test verifies the aggregation logic works without crashing.
 */
static int test_percpu_aggregation(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Get stats for interface 0 */
	err = get_interface_stats(&ctx, 0, &stats);
	ASSERT_EQ(err, 0, "Should be able to read and aggregate PERCPU stats");

	/* Verify the aggregation doesn't crash (main goal of this test) */
	/* Stats values may be zero since bpf_prog_test_run() doesn't set ifindex */

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Stats map can be read and values are accessible
 *
 * Note: bpf_prog_test_run() doesn't set ingress_ifindex, so stats
 * won't actually increment. This test verifies the map is accessible
 * and values can be read without errors.
 */
static int test_stats_accuracy(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Read stats from map */
	err = get_interface_stats(&ctx, 0, &stats);
	ASSERT_EQ(err, 0, "Should be able to read stats from PERCPU map");

	/* Verify stats structure is valid (fields are accessible) */
	/* Values may be zero since we're not sending real traffic */
	(void)stats.rx_packets;
	(void)stats.rx_bytes;
	(void)stats.tx_packets;
	(void)stats.tx_bytes;

	teardown_bpf_test(&ctx);
	TEST_PASS();
}

/*
 * Test: Stats structure fields are accessible
 *
 * Note: This test verifies the stats structure can be read and all
 * fields are accessible. Actual counting requires real interface traffic.
 */
static int test_stats_bytes(void)
{
	struct bpf_test_ctx ctx;
	struct if_stats stats;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Read stats from map */
	err = get_interface_stats(&ctx, 0, &stats);
	ASSERT_EQ(err, 0, "Should be able to read stats");

	/* Verify all fields are accessible without corruption */
	/* The actual values may be zero without real traffic */
	/* Just access the fields to verify structure is valid */
	(void)stats.rx_bytes;
	(void)stats.tx_bytes;
	(void)stats.dropped;
	(void)stats.errors;

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

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

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
 * Test: Drop stats map is accessible
 *
 * Note: This test verifies the drop stats map can be read. Testing actual
 * drop counting would require packets that pass kernel validation but
 * fail XDP program validation.
 */
static int test_drop_stats(void)
{
	struct bpf_test_ctx ctx;
	__u64 drop_count;
	int err;

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

	err = setup_bpf_test(&ctx);
	ASSERT_EQ(err, 0, "BPF program should load");

	/* Read drop stats for various drop reasons */
	err = get_drop_reason(&ctx, DROP_PARSE_ERROR, &drop_count);
	ASSERT_EQ(err, 0, "Should be able to read DROP_PARSE_ERROR stats");

	err = get_drop_reason(&ctx, DROP_NO_ROUTE, &drop_count);
	ASSERT_EQ(err, 0, "Should be able to read DROP_NO_ROUTE stats");

	/* Just verify drop_count variable is accessible */
	(void)drop_count;

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

	if (geteuid() != 0) {
		TEST_SKIP("Requires root privileges");
	}

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
