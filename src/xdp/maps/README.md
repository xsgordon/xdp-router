# XDP Maps Directory

This directory will contain eBPF map definitions for the XDP data plane.

Planned maps:
- stats.h - Packet statistics maps
- config.h - Configuration maps
- srv6.h - SRv6 local SID and policy maps

Maps provide shared state between the XDP program and user-space control plane.
