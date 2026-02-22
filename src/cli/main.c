// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_link.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "common/common.h"
#include "xdp_router.skel.h"

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [options]\n\n", prog);
	printf("Commands:\n");
	printf("  attach <interface>           Attach XDP program to interface\n");
	printf("  detach <interface>           Detach XDP program from interface\n");
	printf("  stats [--interface <iface>]  Show statistics\n");
	printf("  srv6 sids                    Show SRv6 local SIDs\n");
	printf("  srv6 policies                Show SRv6 encap policies\n");
	printf("  debug drop-reasons           Show packet drop reasons\n");
	printf("  version                      Show version information\n");
	printf("  help                         Show this help\n");
	printf("\n");
	printf("Examples:\n");
	printf("  %s attach eth0\n", prog);
	printf("  %s stats --interface eth0\n", prog);
	printf("  %s srv6 sids\n", prog);
}

static void print_version(void)
{
	printf("xdp-router-cli version %d.%d.%d\n",
	       XDP_ROUTER_VERSION_MAJOR,
	       XDP_ROUTER_VERSION_MINOR,
	       XDP_ROUTER_VERSION_PATCH);
}

static int cmd_attach(int argc, char **argv)
{
	struct xdp_router_bpf *skel;
	int ifindex, err;
	__u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;

	/* Check for required privileges */
	if (geteuid() != 0) {
		fprintf(stderr, "Error: This command requires root privileges\n");
		fprintf(stderr, "Please run with sudo: sudo xdp-router-cli attach <interface>\n");
		return -1;
	}

	if (argc < 1) {
		fprintf(stderr, "Error: interface name required\n");
		fprintf(stderr, "Usage: xdp-router-cli attach <interface>\n");
		return -1;
	}

	const char *ifname = argv[0];

	/* Get interface index */
	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		fprintf(stderr, "Error: interface '%s' not found: %s\n",
			ifname, strerror(errno));
		return -1;
	}

	/* Open BPF skeleton */
	skel = xdp_router_bpf__open();
	if (!skel) {
		fprintf(stderr, "Error: failed to open BPF skeleton\n");
		return -1;
	}

	/* Load BPF program */
	err = xdp_router_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Error: failed to load BPF program: %d\n", err);
		goto cleanup;
	}

	/* Pin maps to BPF filesystem for persistent access */
	err = bpf_object__pin_maps(skel->obj, "/sys/fs/bpf/xdp_router");
	if (err && errno != EEXIST) {
		fprintf(stderr, "Warning: failed to pin BPF maps: %s\n", strerror(errno));
		fprintf(stderr, "Stats command may not work.\n");
	}

	/* Attach XDP program to interface */
	err = bpf_xdp_attach(ifindex, bpf_program__fd(skel->progs.xdp_router_main),
			     xdp_flags, NULL);
	if (err) {
		fprintf(stderr, "Error: failed to attach XDP program to %s: %s\n",
			ifname, strerror(-err));
		fprintf(stderr, "Note: XDP program may already be attached. Use 'detach' first.\n");
		goto cleanup;
	}

	printf("Successfully attached XDP program to %s (ifindex %d)\n",
	       ifname, ifindex);
	printf("Mode: SKB (generic XDP)\n");
	printf("\n");
	printf("Note: Program will remain attached even after this command exits.\n");
	printf("Use 'xdp-router-cli detach %s' to remove it.\n", ifname);

	/* Don't destroy skeleton - program stays attached */
	/* User must explicitly detach */
	xdp_router_bpf__destroy(skel);
	return 0;

cleanup:
	xdp_router_bpf__destroy(skel);
	return -1;
}

static int cmd_detach(int argc, char **argv)
{
	int ifindex, err;

	/* Check for required privileges */
	if (geteuid() != 0) {
		fprintf(stderr, "Error: This command requires root privileges\n");
		fprintf(stderr, "Please run with sudo: sudo xdp-router-cli detach <interface>\n");
		return -1;
	}

	if (argc < 1) {
		fprintf(stderr, "Error: interface name required\n");
		fprintf(stderr, "Usage: xdp-router-cli detach <interface>\n");
		return -1;
	}

	const char *ifname = argv[0];

	/* Get interface index */
	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		fprintf(stderr, "Error: interface '%s' not found: %s\n",
			ifname, strerror(errno));
		return -1;
	}

	/* Detach XDP program */
	err = bpf_xdp_detach(ifindex, 0, NULL);
	if (err) {
		fprintf(stderr, "Error: failed to detach XDP program from %s: %s\n",
			ifname, strerror(-err));
		fprintf(stderr, "Note: No XDP program may be attached to this interface.\n");
		return -1;
	}

	/* Clean up pinned maps (optional - maps can be reused) */
	/* Only unpin if this was the last interface using them */
	/* For now, leave maps pinned for stats access */

	printf("Successfully detached XDP program from %s (ifindex %d)\n",
	       ifname, ifindex);
	printf("\n");
	printf("Note: BPF maps remain pinned at /sys/fs/bpf/xdp_router/\n");
	printf("Statistics are still accessible via 'xdp-router-cli stats'\n");

	return 0;
}

static int cmd_stats(int argc, char **argv)
{
	int stats_fd, i, err;
	struct if_stats stats;
	const char *ifname = NULL;
	int target_ifindex = -1;

	/* Check for required privileges */
	if (geteuid() != 0) {
		fprintf(stderr, "Error: This command requires root privileges\n");
		fprintf(stderr, "Please run with sudo: sudo xdp-router-cli stats [--interface <iface>]\n");
		return -1;
	}

	/* Parse optional --interface argument */
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--interface") || !strcmp(argv[i], "-i")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "Error: --interface requires an argument\n");
				fprintf(stderr, "Usage: xdp-router-cli stats [--interface <name>]\n");
				return -1;
			}
			ifname = argv[i + 1];
			target_ifindex = if_nametoindex(ifname);
			if (!target_ifindex) {
				fprintf(stderr, "Error: interface '%s' not found\n", ifname);
				return -1;
			}
		}
	}

	/* Open stats map */
	stats_fd = bpf_obj_get("/sys/fs/bpf/xdp_router/packet_stats");
	if (stats_fd < 0) {
		fprintf(stderr, "Error: failed to open stats map: %s\n", strerror(errno));
		fprintf(stderr, "Note: XDP program may not be attached. Use 'attach' first.\n");
		return -1;
	}

	printf("\n");
	printf("=== XDP Router Statistics ===\n");
	printf("\n");

	/* Iterate through all interfaces */
	for (i = 0; i < MAX_INTERFACES; i++) {
		__u32 key = i;

		/* Skip if filtering by interface */
		if (target_ifindex >= 0 && i != target_ifindex)
			continue;

		err = bpf_map_lookup_elem(stats_fd, &key, &stats);
		if (err)
			continue;

		/* Skip interfaces with no traffic */
		if (stats.rx_packets == 0 && stats.tx_packets == 0 &&
		    stats.dropped == 0 && stats.errors == 0)
			continue;

		char iface_name[IF_NAMESIZE] = {0};
		if (!if_indextoname(i, iface_name))
			snprintf(iface_name, sizeof(iface_name), "ifindex_%d", i);

		printf("Interface: %s (ifindex %d)\n", iface_name, i);
		printf("  RX packets: %llu\n", (unsigned long long)stats.rx_packets);
		printf("  RX bytes:   %llu\n", (unsigned long long)stats.rx_bytes);
		printf("  TX packets: %llu\n", (unsigned long long)stats.tx_packets);
		printf("  TX bytes:   %llu\n", (unsigned long long)stats.tx_bytes);
		printf("  Dropped:    %llu\n", (unsigned long long)stats.dropped);
		printf("  Errors:     %llu\n", (unsigned long long)stats.errors);

		/* Check for counter saturation */
		if (stats.rx_packets == UINT64_MAX || stats.rx_bytes == UINT64_MAX ||
		    stats.tx_packets == UINT64_MAX || stats.tx_bytes == UINT64_MAX ||
		    stats.dropped == UINT64_MAX || stats.errors == UINT64_MAX) {
			printf("  ⚠️  WARNING: One or more counters have reached maximum value (saturated)\n");
		}
		printf("\n");
	}

	close(stats_fd);
	return 0;
}

static int cmd_srv6(int argc, char **argv)
{
	printf("srv6: not yet implemented (Phase 4)\n");
	return -1;
}

static int cmd_debug(int argc, char **argv)
{
	printf("debug: not yet implemented (Phase 6)\n");
	return -1;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	const char *cmd = argv[1];

	if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) {
		print_usage(argv[0]);
		return 0;
	}

	if (!strcmp(cmd, "version") || !strcmp(cmd, "--version")) {
		print_version();
		return 0;
	}

	if (!strcmp(cmd, "attach")) {
		return cmd_attach(argc - 2, argv + 2);
	}

	if (!strcmp(cmd, "detach")) {
		return cmd_detach(argc - 2, argv + 2);
	}

	if (!strcmp(cmd, "stats")) {
		return cmd_stats(argc - 2, argv + 2);
	}

	if (!strcmp(cmd, "srv6")) {
		return cmd_srv6(argc - 2, argv + 2);
	}

	if (!strcmp(cmd, "debug")) {
		return cmd_debug(argc - 2, argv + 2);
	}

	fprintf(stderr, "Unknown command: %s\n", cmd);
	print_usage(argv[0]);
	return 1;
}
