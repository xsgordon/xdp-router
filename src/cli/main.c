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

/*
 * Compatibility wrappers for XDP attach/detach
 * Supports both old and new libbpf versions
 *
 * Strategy:
 * - Try bpf_xdp_attach/detach first (libbpf 1.0+, cleaner API)
 * - If compilation fails, use bpf_set_link_xdp_fd (libbpf 0.x, legacy)
 */

/* Detect if new API is available by checking libbpf version */
#if LIBBPF_MAJOR_VERSION >= 1
	/* New API available (libbpf 1.0+) */
	#define HAVE_BPF_XDP_ATTACH 1
#else
	#define HAVE_BPF_XDP_ATTACH 0
#endif

static int xdp_attach_wrapper(int ifindex, int prog_fd, __u32 flags)
{
#if HAVE_BPF_XDP_ATTACH
	/* Use new API (libbpf 1.0+) */
	return bpf_xdp_attach(ifindex, prog_fd, flags, NULL);
#else
	/* Use legacy API (libbpf 0.x) */
	return bpf_set_link_xdp_fd(ifindex, prog_fd, flags);
#endif
}

static int xdp_detach_wrapper(int ifindex)
{
#if HAVE_BPF_XDP_ATTACH
	/* Use new API (libbpf 1.0+) */
	return bpf_xdp_detach(ifindex, 0, NULL);
#else
	/* Use legacy API - pass -1 to detach (libbpf 0.x) */
	return bpf_set_link_xdp_fd(ifindex, -1, 0);
#endif
}

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

/*
 * Check if the loaded BPF program's map version matches this CLI version
 *
 * Returns:
 *   0 on success (version matches)
 *  -1 on error or version mismatch
 */
static int check_map_version(void)
{
	int config_fd;
	struct xdp_config cfg;
	__u32 key = 0;
	int err;

	/* Try to open config map */
	config_fd = bpf_obj_get("/sys/fs/bpf/xdp_router/config_map");
	if (config_fd < 0) {
		/* Map not found - likely no program attached */
		return -1;
	}

	/* Read config from map */
	err = bpf_map_lookup_elem(config_fd, &key, &cfg);
	close(config_fd);

	if (err) {
		fprintf(stderr, "Warning: failed to read config map\n");
		return -1;
	}

	/* Check version */
	if (cfg.version != XDP_ROUTER_MAP_VERSION) {
		fprintf(stderr, "Error: BPF map version mismatch\n");
		fprintf(stderr, "  CLI version:  0x%08x (%d.%d.%d)\n",
			XDP_ROUTER_MAP_VERSION,
			XDP_ROUTER_VERSION_MAJOR,
			XDP_ROUTER_VERSION_MINOR,
			XDP_ROUTER_VERSION_PATCH);
		fprintf(stderr, "  Map version:  0x%08x (%d.%d.%d)\n",
			cfg.version,
			(cfg.version >> 24) & 0xFF,
			(cfg.version >> 16) & 0xFF,
			cfg.version & 0xFFFF);
		fprintf(stderr, "\n");
		fprintf(stderr, "This usually means:\n");
		fprintf(stderr, "  - The XDP program was loaded by a different version of this CLI\n");
		fprintf(stderr, "  - You need to detach and reattach the XDP program\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "To fix:\n");
		fprintf(stderr, "  sudo xdp-router-cli detach <interface>\n");
		fprintf(stderr, "  sudo xdp-router-cli attach <interface>\n");
		return -1;
	}

	return 0;
}

/*
 * Aggregate PERCPU statistics for a single interface
 *
 * Reads per-CPU statistics from a PERCPU map and aggregates them into
 * a single struct. Handles the buffer allocation and summation logic.
 *
 * @param stats_fd     File descriptor of the packet_stats PERCPU map
 * @param ifindex      Interface index to read stats for
 * @param percpu_buf   Pre-allocated buffer for PERCPU values (nr_cpus * sizeof(struct if_stats))
 * @param nr_cpus      Number of possible CPUs
 * @param out          Output parameter for aggregated stats
 * @return 0 on success, -1 if interface has no stats
 */
static int aggregate_percpu_stats(int stats_fd, __u32 ifindex,
				   struct if_stats *percpu_buf,
				   int nr_cpus,
				   struct if_stats *out)
{
	int cpu, err;

	/* Lookup PERCPU values (one per CPU) */
	err = bpf_map_lookup_elem(stats_fd, &ifindex, percpu_buf);
	if (err)
		return -1;

	/* Sum stats across all CPUs */
	memset(out, 0, sizeof(*out));
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		out->rx_packets += percpu_buf[cpu].rx_packets;
		out->rx_bytes   += percpu_buf[cpu].rx_bytes;
		out->tx_packets += percpu_buf[cpu].tx_packets;
		out->tx_bytes   += percpu_buf[cpu].tx_bytes;
		out->dropped    += percpu_buf[cpu].dropped;
		out->errors     += percpu_buf[cpu].errors;
	}

	return 0;
}

/*
 * Print statistics for a single interface
 *
 * Displays packet statistics in a human-readable format. Includes
 * warning for saturated counters (reached UINT64_MAX).
 *
 * @param ifindex   Interface index
 * @param stats     Aggregated statistics to display
 */
static void print_interface_stats(__u32 ifindex, const struct if_stats *stats)
{
	char iface_name[IF_NAMESIZE] = {0};

	/* Get interface name (or use ifindex_N if lookup fails) */
	if (!if_indextoname(ifindex, iface_name))
		snprintf(iface_name, sizeof(iface_name), "ifindex_%d", ifindex);

	/* Print statistics */
	printf("Interface: %s (ifindex %d)\n", iface_name, ifindex);
	printf("  RX packets: %llu\n", (unsigned long long)stats->rx_packets);
	printf("  RX bytes:   %llu\n", (unsigned long long)stats->rx_bytes);
	printf("  TX packets: %llu\n", (unsigned long long)stats->tx_packets);
	printf("  TX bytes:   %llu\n", (unsigned long long)stats->tx_bytes);
	printf("  Dropped:    %llu\n", (unsigned long long)stats->dropped);
	printf("  Errors:     %llu\n", (unsigned long long)stats->errors);

	/* Check for counter saturation */
	if (stats->rx_packets == UINT64_MAX || stats->rx_bytes == UINT64_MAX ||
	    stats->tx_packets == UINT64_MAX || stats->tx_bytes == UINT64_MAX ||
	    stats->dropped == UINT64_MAX || stats->errors == UINT64_MAX) {
		printf("  ⚠️  WARNING: One or more counters have reached maximum value (saturated)\n");
	}
	printf("\n");
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
	err = xdp_attach_wrapper(ifindex, bpf_program__fd(skel->progs.xdp_router_main),
				 xdp_flags);
	if (err) {
		fprintf(stderr, "Error: failed to attach XDP program to %s: %s\n",
			ifname, strerror(-err));
		fprintf(stderr, "Note: XDP program may already be attached. Use 'detach' first.\n");
		goto cleanup;
	}

	/* Initialize config map with version and default settings */
	{
		struct xdp_config cfg = {
			.version = XDP_ROUTER_MAP_VERSION,
			.features = FEATURE_IPV4_BIT | FEATURE_IPV6_BIT,
			.log_level = 0,
			.max_srv6_sids = 0,
		};
		__u32 key = 0;
		int config_fd = bpf_map__fd(skel->maps.config_map);

		err = bpf_map_update_elem(config_fd, &key, &cfg, BPF_ANY);
		if (err) {
			fprintf(stderr, "Warning: failed to initialize config map: %s\n", strerror(-err));
			fprintf(stderr, "Program may not function correctly.\n");
		}
	}

	printf("Successfully attached XDP program to %s (ifindex %d)\n",
	       ifname, ifindex);
	printf("Mode: SKB (generic XDP)\n");
	printf("Map version: 0x%08x (%d.%d.%d)\n",
	       XDP_ROUTER_MAP_VERSION,
	       XDP_ROUTER_VERSION_MAJOR,
	       XDP_ROUTER_VERSION_MINOR,
	       XDP_ROUTER_VERSION_PATCH);
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
	err = xdp_detach_wrapper(ifindex);
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
	struct if_stats stats, *percpu_stats;
	const char *ifname = NULL;
	int target_ifindex = -1;
	int nr_cpus;

	/* Check for required privileges */
	if (geteuid() != 0) {
		fprintf(stderr, "Error: This command requires root privileges\n");
		fprintf(stderr, "Please run with sudo: sudo xdp-router-cli stats [--interface <iface>]\n");
		return -1;
	}

	/* Check map version compatibility */
	err = check_map_version();
	if (err) {
		/* Error message already printed by check_map_version() */
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

	/* Allocate buffer for PERCPU map values */
	nr_cpus = libbpf_num_possible_cpus();
	if (nr_cpus < 0) {
		fprintf(stderr, "Error: failed to get number of CPUs: %s\n", strerror(-nr_cpus));
		close(stats_fd);
		return -1;
	}

	percpu_stats = calloc(nr_cpus, sizeof(struct if_stats));
	if (!percpu_stats) {
		fprintf(stderr, "Error: failed to allocate memory for PERCPU stats\n");
		close(stats_fd);
		return -1;
	}

	printf("\n");
	printf("=== XDP Router Statistics ===\n");
	printf("\n");

	/* Iterate through all interfaces */
	for (i = 0; i < MAX_INTERFACES; i++) {
		/* Skip if filtering by interface */
		if (target_ifindex >= 0 && i != target_ifindex)
			continue;

		/* Aggregate PERCPU stats for this interface */
		err = aggregate_percpu_stats(stats_fd, i, percpu_stats, nr_cpus, &stats);
		if (err)
			continue;  /* Interface has no stats */

		/* Skip interfaces with no traffic */
		if (stats.rx_packets == 0 && stats.tx_packets == 0 &&
		    stats.dropped == 0 && stats.errors == 0)
			continue;

		/* Print statistics for this interface */
		print_interface_stats(i, &stats);
	}

	free(percpu_stats);
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
