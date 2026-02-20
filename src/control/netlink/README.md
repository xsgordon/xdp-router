# Netlink Monitoring Directory

This directory will contain the Netlink monitoring subsystem for the control plane daemon.

Planned modules:
- monitor.c - Main Netlink event loop
- route.c - Route update handlers (RTM_NEWROUTE, RTM_DELROUTE)
- neigh.c - Neighbor update handlers (RTM_NEWNEIGH, RTM_DELNEIGH)
- link.c - Link update handlers (RTM_NEWLINK, RTM_DELLINK)

Monitors kernel routing events and synchronizes state to eBPF maps.
