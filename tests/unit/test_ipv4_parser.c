// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * IPv4 Parser Tests
 *
 * Comprehensive unit tests for the IPv4 parser.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

#include "../common/test_harness.h"
#include "../common/packet_builder.h"

/* Include parsers under test */
#ifndef __always_inline
#define __always_inline inline
#endif
#include "../../src/xdp/parsers/ethernet.h"
#include "../../src/xdp/parsers/ipv4.h"

/*
 * Test 1: Parse valid IPv4 packet
 */
static int test_parse_ipv4_valid(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet + IPv4 */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Setup and parse Ethernet first */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv4 should succeed");
	ASSERT_NOT_NULL(pctx.iph, "IPv4 header should be set");
	ASSERT_EQ(pctx.is_fragment, 0, "should not be fragment");
	ASSERT_EQ(pctx.iph->version, 4, "version should be 4");
	ASSERT_EQ(pctx.iph->ihl, 5, "IHL should be 5 (20 bytes)");
	ASSERT_EQ(pctx.iph->ttl, 64, "TTL should be 64");
	ASSERT_EQ(pctx.iph->protocol, IPPROTO_TCP, "protocol should be TCP");

	return TEST_PASS;
}

/*
 * Test 2: Parse IPv4 with invalid version
 */
static int test_parse_ipv4_invalid_version(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	struct iphdr *iph;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Corrupt version field */
	iph = (struct iphdr *)(pkt.data + sizeof(struct ethhdr));
	iph->version = 5;  /* Invalid version */

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - should fail */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ipv4 should reject invalid version");

	return TEST_PASS;
}

/*
 * Test 3: Parse IPv4 with short IHL (header length)
 */
static int test_parse_ipv4_short_ihl(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	struct iphdr *iph;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Set IHL to 4 (16 bytes - less than minimum 20) */
	iph = (struct iphdr *)(pkt.data + sizeof(struct ethhdr));
	iph->ihl = 4;

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - should fail */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ipv4 should reject IHL < 5");

	return TEST_PASS;
}

/*
 * Test 4: Parse IPv4 packet that's truncated
 */
static int test_parse_ipv4_truncated(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Truncate to cut off part of IPv4 header */
	packet_truncate(&pkt, sizeof(struct ethhdr) + 15);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - should fail */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ipv4 should reject truncated packet");

	return TEST_PASS;
}

/*
 * Test 5: Parse IPv4 fragment (MF flag set)
 */
static int test_parse_ipv4_fragment_mf(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Set MF (More Fragments) flag */
	packet_set_ipv4_frag(&pkt, 0, 1, 0);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv4 should succeed");
	ASSERT_EQ(pctx.is_fragment, 1, "should be detected as fragment");

	return TEST_PASS;
}

/*
 * Test 6: Parse IPv4 fragment with offset
 */
static int test_parse_ipv4_fragment_offset(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Set fragment offset (non-zero) */
	packet_set_ipv4_frag(&pkt, 0, 0, 100);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv4 should succeed");
	ASSERT_EQ(pctx.is_fragment, 1, "should be detected as fragment");

	return TEST_PASS;
}

/*
 * Test 7: Parse IPv4 with DF flag (no fragmentation)
 */
static int test_parse_ipv4_dont_fragment(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Set DF (Don't Fragment) flag only */
	packet_set_ipv4_frag(&pkt, 1, 0, 0);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv4 should succeed");
	ASSERT_EQ(pctx.is_fragment, 0, "DF alone should not be fragment");

	return TEST_PASS;
}

/*
 * Test 8: Parse IPv4 with illegal DF+MF combination (security test)
 */
static int test_parse_ipv4_df_mf_conflict(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Set both DF and MF flags (illegal per RFC 791) */
	packet_set_ipv4_frag(&pkt, 1, 1, 0);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - should fail (security check) */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ipv4 should reject DF+MF combination");

	return TEST_PASS;
}

/*
 * Test 9: Parse IPv4 over VLAN
 */
static int test_parse_ipv4_over_vlan(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet + VLAN + IPv4 */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 100, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_UDP, 32);

	/* Setup and parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 */
	rc = parse_ipv4(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ipv4 should work over VLAN");
	ASSERT_NOT_NULL(pctx.iph, "IPv4 header should be set");
	ASSERT_EQ(pctx.iph->protocol, IPPROTO_UDP, "protocol should be UDP");
	ASSERT_EQ(pctx.iph->ttl, 32, "TTL should be 32");

	return TEST_PASS;
}

/*
 * Main test runner
 */
int main(void)
{
	TEST_SUITE_BEGIN("IPv4 Parser Tests");

	RUN_TEST(test_parse_ipv4_valid);
	RUN_TEST(test_parse_ipv4_invalid_version);
	RUN_TEST(test_parse_ipv4_short_ihl);
	RUN_TEST(test_parse_ipv4_truncated);
	RUN_TEST(test_parse_ipv4_fragment_mf);
	RUN_TEST(test_parse_ipv4_fragment_offset);
	RUN_TEST(test_parse_ipv4_dont_fragment);
	RUN_TEST(test_parse_ipv4_df_mf_conflict);
	RUN_TEST(test_parse_ipv4_over_vlan);

	TEST_SUITE_END();
}
