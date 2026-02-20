// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "common/common.h"

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
	printf("attach: not yet implemented (Phase 2)\n");
	return -1;
}

static int cmd_detach(int argc, char **argv)
{
	printf("detach: not yet implemented (Phase 2)\n");
	return -1;
}

static int cmd_stats(int argc, char **argv)
{
	printf("stats: not yet implemented (Phase 6)\n");
	return -1;
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
