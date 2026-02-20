// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>

#include <bpf/libbpf.h>

#include "common/common.h"

static volatile sig_atomic_t running = 1;

static void sig_handler(int sig)
{
	running = 0;
}

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
			   va_list args)
{
	/* Filter debug messages unless verbose mode */
	if (level == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, format, args);
}

int main(int argc, char **argv)
{
	int err = 0;

	/* Set up libbpf logging */
	libbpf_set_print(libbpf_print_fn);

	/* Set up signal handling */
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	printf("xdp-routerd starting (version %d.%d.%d)\n",
	       XDP_ROUTER_VERSION_MAJOR,
	       XDP_ROUTER_VERSION_MINOR,
	       XDP_ROUTER_VERSION_PATCH);

	/* Phase 3: Initialize netlink socket, BPF maps, etc. */
	/* For now, this is a placeholder */

	printf("xdp-routerd: daemon mode not yet implemented\n");
	printf("xdp-routerd: will be implemented in Phase 3\n");

	/* Main event loop */
	while (running) {
		sleep(1);
	}

	printf("xdp-routerd shutting down\n");
	return err;
}
