/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __TEST_HARNESS_H
#define __TEST_HARNESS_H

#include <stdio.h>
#include <string.h>

/*
 * Test Harness for xdp-router
 *
 * Provides macros and utilities for unit testing.
 */

/* Test results */
#define TEST_PASS 0
#define TEST_FAIL 1

/* Color output */
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_RESET   "\033[0m"

/* Test statistics */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/*
 * Assertion macros
 */

#define ASSERT(condition, message) \
	do { \
		if (!(condition)) { \
			printf(COLOR_RED "ASSERTION FAILED" COLOR_RESET ": %s\n", message); \
			printf("  at %s:%d\n", __FILE__, __LINE__); \
			printf("  condition: %s\n", #condition); \
			return TEST_FAIL; \
		} \
	} while (0)

#define ASSERT_EQ(actual, expected, message) \
	do { \
		if ((actual) != (expected)) { \
			printf(COLOR_RED "ASSERTION FAILED" COLOR_RESET ": %s\n", message); \
			printf("  at %s:%d\n", __FILE__, __LINE__); \
			printf("  expected: %lld\n", (long long)(expected)); \
			printf("  actual:   %lld\n", (long long)(actual)); \
			return TEST_FAIL; \
		} \
	} while (0)

#define ASSERT_NE(actual, expected, message) \
	do { \
		if ((actual) == (expected)) { \
			printf(COLOR_RED "ASSERTION FAILED" COLOR_RESET ": %s\n", message); \
			printf("  at %s:%d\n", __FILE__, __LINE__); \
			printf("  value: %lld (should not equal)\n", (long long)(actual)); \
			return TEST_FAIL; \
		} \
	} while (0)

#define ASSERT_NULL(ptr, message) \
	ASSERT((ptr) == NULL, message)

#define ASSERT_NOT_NULL(ptr, message) \
	ASSERT((ptr) != NULL, message)

#define ASSERT_STR_EQ(actual, expected, message) \
	do { \
		if (strcmp((actual), (expected)) != 0) { \
			printf(COLOR_RED "ASSERTION FAILED" COLOR_RESET ": %s\n", message); \
			printf("  at %s:%d\n", __FILE__, __LINE__); \
			printf("  expected: \"%s\"\n", (expected)); \
			printf("  actual:   \"%s\"\n", (actual)); \
			return TEST_FAIL; \
		} \
	} while (0)

#define ASSERT_MEM_EQ(actual, expected, size, message) \
	do { \
		if (memcmp((actual), (expected), (size)) != 0) { \
			printf(COLOR_RED "ASSERTION FAILED" COLOR_RESET ": %s\n", message); \
			printf("  at %s:%d\n", __FILE__, __LINE__); \
			printf("  memory differs\n"); \
			return TEST_FAIL; \
		} \
	} while (0)

/*
 * Test runner macros
 */

#define RUN_TEST(test_func) \
	do { \
		printf("Running " #test_func "... "); \
		fflush(stdout); \
		tests_run++; \
		if (test_func() == TEST_PASS) { \
			printf(COLOR_GREEN "PASS" COLOR_RESET "\n"); \
			tests_passed++; \
		} else { \
			printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
			tests_failed++; \
		} \
	} while (0)

#define TEST_SUITE_BEGIN(name) \
	do { \
		printf("\n"); \
		printf("=== %s ===\n", name); \
		printf("\n"); \
		tests_run = 0; \
		tests_passed = 0; \
		tests_failed = 0; \
	} while (0)

#define TEST_SUITE_END() \
	do { \
		printf("\n"); \
		printf("=== Test Summary ===\n"); \
		printf("Tests run: %d\n", tests_run); \
		printf("Tests passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed); \
		if (tests_failed > 0) { \
			printf("Tests failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed); \
		} else { \
			printf("Tests failed: %d\n", tests_failed); \
		} \
		printf("\n"); \
		if (tests_failed == 0) { \
			printf(COLOR_GREEN "All tests PASSED!" COLOR_RESET "\n"); \
			return 0; \
		} else { \
			printf(COLOR_RED "Some tests FAILED!" COLOR_RESET "\n"); \
			return 1; \
		} \
	} while (0)

/*
 * Test skip macro (for tests that require specific conditions)
 */
#define TEST_SKIP(reason) \
	do { \
		printf(COLOR_YELLOW "SKIP" COLOR_RESET " (%s)\n", reason); \
		return TEST_PASS; \
	} while (0)

/*
 * Helper to create parser context from test packet
 */
#include "../../src/common/parser.h"

static inline void setup_parser_ctx(struct parser_ctx *pctx,
				     const void *data,
				     size_t len)
{
	memset(pctx, 0, sizeof(*pctx));
	pctx->data = (void *)data;
	pctx->data_end = (void *)((uint8_t *)data + len);
}

#endif /* __TEST_HARNESS_H */
