// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

/*
 * Ethernet Parser Tests
 *
 * Comprehensive unit tests for the Ethernet/VLAN parser.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>

#include "../common/test_harness.h"
#include "../common/packet_builder.h"

/* Include parser under test */
#ifndef __always_inline
#define __always_inline inline
#endif
#include "../../src/xdp/parsers/ethernet.h"

/*
 * Test 1: Parse valid basic Ethernet frame
 */
static int test_parse_ethernet_valid(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build basic Ethernet frame */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ethernet should succeed");
	ASSERT_NOT_NULL(pctx.eth, "eth header should be set");
	ASSERT_EQ(pctx.ethertype, ETH_P_IP, "ethertype should be IP");
	ASSERT_EQ(pctx.l3_offset, sizeof(struct ethhdr), "l3_offset should be after ethernet header");
	ASSERT_MEM_EQ(pctx.eth->h_dest, MAC_DST, ETH_ALEN, "destination MAC should match");
	ASSERT_MEM_EQ(pctx.eth->h_source, MAC_SRC, ETH_ALEN, "source MAC should match");

	return TEST_PASS;
}

/*
 * Test 2: Parse Ethernet frame that's too short
 */
static int test_parse_ethernet_too_short(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build frame and truncate it */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_truncate(&pkt, sizeof(struct ethhdr) - 1);  /* One byte short */

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse - should fail */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ethernet should fail on short packet");

	return TEST_PASS;
}

/*
 * Test 3: Parse Ethernet with single VLAN tag
 */
static int test_parse_ethernet_single_vlan(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet frame with VLAN */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 100, htons(ETH_P_IP));

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ethernet should succeed");
	ASSERT_EQ(pctx.ethertype, ETH_P_IP, "ethertype should be IP after VLAN");
	ASSERT_EQ(pctx.l3_offset, sizeof(struct ethhdr) + sizeof(struct vlan_hdr),
		  "l3_offset should account for VLAN tag");

	return TEST_PASS;
}

/*
 * Test 4: Parse Ethernet with double VLAN tag (QinQ)
 */
static int test_parse_ethernet_double_vlan(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet frame with 2 VLAN tags */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 200, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 100, htons(ETH_P_8021Q));

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ethernet should succeed with 2 VLAN tags");
	ASSERT_EQ(pctx.ethertype, ETH_P_IP, "ethertype should be IP after VLANs");
	ASSERT_EQ(pctx.l3_offset, sizeof(struct ethhdr) + 2 * sizeof(struct vlan_hdr),
		  "l3_offset should account for 2 VLAN tags");

	return TEST_PASS;
}

/*
 * Test 5: Parse Ethernet with triple VLAN tag (should reject - security)
 */
static int test_parse_ethernet_triple_vlan_attack(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet frame with 3 VLAN tags (attack scenario) */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 300, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 200, htons(ETH_P_8021Q));
	packet_add_vlan(&pkt, 100, htons(ETH_P_8021Q));

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse - should fail (security protection) */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ethernet should reject 3+ VLAN tags");

	return TEST_PASS;
}

/*
 * Test 6: Parse Ethernet with VLAN but truncated
 */
static int test_parse_ethernet_vlan_truncated(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build Ethernet frame with VLAN */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IP));
	packet_add_vlan(&pkt, 100, htons(ETH_P_IP));

	/* Truncate to cut off VLAN header */
	packet_truncate(&pkt, sizeof(struct ethhdr) + 2);

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse - should fail */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, -1, "parse_ethernet should fail on truncated VLAN");

	return TEST_PASS;
}

/*
 * Test 7: Parse Ethernet with IPv6 ethertype
 */
static int test_parse_ethernet_ipv6(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build basic Ethernet frame with IPv6 */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_DST, MAC_SRC, htons(ETH_P_IPV6));

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ethernet should succeed");
	ASSERT_EQ(pctx.ethertype, ETH_P_IPV6, "ethertype should be IPv6");

	return TEST_PASS;
}

/*
 * Test 8: Parse Ethernet with multicast destination
 */
static int test_parse_ethernet_multicast(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build frame with multicast destination */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_MULTICAST, MAC_SRC, htons(ETH_P_IP));

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ethernet should succeed");
	ASSERT_NOT_NULL(pctx.eth, "eth header should be set");
	/* Multicast bit should be set in first byte */
	ASSERT_EQ(pctx.eth->h_dest[0] & 0x01, 0x01, "multicast bit should be set");

	return TEST_PASS;
}

/*
 * Test 9: Parse Ethernet with broadcast destination
 */
static int test_parse_ethernet_broadcast(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;

	/* Build frame with broadcast destination */
	packet_init(&pkt);
	packet_build_eth(&pkt, MAC_BROADCAST, MAC_SRC, htons(ETH_P_IP));

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ethernet should succeed");
	ASSERT_MEM_EQ(pctx.eth->h_dest, MAC_BROADCAST, ETH_ALEN,
		      "destination should be broadcast address");
	/* Multicast bit should be set */
	ASSERT_EQ(pctx.eth->h_dest[0] & 0x01, 0x01, "multicast bit should be set");

	return TEST_PASS;
}

/*
 * Test 10: Parse Ethernet with 802.1AD (QinQ) outer tag
 */
static int test_parse_ethernet_8021ad(void)
{
	struct test_packet pkt;
	struct parser_ctx pctx;
	int rc;
	struct ethhdr *eth;
	struct vlan_hdr *vlan;

	/* Build frame manually with 802.1AD */
	packet_init(&pkt);

	/* Ethernet header */
	eth = (struct ethhdr *)pkt.data;
	memcpy(eth->h_dest, MAC_DST, ETH_ALEN);
	memcpy(eth->h_source, MAC_SRC, ETH_ALEN);
	eth->h_proto = htons(ETH_P_8021AD);  /* 802.1AD Service VLAN */
	pkt.len = sizeof(struct ethhdr);

	/* First VLAN tag (Service VLAN) */
	vlan = (struct vlan_hdr *)(pkt.data + pkt.len);
	vlan->h_vlan_TCI = htons(100);
	vlan->h_vlan_encapsulated_proto = htons(ETH_P_8021Q);  /* Customer VLAN */
	pkt.len += sizeof(struct vlan_hdr);

	/* Second VLAN tag (Customer VLAN) */
	vlan = (struct vlan_hdr *)(pkt.data + pkt.len);
	vlan->h_vlan_TCI = htons(200);
	vlan->h_vlan_encapsulated_proto = htons(ETH_P_IP);
	pkt.len += sizeof(struct vlan_hdr);

	/* Setup parser context */
	setup_parser_ctx(&pctx, pkt.data, pkt.len);

	/* Parse */
	rc = parse_ethernet(&pctx);

	/* Verify */
	ASSERT_EQ(rc, 0, "parse_ethernet should handle 802.1AD");
	ASSERT_EQ(pctx.ethertype, ETH_P_IP, "ethertype should be IP");
	ASSERT_EQ(pctx.l3_offset, sizeof(struct ethhdr) + 2 * sizeof(struct vlan_hdr),
		  "l3_offset should account for both VLAN tags");

	return TEST_PASS;
}

/*
 * Main test runner
 */
int main(void)
{
	TEST_SUITE_BEGIN("Ethernet Parser Tests");

	RUN_TEST(test_parse_ethernet_valid);
	RUN_TEST(test_parse_ethernet_too_short);
	RUN_TEST(test_parse_ethernet_single_vlan);
	RUN_TEST(test_parse_ethernet_double_vlan);
	RUN_TEST(test_parse_ethernet_triple_vlan_attack);
	RUN_TEST(test_parse_ethernet_vlan_truncated);
	RUN_TEST(test_parse_ethernet_ipv6);
	RUN_TEST(test_parse_ethernet_multicast);
	RUN_TEST(test_parse_ethernet_broadcast);
	RUN_TEST(test_parse_ethernet_8021ad);

	TEST_SUITE_END();
}
