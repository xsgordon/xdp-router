/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 */

#ifndef __TEST_PACKET_BUILDER_H
#define __TEST_PACKET_BUILDER_H

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

/*
 * Test Packet Builder Utilities
 *
 * Helper functions to construct test packets for unit testing
 * parsers and handlers.
 */

#define TEST_PACKET_MAX_SIZE 1500

/* VLAN header structure (only define if not already defined) */
#ifndef __struct_vlan_hdr_defined
#define __struct_vlan_hdr_defined
struct vlan_hdr {
	uint16_t h_vlan_TCI;
	uint16_t h_vlan_encapsulated_proto;
} __attribute__((packed));
#endif

/* Test packet buffer */
struct test_packet {
	uint8_t data[TEST_PACKET_MAX_SIZE];
	size_t len;
};

/*
 * Initialize test packet buffer
 */
static inline void packet_init(struct test_packet *pkt)
{
	memset(pkt, 0, sizeof(*pkt));
}

/*
 * Build basic Ethernet frame
 *
 * @dst: Destination MAC address (6 bytes)
 * @src: Source MAC address (6 bytes)
 * @ethertype: EtherType (network byte order)
 */
static inline void packet_build_eth(struct test_packet *pkt,
				     const uint8_t *dst,
				     const uint8_t *src,
				     uint16_t ethertype)
{
	struct ethhdr *eth = (struct ethhdr *)pkt->data;

	memcpy(eth->h_dest, dst, ETH_ALEN);
	memcpy(eth->h_source, src, ETH_ALEN);
	eth->h_proto = ethertype;

	pkt->len = sizeof(struct ethhdr);
}

/*
 * Add VLAN tag to packet
 *
 * @vlan_id: VLAN ID (12 bits)
 * @ethertype: Encapsulated EtherType
 */
static inline void packet_add_vlan(struct test_packet *pkt,
				    uint16_t vlan_id,
				    uint16_t ethertype)
{
	struct ethhdr *eth = (struct ethhdr *)pkt->data;
	struct vlan_hdr *vlan;

	/* Move existing data to make room for VLAN header */
	memmove(pkt->data + sizeof(struct ethhdr) + sizeof(struct vlan_hdr),
		pkt->data + sizeof(struct ethhdr),
		pkt->len - sizeof(struct ethhdr));

	/* Update Ethernet type to VLAN */
	eth->h_proto = htons(ETH_P_8021Q);

	/* Insert VLAN header */
	vlan = (struct vlan_hdr *)(pkt->data + sizeof(struct ethhdr));
	vlan->h_vlan_TCI = htons(vlan_id & 0x0FFF);
	vlan->h_vlan_encapsulated_proto = ethertype;

	pkt->len += sizeof(struct vlan_hdr);
}

/*
 * Build IPv4 header
 *
 * @src: Source IP address (network byte order)
 * @dst: Destination IP address (network byte order)
 * @proto: Protocol (IPPROTO_TCP, IPPROTO_UDP, etc.)
 * @ttl: Time to live
 */
static inline void packet_build_ipv4(struct test_packet *pkt,
				      uint32_t src,
				      uint32_t dst,
				      uint8_t proto,
				      uint8_t ttl)
{
	struct iphdr *iph = (struct iphdr *)(pkt->data + pkt->len);

	iph->version = 4;
	iph->ihl = 5;  /* 20 bytes, no options */
	iph->tos = 0;
	iph->tot_len = htons(sizeof(struct iphdr));
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = ttl;
	iph->protocol = proto;
	iph->check = 0;  /* Checksums not validated in tests */
	iph->saddr = src;
	iph->daddr = dst;

	pkt->len += sizeof(struct iphdr);
}

/*
 * Build IPv6 header
 *
 * @src: Source IPv6 address (16 bytes)
 * @dst: Destination IPv6 address (16 bytes)
 * @next_hdr: Next header type
 * @hop_limit: Hop limit
 */
static inline void packet_build_ipv6(struct test_packet *pkt,
				      const uint8_t *src,
				      const uint8_t *dst,
				      uint8_t next_hdr,
				      uint8_t hop_limit)
{
	struct ipv6hdr *ip6h = (struct ipv6hdr *)(pkt->data + pkt->len);

	ip6h->version = 6;
	ip6h->priority = 0;
	memset(ip6h->flow_lbl, 0, 3);
	ip6h->payload_len = 0;
	ip6h->nexthdr = next_hdr;
	ip6h->hop_limit = hop_limit;
	memcpy(&ip6h->saddr, src, 16);
	memcpy(&ip6h->daddr, dst, 16);

	pkt->len += sizeof(struct ipv6hdr);
}

/*
 * Set IPv4 fragment flags
 *
 * @df: Don't Fragment flag
 * @mf: More Fragments flag
 * @offset: Fragment offset (in 8-byte units)
 */
static inline void packet_set_ipv4_frag(struct test_packet *pkt,
					 int df, int mf,
					 uint16_t offset)
{
	struct ethhdr *eth = (struct ethhdr *)pkt->data;
	struct iphdr *iph;
	uint16_t frag_off = 0;
	size_t ip_offset = sizeof(struct ethhdr);

	/* Account for VLAN tags */
	while (eth->h_proto == htons(ETH_P_8021Q) ||
	       eth->h_proto == htons(ETH_P_8021AD)) {
		ip_offset += sizeof(struct vlan_hdr);
		eth = (struct ethhdr *)((uint8_t *)eth + sizeof(struct vlan_hdr));
	}

	iph = (struct iphdr *)(pkt->data + ip_offset);

	if (df)
		frag_off |= 0x4000;  /* DF flag */
	if (mf)
		frag_off |= 0x2000;  /* MF flag */
	frag_off |= (offset & 0x1FFF);  /* Fragment offset */

	iph->frag_off = htons(frag_off);
}

/*
 * Set IPv6 version field (for testing malformed packets)
 */
static inline void packet_set_ipv6_version(struct test_packet *pkt,
					    uint8_t version)
{
	struct ethhdr *eth = (struct ethhdr *)pkt->data;
	struct ipv6hdr *ip6h;
	size_t ip_offset = sizeof(struct ethhdr);

	/* Account for VLAN tags */
	while (eth->h_proto == htons(ETH_P_8021Q) ||
	       eth->h_proto == htons(ETH_P_8021AD)) {
		ip_offset += sizeof(struct vlan_hdr);
		eth = (struct ethhdr *)((uint8_t *)eth + sizeof(struct vlan_hdr));
	}

	ip6h = (struct ipv6hdr *)(pkt->data + ip_offset);
	ip6h->version = version;
}

/*
 * Truncate packet to simulate short/malformed packets
 */
static inline void packet_truncate(struct test_packet *pkt, size_t new_len)
{
	if (new_len < pkt->len)
		pkt->len = new_len;
}

/*
 * Helper: Standard MAC addresses for testing
 */
static const uint8_t MAC_SRC[ETH_ALEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t MAC_DST[ETH_ALEN] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
static const uint8_t MAC_MULTICAST[ETH_ALEN] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
static const uint8_t MAC_BROADCAST[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/*
 * Helper: Standard IP addresses for testing
 */
static const uint32_t IPV4_SRC = 0x0A000001;  /* 10.0.0.1 */
static const uint32_t IPV4_DST = 0x0A000002;  /* 10.0.0.2 */

static const uint8_t IPV6_SRC[16] = {
	0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};  /* 2001:db8::1 */

static const uint8_t IPV6_DST[16] = {
	0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};  /* 2001:db8::2 */

#endif /* __TEST_PACKET_BUILDER_H */
