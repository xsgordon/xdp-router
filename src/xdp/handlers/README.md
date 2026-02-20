# XDP Handlers Directory

This directory will contain protocol-specific packet handlers for the XDP data plane.

Planned handlers:
- ipv4.c - IPv4 forwarding logic
- ipv6.c - IPv6 forwarding logic
- srv6_end.c - SRv6 End action handler
- srv6_encap.c - SRv6 encapsulation handler
- srv6_decap.c - SRv6 decapsulation handler
- isis.c - IS-IS punt handler

Each handler implements the forwarding logic for a specific protocol.
