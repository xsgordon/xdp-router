# SRv6 Control Plane Directory

This directory will contain SRv6-specific control plane logic.

Planned modules:
- srv6_manager.c - Main SRv6 management
- local_sids.c - Local SID (MySID) table management
- policies.c - SRv6 encapsulation policy management
- seg6_netlink.c - Handle RTM_NEWSEG6LOCAL messages

Manages SRv6 configuration and synchronizes to eBPF maps.
