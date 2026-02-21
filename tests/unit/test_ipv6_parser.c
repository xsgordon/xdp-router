// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * IPv6 Parser Tests
 *
 * Comprehensive unit tests for the IPv6 parser.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>

#include "../common/test_harness.h"
#include "../common/packet_builder.h"

/* Include parsers under test */
#ifndef __always_inline
#define __always_inline inline
#endif
#include "../../src/xdp/parsers/ethernet.h"
#include "../../src/xdp/parsers/ipv6.h"

/*
 * Test 1: Parse valid IPv6 packet
 */
static int test_parse_ipv6_valid(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet + IPv6 */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_TCP, 64);

	/* Setup and parse Ethernet first */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 */
	rc = parse_ipv6(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv6 should succeed");
	ASSERT_NOT_NULL(pctx.ip6h, "IPv6 header should be set");
	ASSERT_EQ(pctx.ip6h->version, 6, "version should be 6");
	ASSERT_EQ(pctx.ip6h->hop_limit, 64, "hop limit should be 64");
	ASSERT_EQ(pctx.ip6h->nexthdr, IPPROTO_TCP, "next header should be TCP");

	return TEST_PASS;
}

/*
 * Test 2: Parse IPv6 with invalid version (security test)
 */
static int test_parse_ipv6_invalid_version(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_TCP, 64);

	/* Corrupt version field to 7 */
	packet_set_ipv6_version(&pkt, 7);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 - should fail */
	rc = parse_ipv6(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ipv6 should reject invalid version");

	return TEST_PASS;
}

/*
 * Test 3: Parse IPv6 packet that's truncated
 */
static int test_parse_ipv6_truncated(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_TCP, 64);

	/* Truncate to cut off part of IPv6 header */
	packet_truncate(&pkt, sizeof(struct ethhdr) + 30);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 - should fail */
	rc = parse_ipv6(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ipv6 should reject truncated packet");

	return TEST_PASS;
}

/*
 * Test 4: Parse IPv6 over VLAN
 */
static int test_parse_ipv6_over_vlan(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet + VLAN + IPv6 */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_add_vlan(&pkt, 200, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_UDP, 128);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 */
	rc = parse_ipv6(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv6 should work over VLAN");
	ASSERT_NOT_NULL(pctx.ip6h, "IPv6 header should be set");
	ASSERT_EQ(pctx.ip6h->nexthdr, IPPROTO_UDP, "next header should be UDP");
	ASSERT_EQ(pctx.ip6h->hop_limit, 128, "hop limit should be 128");

	return TEST_PASS;
}

/*
 * Test 5: Parse IPv6 with hop limit 1
 */
static int test_parse_ipv6_hop_limit_one(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet with hop limit 1 */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_TCP, 1);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 */
	rc = parse_ipv6(&pctx);

	/* Verify - parser should succeed, handler will deal with TTL */
	ASSERT_EQ(rc, 0, "parse_ipv6 should succeed");
	ASSERT_EQ(pctx.ip6h->hop_limit, 1, "hop limit should be 1");

	return TEST_PASS;
}

/*
 * Test 6: Parse IPv6 over double VLAN
 */
static int test_parse_ipv6_over_double_vlan(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet + 2xVLAN + IPv6 */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_add_vlan(&pkt, 300, htons(ETH_P_IPV6));
	packet_add_vlan(&pkt, 200, htons(ETH_P_8021Q));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_ICMPV6, 255);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 */
	rc = parse_ipv6(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv6 should work over double VLAN");
	ASSERT_NOT_NULL(pctx.ip6h, "IPv6 header should be set");
	ASSERT_EQ(pctx.ip6h->nexthdr, IPPROTO_ICMPV6, "next header should be ICMPv6");
	ASSERT_EQ(pctx.ip6h->hop_limit, 255, "hop limit should be 255");

	return TEST_PASS;
}

/*
 * Main test runner
 */
int main(void)
{
	TEST_SUITE_BEGIN("IPv6 Parser Tests");

	RUN_TEST(test_parse_ipv6_valid);
	RUN_TEST(test_parse_ipv6_invalid_version);
	RUN_TEST(test_parse_ipv6_truncated);
	RUN_TEST(test_parse_ipv6_over_vlan);
	RUN_TEST(test_parse_ipv6_hop_limit_one);
	RUN_TEST(test_parse_ipv6_over_double_vlan);

	TEST_SUITE_END();
}
