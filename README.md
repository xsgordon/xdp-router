# xdp-router

A high-performance, XDP-based routing engine for Linux that provides line-rate packet forwarding while maintaining full integration with the kernel's control plane.

## Overview

xdp-router is a hybrid routing solution that combines:
- **Linux Kernel Control Plane**: FRR populates the kernel FIB via standard Netlink
- **XDP Data Plane**: eBPF programs perform fast-path packet forwarding at the NIC driver layer
- **User-Space Daemon**: Synchronizes advanced features (SRv6, custom policies) to eBPF maps

### Key Features

- **High Performance**: Target >20 Million Packets Per Second (MPPS) per CPU core
- **Standard Control Plane**: Full FRR integration (BGP, OSPF, IS-IS)
- **SRv6 Support**: Segment Routing over IPv6 with End, Encap, and Decap actions
- **Kernel Native**: Works with standard Linux tools (ip, tcpdump, ping)
- **Modular Design**: Easy to extend with new protocols and features
- **Observable**: Comprehensive statistics and debugging capabilities

### Why xdp-router?

Unlike DPDK-based solutions that bypass the kernel entirely, xdp-router:
- Uses standard Linux drivers (no special NIC configuration)
- Maintains kernel visibility into all traffic
- Integrates seamlessly with existing Linux networking
- Supports standard troubleshooting tools
- Provides operational simplicity

## Architecture

```
User Space:    FRR → xdp-routerd → xdp-router-cli
                ↓         ↓              ↓
Kernel:    Kernel FIB  eBPF Maps    Statistics
                ↓         ↓
XDP Layer: XDP Program (fast path forwarding)
                ↓
Hardware:       NIC
```

For detailed architecture documentation, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Project Status

**Current Phase**: Initial Development

This project is in early development. See [PLAN.md](PLAN.md) for the detailed implementation roadmap.

## Building

### Prerequisites

- Linux Kernel 5.10+ with XDP support
- clang 10+ with BPF target support
- libbpf >= 0.5
- libnl-3
- libelf
- bpftool
- iproute2 with seg6 support

### Build Instructions

```bash
# Install dependencies (Fedora/RHEL)
sudo dnf install -y clang llvm libbpf-devel libnl3-devel elfutils-libelf-devel bpftool

# Install dependencies (Ubuntu/Debian)
sudo apt install -y clang llvm libbpf-dev libnl-3-dev libelf-dev linux-tools-common

# Build
make

# Install
sudo make install
```

## Quick Start

```bash
# Load XDP program on interface
sudo xdp-router-cli attach eth0

# View statistics
sudo xdp-router-cli stats

# View SRv6 local SIDs
sudo xdp-router-cli srv6 sids

# Detach XDP program
sudo xdp-router-cli detach eth0
```

## Configuration

Configuration file: `/etc/xdp-router/config.yaml`

```yaml
features:
  ipv4: true
  ipv6: true
  srv6:
    enabled: true
    behaviors: [End, End.DT4, End.DT6]

interfaces:
  - name: eth0
    mode: native  # or offload
```

## Performance

Preliminary benchmarks (target goals):

| Metric | Target | Notes |
|--------|--------|-------|
| Throughput | >20 MPPS/core | 64-byte packets |
| SRv6 Overhead | <5% | vs baseline forwarding |
| Route Update Latency | <10ms | FRR → Data plane |

## Comparison with DPDK

| Feature | DPDK (e.g., Grout) | xdp-router |
|---------|-------------------|------------|
| Kernel Integration | Bypass | Integrated |
| NIC Management | DPDK PMD | Linux drivers |
| Routing Table | Custom | Kernel FIB |
| Control Plane | Custom/Complex | FRR (standard) |
| Tooling | DPDK-specific | Standard Linux |
| Operational Complexity | High | Low |

## Protocol Support

### Current
- IPv4 forwarding
- IPv6 forwarding
- SRv6 (End, End.DT4, End.DT6)
- IS-IS (control plane punt)
- BGP (via FRR)
- OSPF (via FRR)

### Planned
- MPLS
- Additional SRv6 functions (End.DX2, End.DX4, End.DX6, End.B6.Encaps)
- IPsec acceleration
- Flow tracking

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Detailed system architecture
- [PLAN.md](PLAN.md) - Implementation roadmap
- [docs/](docs/) - Additional documentation

## Development

### Project Structure

```
xdp-router/
├── src/
│   ├── xdp/          # XDP/eBPF data plane
│   ├── control/      # User-space control plane daemon
│   ├── common/       # Shared headers/definitions
│   └── cli/          # CLI tools
├── lib/              # Reusable libraries
├── tests/            # Test suite
└── tools/            # Development tools
```

### Testing

```bash
# Run unit tests
make test

# Run integration tests
make test-integration

# Run performance tests
make test-performance
```

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

[License TBD]

## Related Projects

- [FRR](https://frrouting.org/) - Control plane routing daemon
- [libbpf](https://github.com/libbpf/libbpf) - BPF library
- [xdp-project](https://github.com/xdp-project) - XDP development resources
- [DPDK Grout](https://github.com/DPDK/grout) - DPDK-based router (alternative approach)

## References

- [XDP Tutorial](https://github.com/xdp-project/xdp-tutorial)
- [Linux XDP Documentation](https://www.kernel.org/doc/html/latest/networking/xdp.html)
- [SRv6 Architecture (RFC 8402)](https://datatracker.ietf.org/doc/html/rfc8402)
- [IS-IS Protocol](https://datatracker.ietf.org/doc/html/rfc1142)

## Contact

[Contact information TBD]
