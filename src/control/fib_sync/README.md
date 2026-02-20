# FIB Synchronization Directory

This directory will contain FIB (Forwarding Information Base) synchronization logic.

Planned modules:
- fib_sync.c - FIB state synchronization
- custom_routes.c - Custom route handling for features beyond bpf_fib_lookup()

Handles synchronization between kernel FIB and custom eBPF maps for advanced features.
