# XDP Parsers Directory

This directory will contain protocol parsers for the XDP data plane.

Planned parsers:
- ipv4.c - IPv4 packet parser
- ipv6.c - IPv6 packet parser
- srv6.c - SRv6 routing header parser
- isis.c - IS-IS packet detection

Each parser extracts header information and populates the parser_ctx structure.
