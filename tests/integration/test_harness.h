/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __INTEGRATION_TEST_HARNESS_H
#define __INTEGRATION_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "common/common.h"
#include "xdp_router.skel.h"

/* ANSI color codes for test output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[0;33m"
#define COLOR_BLUE    "\033[0;34m"

/* Test result macros */
#define TEST_PASS() \
	do { \
		printf("[%sPASS%s]\n", COLOR_GREEN, COLOR_RESET); \
		return 0; \
	} while (0)

#define TEST_FAIL(msg, ...) \
	do { \
		printf("[%sFAIL%s] ", COLOR_RED, COLOR_RESET); \
		printf(msg, ##__VA_ARGS__); \
		printf("\n"); \
		return -1; \
	} while (0)

#define TEST_SKIP(msg, ...) \
	do { \
		printf("[%sSKIP%s] ", COLOR_YELLOW, COLOR_RESET); \
		printf(msg, ##__VA_ARGS__); \
		printf("\n"); \
		return 0; \
	} while (0)

#define ASSERT(cond, msg, ...) \
	do { \
		if (!(cond)) { \
			TEST_FAIL("Assertion failed: " msg, ##__VA_ARGS__); \
		} \
	} while (0)

#define ASSERT_EQ(a, b, msg, ...) \
	do { \
		if ((a) != (b)) { \
			TEST_FAIL("Expected %ld, got %ld: " msg, \
				  (long)(b), (long)(a), ##__VA_ARGS__); \
		} \
	} while (0)

#define ASSERT_NE(a, b, msg, ...) \
	do { \
		if ((a) == (b)) { \
			TEST_FAIL("Expected not equal to %ld: " msg, \
				  (long)(b), ##__VA_ARGS__); \
		} \
	} while (0)

#define ASSERT_GT(a, b, msg, ...) \
	do { \
		if ((a) <= (b)) { \
			TEST_FAIL("Expected > %ld, got %ld: " msg, \
				  (long)(b), (long)(a), ##__VA_ARGS__); \
		} \
	} while (0)

#define ASSERT_NULL(ptr, msg, ...) \
	do { \
		if ((ptr) != NULL) { \
			TEST_FAIL("Expected NULL: " msg, ##__VA_ARGS__); \
		} \
	} while (0)

#define ASSERT_NOT_NULL(ptr, msg, ...) \
	do { \
		if ((ptr) == NULL) { \
			TEST_FAIL("Expected non-NULL: " msg, ##__VA_ARGS__); \
		} \
	} while (0)

/*
 * BPF test context
 *
 * Contains loaded BPF program and map file descriptors for testing.
 * Use setup_bpf_test() to initialize and teardown_bpf_test() to clean up.
 */
struct bpf_test_ctx {
	struct xdp_router_bpf *skel;
	int prog_fd;
	int stats_fd;
	int drop_stats_fd;
	int config_fd;
	int nr_cpus;
};

/*
 * Set up BPF test environment
 *
 * Loads the XDP router BPF program and opens all maps.
 * Must be called at the start of each test.
 *
 * @param ctx  Test context to initialize
 * @return 0 on success, -1 on error
 */
static inline int setup_bpf_test(struct bpf_test_ctx *ctx)
{
	int err;

	memset(ctx, 0, sizeof(*ctx));

	/* Get number of CPUs for PERCPU maps */
	ctx->nr_cpus = libbpf_num_possible_cpus();
	if (ctx->nr_cpus < 0) {
		fprintf(stderr, "Failed to get number of CPUs: %s\n",
			strerror(-ctx->nr_cpus));
		return -1;
	}

	/* Open and load BPF program */
	ctx->skel = xdp_router_bpf__open();
	if (!ctx->skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return -1;
	}

	err = xdp_router_bpf__load(ctx->skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF program: %d\n", err);
		xdp_router_bpf__destroy(ctx->skel);
		return -1;
	}

	/* Get file descriptors */
	ctx->prog_fd = bpf_program__fd(ctx->skel->progs.xdp_router_main);
	ctx->stats_fd = bpf_map__fd(ctx->skel->maps.packet_stats);
	ctx->drop_stats_fd = bpf_map__fd(ctx->skel->maps.drop_stats);
	ctx->config_fd = bpf_map__fd(ctx->skel->maps.config_map);

	if (ctx->prog_fd < 0 || ctx->stats_fd < 0 ||
	    ctx->drop_stats_fd < 0 || ctx->config_fd < 0) {
		fprintf(stderr, "Failed to get BPF file descriptors\n");
		xdp_router_bpf__destroy(ctx->skel);
		return -1;
	}

	/* Initialize config map with default values */
	{
		struct xdp_config cfg = {
			.version = XDP_ROUTER_MAP_VERSION,
			.features = FEATURE_IPV4_BIT | FEATURE_IPV6_BIT,
			.log_level = 0,
			.max_srv6_sids = 0,
		};
		__u32 key = 0;

		err = bpf_map_update_elem(ctx->config_fd, &key, &cfg, BPF_ANY);
		if (err) {
			fprintf(stderr, "Failed to initialize config map: %s\n",
				strerror(errno));
			xdp_router_bpf__destroy(ctx->skel);
			return -1;
		}
	}

	return 0;
}

/*
 * Tear down BPF test environment
 *
 * Destroys the BPF skeleton and frees resources.
 * Must be called at the end of each test.
 *
 * @param ctx  Test context to clean up
 */
static inline void teardown_bpf_test(struct bpf_test_ctx *ctx)
{
	if (ctx && ctx->skel)
		xdp_router_bpf__destroy(ctx->skel);
}

/*
 * Run XDP program with test packet using bpf_prog_test_run()
 *
 * Executes the XDP program on a test packet without attaching to an interface.
 * This allows testing without root privileges or network namespaces.
 *
 * @param ctx        Test context with loaded BPF program
 * @param data       Packet data (Ethernet frame)
 * @param data_len   Length of packet data
 * @param ret_val    Output parameter for XDP action (XDP_PASS, XDP_DROP, etc.)
 * @param duration   Output parameter for execution time (nanoseconds)
 * @return 0 on success, -1 on error
 */
static inline int run_xdp_test(struct bpf_test_ctx *ctx,
				void *data, __u32 data_len,
				__u32 *ret_val, __u32 *duration)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = data,
		.data_size_in = data_len,
		.repeat = 1,
	);
	int err;

	err = bpf_prog_test_run_opts(ctx->prog_fd, &opts);
	if (err) {
		fprintf(stderr, "bpf_prog_test_run failed: %s\n", strerror(errno));
		return -1;
	}

	if (ret_val)
		*ret_val = opts.retval;
	if (duration)
		*duration = opts.duration;

	return 0;
}

/*
 * Get aggregated statistics for an interface
 *
 * Reads PERCPU statistics and aggregates them across all CPUs.
 *
 * @param ctx      Test context
 * @param ifindex  Interface index
 * @param stats    Output parameter for aggregated stats
 * @return 0 on success, -1 on error
 */
static inline int get_interface_stats(struct bpf_test_ctx *ctx,
				       __u32 ifindex,
				       struct if_stats *stats)
{
	struct if_stats *percpu_stats;
	int cpu, err;

	percpu_stats = calloc(ctx->nr_cpus, sizeof(struct if_stats));
	if (!percpu_stats)
		return -1;

	err = bpf_map_lookup_elem(ctx->stats_fd, &ifindex, percpu_stats);
	if (err) {
		free(percpu_stats);
		return -1;
	}

	/* Aggregate across all CPUs */
	memset(stats, 0, sizeof(*stats));
	for (cpu = 0; cpu < ctx->nr_cpus; cpu++) {
		stats->rx_packets += percpu_stats[cpu].rx_packets;
		stats->rx_bytes   += percpu_stats[cpu].rx_bytes;
		stats->tx_packets += percpu_stats[cpu].tx_packets;
		stats->tx_bytes   += percpu_stats[cpu].tx_bytes;
		stats->dropped    += percpu_stats[cpu].dropped;
		stats->errors     += percpu_stats[cpu].errors;
	}

	free(percpu_stats);
	return 0;
}

/*
 * Get drop reason counter
 *
 * @param ctx     Test context
 * @param reason  Drop reason enum value
 * @param count   Output parameter for counter value
 * @return 0 on success, -1 on error
 */
static inline int get_drop_reason(struct bpf_test_ctx *ctx,
				   enum drop_reason reason,
				   __u64 *count)
{
	__u64 *percpu_count;
	__u32 key = reason;
	int cpu, err;

	percpu_count = calloc(ctx->nr_cpus, sizeof(__u64));
	if (!percpu_count)
		return -1;

	err = bpf_map_lookup_elem(ctx->drop_stats_fd, &key, percpu_count);
	if (err) {
		free(percpu_count);
		return -1;
	}

	/* Aggregate across all CPUs */
	*count = 0;
	for (cpu = 0; cpu < ctx->nr_cpus; cpu++)
		*count += percpu_count[cpu];

	free(percpu_count);
	return 0;
}

/* Test runner macros */
#define RUN_TEST(test_func) \
	do { \
		printf("Running %s... ", #test_func); \
		fflush(stdout); \
		if (test_func() == 0) { \
			tests_passed++; \
		} else { \
			tests_failed++; \
		} \
		tests_run++; \
	} while (0)

#define PRINT_TEST_SUMMARY() \
	do { \
		printf("\n"); \
		printf("=== Test Summary ===\n"); \
		printf("Tests run: %d\n", tests_run); \
		printf("Tests passed: %s%d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET); \
		if (tests_failed > 0) { \
			printf("Tests failed: %s%d%s\n", COLOR_RED, tests_failed, COLOR_RESET); \
			printf("\n%sSome tests FAILED!%s\n", COLOR_RED, COLOR_RESET); \
			return 1; \
		} else { \
			printf("Tests failed: 0\n"); \
			printf("\n%sAll tests PASSED!%s\n", COLOR_GREEN, COLOR_RESET); \
			return 0; \
		} \
	} while (0)

#endif /* __INTEGRATION_TEST_HARNESS_H */
