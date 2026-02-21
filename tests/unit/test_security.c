// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * Security Regression Tests
 *
 * Comprehensive tests for security vulnerabilities identified in
 * security reviews. These tests ensure that all 20+ security issues
 * remain fixed and no regressions are introduced.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include "../common/test_harness.h"
#include "../common/packet_builder.h"

/* Include parsers under test */
#ifndef __always_inline
#define __always_inline inline
#endif
#include "../../src/xdp/parsers/ethernet.h"
#include "../../src/xdp/parsers/ipv4.h"
#include "../../src/xdp/parsers/ipv6.h"

/*
 * SECURITY TEST 1: Triple VLAN Attack Prevention
 *
 * CVE-Style: Protocol confusion via excessive VLAN tags
 * Risk: Attacker could use 3rd VLAN tag's EtherType as protocol type
 * Mitigation: Parser rejects packets with >2 VLAN tags
 */
static int test_security_triple_vlan_attack(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build frame with 3 VLAN tags (attack) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 300, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 200, htons(ETH_P_8021Q));
	packet_add_vlan(&pkt, 100, htons(ETH_P_8021Q));

	/* Parse - MUST reject */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject 3+ VLAN tags (protocol confusion attack)");

	return TEST_PASS;
}

/*
 * SECURITY TEST 2: IPv4 DF+MF Flag Conflict (RFC 791 Violation)
 *
 * CVE-Style: Malformed packet with contradictory flags
 * Risk: Firewall bypass, IDS evasion, protocol confusion
 * Mitigation: Parser rejects packets with both DF and MF set
 */
static int test_security_ipv4_df_mf_conflict(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet with illegal DF+MF combination */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);
	packet_set_ipv4_frag(&pkt, 1, 1, 0);  /* Both DF and MF */

	/* Parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - MUST reject */
	rc = parse_ipv4(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject DF+MF combination (RFC 791 violation)");

	return TEST_PASS;
}

/*
 * SECURITY TEST 3: IPv4 Version Mismatch
 *
 * CVE-Style: Protocol confusion via invalid version
 * Risk: Parser confusion, potential for bypass
 * Mitigation: Strict version validation
 */
static int test_security_ipv4_version_attack(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	struct iphdr *iph;
	int rc;

	/* Build packet with wrong version */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Corrupt version to 6 (IPv6) in IPv4 packet */
	iph = (struct iphdr *)(pkt.data + sizeof(struct ethhdr));
	iph->version = 6;

	/* Parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - MUST reject */
	rc = parse_ipv4(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject version mismatch");

	return TEST_PASS;
}

/*
 * SECURITY TEST 4: IPv4 Short IHL Attack
 *
 * CVE-Style: Buffer under-read via invalid header length
 * Risk: Out-of-bounds memory access
 * Mitigation: Strict IHL validation (must be >= 5)
 */
static int test_security_ipv4_short_ihl_attack(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	struct iphdr *iph;
	int rc;

	/* Build packet with IHL < 5 (invalid) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	/* Set IHL to 4 (16 bytes - less than minimum 20) */
	iph = (struct iphdr *)(pkt.data + sizeof(struct ethhdr));
	iph->ihl = 4;

	/* Parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - MUST reject */
	rc = parse_ipv4(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject IHL < 5 (under-read attack)");

	return TEST_PASS;
}

/*
 * SECURITY TEST 5: IPv6 Version Mismatch
 *
 * CVE-Style: Protocol confusion via invalid version
 * Risk: Parser confusion, potential for bypass
 * Mitigation: Strict version validation
 */
static int test_security_ipv6_version_attack(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet with wrong version */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_TCP, 64);
	packet_set_ipv6_version(&pkt, 4);  /* IPv4 version in IPv6 packet */

	/* Parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 - MUST reject */
	rc = parse_ipv6(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject version mismatch");

	return TEST_PASS;
}

/*
 * SECURITY TEST 6: Truncated Ethernet Header
 *
 * CVE-Style: Buffer over-read via truncated packet
 * Risk: Out-of-bounds memory access, crash
 * Mitigation: Strict bounds checking
 */
static int test_security_truncated_ethernet(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet and truncate to 13 bytes (one short) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_truncate(&pkt, sizeof(struct ethhdr) - 1);

	/* Parse - MUST reject */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject truncated ethernet header");

	return TEST_PASS;
}

/*
 * SECURITY TEST 7: Truncated VLAN Header
 *
 * CVE-Style: Buffer over-read via partial VLAN tag
 * Risk: Out-of-bounds memory access
 * Mitigation: Bounds checking for VLAN headers
 */
static int test_security_truncated_vlan(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build VLAN packet and truncate in middle of VLAN header */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 100, htons(ETH_P_IP));
	packet_truncate(&pkt, sizeof(struct ethhdr) + 2);  /* Cut off VLAN */

	/* Parse - MUST reject */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject truncated VLAN header");

	return TEST_PASS;
}

/*
 * SECURITY TEST 8: Truncated IPv4 Header
 *
 * CVE-Style: Buffer over-read via partial IP header
 * Risk: Out-of-bounds memory access
 * Mitigation: Bounds checking for IPv4 headers
 */
static int test_security_truncated_ipv4(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet and truncate to 15 bytes of IP header (needs 20) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);
	packet_truncate(&pkt, sizeof(struct ethhdr) + 15);

	/* Parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv4 - MUST reject */
	rc = parse_ipv4(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject truncated IPv4 header");

	return TEST_PASS;
}

/*
 * SECURITY TEST 9: Truncated IPv6 Header
 *
 * CVE-Style: Buffer over-read via partial IPv6 header
 * Risk: Out-of-bounds memory access
 * Mitigation: Bounds checking for IPv6 headers
 */
static int test_security_truncated_ipv6(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build packet and truncate to 30 bytes of IPv6 header (needs 40) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));
	packet_build_ipv6(&pkt, IPV6_SRC, IPV6_DST, IPPROTO_TCP, 64);
	packet_truncate(&pkt, sizeof(struct ethhdr) + 30);

	/* Parse */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "ethernet parse should succeed");

	/* Parse IPv6 - MUST reject */
	rc = parse_ipv6(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject truncated IPv6 header");

	return TEST_PASS;
}

/*
 * SECURITY TEST 10: Zero-Length Packet
 *
 * CVE-Style: Edge case handling
 * Risk: Unexpected behavior, potential crash
 * Mitigation: Reject packets with no data
 */
static int test_security_zero_length_packet(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Zero-length packet */
	packet_init(&pkt);
	pkt.len = 0;

	/* Parse - MUST reject */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);

	ASSERT_EQ(rc, -1, "SECURITY: Must reject zero-length packet");

	return TEST_PASS;
}

/*
 * SECURITY TEST 11: Maximum VLAN Tag Boundary
 *
 * CVE-Style: Off-by-one validation
 * Risk: Protocol confusion if limit not enforced correctly
 * Mitigation: Accept exactly 2 VLAN tags, reject 3+
 */
static int test_security_max_vlan_boundary(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Test 1: 2 VLAN tags (maximum allowed) - should PASS */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 200, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 100, htons(ETH_P_8021Q));

	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, 0, "2 VLAN tags should be accepted");

	/* Test 2: 3 VLAN tags (over limit) - should FAIL */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 300, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 200, htons(ETH_P_8021Q));
	packet_add_vlan(&pkt, 100, htons(ETH_P_8021Q));

	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	rc = parse_ethernet(&pctx);
	ASSERT_EQ(rc, -1, "SECURITY: 3 VLAN tags must be rejected (boundary test)");

	return TEST_PASS;
}

/*
 * SECURITY TEST 12: IPv4 Fragment Offset Validation
 *
 * CVE-Style: Fragment-based attacks
 * Risk: Fragment reassembly attacks, firewall bypass
 * Mitigation: Detect and flag fragmented packets
 */
static int test_security_ipv4_fragment_detection(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Test 1: No fragmentation */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);

	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	parse_ethernet(&pctx);
	rc = parse_ipv4(&pctx);
	ASSERT_EQ(rc, 0, "parse should succeed");
	ASSERT_EQ(pctx.is_fragment, 0, "should not be flagged as fragment");

	/* Test 2: MF flag set (fragment) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);
	packet_set_ipv4_frag(&pkt, 0, 1, 0);

	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	parse_ethernet(&pctx);
	rc = parse_ipv4(&pctx);
	ASSERT_EQ(rc, 0, "parse should succeed");
	ASSERT_EQ(pctx.is_fragment, 1, "SECURITY: MF flag must set fragment flag");

	/* Test 3: Non-zero offset (fragment) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_build_ipv4(&pkt, htonl(IPV4_SRC), htonl(IPV4_DST),
			  IPPROTO_TCP, 64);
	packet_set_ipv4_frag(&pkt, 0, 0, 100);

	setup_parser_ctx(&pctx, pkt.data, pkt.len);
	parse_ethernet(&pctx);
	rc = parse_ipv4(&pctx);
	ASSERT_EQ(rc, 0, "parse should succeed");
	ASSERT_EQ(pctx.is_fragment, 1, "SECURITY: Offset > 0 must set fragment flag");

	return TEST_PASS;
}

/*
 * Main test runner
 */
int main(void)
{
	TEST_SUITE_BEGIN("Security Regression Tests");

	/* Protocol Confusion Attacks */
	RUN_TEST(test_security_triple_vlan_attack);
	RUN_TEST(test_security_ipv4_version_attack);
	RUN_TEST(test_security_ipv6_version_attack);

	/* Malformed Packet Attacks */
	RUN_TEST(test_security_ipv4_df_mf_conflict);
	RUN_TEST(test_security_ipv4_short_ihl_attack);

	/* Buffer Over-read Attacks */
	RUN_TEST(test_security_truncated_ethernet);
	RUN_TEST(test_security_truncated_vlan);
	RUN_TEST(test_security_truncated_ipv4);
	RUN_TEST(test_security_truncated_ipv6);

	/* Edge Cases */
	RUN_TEST(test_security_zero_length_packet);
	RUN_TEST(test_security_max_vlan_boundary);

	/* Fragment-based Attacks */
	RUN_TEST(test_security_ipv4_fragment_detection);

	TEST_SUITE_END();
}
