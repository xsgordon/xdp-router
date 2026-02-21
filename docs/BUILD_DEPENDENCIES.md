# Build Dependencies - xdp-router

This document provides comprehensive information about build dependencies required to compile xdp-router and instructions for installing them on common Linux distributions.

---

## Table of Contents

1. [Overview](#overview)
2. [Required Dependencies](#required-dependencies)
3. [Optional Dependencies](#optional-dependencies)
4. [Installation by Distribution](#installation-by-distribution)
5. [Verification](#verification)
6. [Troubleshooting](#troubleshooting)
7. [Minimum Versions](#minimum-versions)

---

## Overview

xdp-router is an eBPF/XDP-based routing engine that requires:
- **BPF compilation tools** (clang, llvm)
- **BPF development libraries** (libbpf, kernel headers)
- **BPF utilities** (bpftool)
- **Standard development tools** (gcc, make)
- **Networking libraries** (libnl3, libelf)

The project uses CO-RE (Compile Once, Run Everywhere) which requires:
- Linux kernel ≥ 5.8 with BTF enabled
- bpftool with BTF support
- libbpf ≥ 0.3

---

## Required Dependencies

### Core Build Tools

| Package | Purpose | Fedora/RHEL | Ubuntu/Debian |
|---------|---------|-------------|---------------|
| **clang** | C compiler for BPF programs | `clang` | `clang` |
| **llvm** | LLVM backend for BPF | `llvm` | `llvm` |
| **gcc** | C compiler for user-space | `gcc` | `gcc` |
| **make** | Build automation | `make` | `make` |

### BPF Development

| Package | Purpose | Fedora/RHEL | Ubuntu/Debian |
|---------|---------|-------------|---------------|
| **libbpf-devel** | BPF library headers | `libbpf-devel` | `libbpf-dev` |
| **bpftool** | BPF inspection and skeleton generation | `bpftool` | `linux-tools-common`<br>`linux-tools-generic` |
| **kernel-headers** | Kernel header files | `kernel-headers`<br>`kernel-devel` | `linux-headers-$(uname -r)` |

### Networking Libraries

| Package | Purpose | Fedora/RHEL | Ubuntu/Debian |
|---------|---------|-------------|---------------|
| **libnl3-devel** | Netlink library | `libnl3-devel` | `libnl-3-dev`<br>`libnl-route-3-dev` |
| **libelf-devel** | ELF parsing library | `elfutils-libelf-devel` | `libelf-dev` |

### System Requirements

| Requirement | Minimum Version | Check Command |
|-------------|-----------------|---------------|
| **Linux Kernel** | 5.8+ with BTF | `uname -r`<br>`cat /sys/kernel/btf/vmlinux \| head -c 10` |
| **libbpf** | 0.3+ | `pkg-config --modversion libbpf` |
| **clang** | 10+ | `clang --version` |
| **LLVM** | 10+ | `llc --version` |

---

## Optional Dependencies

### Development Tools

| Package | Purpose | Installation |
|---------|---------|--------------|
| **clang-format** | Code formatting | Fedora: `clang-tools-extra`<br>Ubuntu: `clang-format` |
| **clang-tidy** | Static analysis | Fedora: `clang-tools-extra`<br>Ubuntu: `clang-tidy` |
| **git** | Version control | `git` (both distros) |
| **python3** | Test scripts | `python3` (both distros) |
| **scapy** | Packet crafting for tests | `python3-scapy` (both distros) |

### Testing Tools

| Package | Purpose | Installation |
|---------|---------|--------------|
| **iproute2** | Network configuration | Usually pre-installed |
| **iputils** | Ping and network tools | Usually pre-installed |
| **tcpdump** | Packet capture | `tcpdump` (both distros) |
| **netcat** | Network testing | `nmap-ncat` (Fedora)<br>`netcat-openbsd` (Ubuntu) |

---

## Installation by Distribution

### Fedora / RHEL / CentOS Stream

#### Minimal Installation (Required Only)

```bash
# Update package database
sudo dnf update -y

# Install core build tools
sudo dnf install -y clang llvm gcc make

# Install BPF development tools
sudo dnf install -y libbpf-devel bpftool kernel-headers kernel-devel

# Install networking libraries
sudo dnf install -y libnl3-devel elfutils-libelf-devel

# Verify kernel has BTF support
if [ -f /sys/kernel/btf/vmlinux ]; then
    echo "✓ Kernel BTF support available"
else
    echo "✗ Kernel BTF support NOT available - upgrade kernel to 5.8+"
fi
```

#### Full Installation (Required + Optional)

```bash
# Install minimal dependencies first (see above)

# Install optional development tools
sudo dnf install -y clang-tools-extra git python3 python3-scapy

# Install testing tools
sudo dnf install -y iproute tcpdump nmap-ncat

# Verify installation
make check-deps
```

#### RHEL 8 Specific Notes

```bash
# Enable CodeReady Builder repository for some packages
sudo subscription-manager repos --enable codeready-builder-for-rhel-8-x86_64-rpms

# Or for CentOS 8
sudo dnf config-manager --set-enabled powertools

# Then install as above
```

---

### Ubuntu / Debian

#### Minimal Installation (Required Only)

```bash
# Update package database
sudo apt update

# Install core build tools
sudo apt install -y clang llvm gcc make

# Install BPF development tools
# Note: linux-tools package version must match kernel version
KERNEL_VERSION=$(uname -r)
sudo apt install -y libbpf-dev linux-tools-common linux-tools-${KERNEL_VERSION}

# If linux-tools for your kernel version is not available:
sudo apt install -y linux-tools-generic

# Install kernel headers
sudo apt install -y linux-headers-${KERNEL_VERSION}

# Install networking libraries
sudo apt install -y libnl-3-dev libnl-route-3-dev libelf-dev

# Verify kernel has BTF support
if [ -f /sys/kernel/btf/vmlinux ]; then
    echo "✓ Kernel BTF support available"
else
    echo "✗ Kernel BTF support NOT available - upgrade kernel to 5.8+"
fi
```

#### Full Installation (Required + Optional)

```bash
# Install minimal dependencies first (see above)

# Install optional development tools
sudo apt install -y clang-format clang-tidy git python3 python3-scapy

# Install testing tools
sudo apt install -y iproute2 tcpdump netcat-openbsd

# Verify installation
make check-deps
```

#### Ubuntu 20.04 LTS Specific Notes

```bash
# Ubuntu 20.04 ships with kernel 5.4, which lacks BTF
# Upgrade to HWE (Hardware Enablement) kernel for 5.8+

# Install HWE kernel
sudo apt install -y linux-generic-hwe-20.04

# Reboot to new kernel
sudo reboot

# After reboot, verify kernel version
uname -r  # Should be 5.8 or higher

# Then proceed with normal installation
```

#### Debian 11 (Bullseye) Specific Notes

```bash
# Debian 11 includes kernel 5.10 with BTF support
# Standard installation should work

# If bpftool is not available in standard repos:
sudo apt install -y linux-tools-common linux-tools-$(uname -r)

# Verify bpftool is available
which bpftool || echo "bpftool not found - check linux-tools installation"
```

---

### Arch Linux

```bash
# Update package database
sudo pacman -Syu

# Install dependencies
sudo pacman -S clang llvm gcc make libbpf bpf linux-headers

# Install optional tools
sudo pacman -S clang git python python-scapy iproute2 tcpdump gnu-netcat

# Note: bpftool is included in the 'bpf' package
```

---

### Alpine Linux

```bash
# Update package database
sudo apk update

# Install dependencies
sudo apk add clang llvm gcc make musl-dev libbpf-dev bpftool linux-headers

# Install networking libraries
sudo apk add libnl3-dev elfutils-dev

# Install optional tools
sudo apk add clang-extra-tools git python3 py3-scapy iproute2 tcpdump netcat-openbsd
```

---

## Verification

After installing dependencies, verify the installation:

### Automated Verification

```bash
# Use the built-in dependency checker
cd /path/to/xdp-router
make check-deps
```

Expected output:
```
Checking build dependencies...
✓ All dependencies satisfied
```

### Manual Verification

```bash
# Check each tool individually

# 1. Clang (BPF compiler)
clang --version
# Expected: clang version 10.0.0 or higher

# 2. LLVM
llc --version
# Expected: LLVM version 10.0.0 or higher

# 3. GCC
gcc --version
# Expected: gcc (GCC) 8.0.0 or higher

# 4. bpftool
bpftool version
# Expected: bpftool v5.8 or higher

# 5. libbpf
pkg-config --modversion libbpf
# Expected: 0.3.0 or higher

# 6. libnl-3
pkg-config --modversion libnl-3.0
# Expected: 3.2.0 or higher

# 7. libelf
pkg-config --modversion libelf
# Expected: 0.170 or higher

# 8. Kernel BTF
ls -lh /sys/kernel/btf/vmlinux
# Expected: File exists and is non-zero size

# 9. Kernel version
uname -r
# Expected: 5.8.0 or higher
```

### Quick Test Build

```bash
# Try a minimal build to verify everything works
cd /path/to/xdp-router
make clean
make check-deps
make

# Expected output:
#   GEN      build/vmlinux.h
#   BPF      build/xdp_router.bpf.o
#   SKEL     build/xdp_router.skel.h
#   CC       build/control/main.o
#   LD       build/xdp-routerd
#   CC       build/cli/main.o
#   LD       build/xdp-router-cli
```

---

## Troubleshooting

### Issue: "bpftool: command not found"

**Fedora/RHEL:**
```bash
sudo dnf install -y bpftool
```

**Ubuntu/Debian:**
```bash
# Install kernel-specific tools
sudo apt install -y linux-tools-$(uname -r)

# Or generic tools
sudo apt install -y linux-tools-generic

# bpftool is usually at:
/usr/lib/linux-tools/$(uname -r)/bpftool

# Create symlink if needed
sudo ln -s /usr/lib/linux-tools/$(uname -r)/bpftool /usr/local/bin/bpftool
```

---

### Issue: "BTF not available"

**Symptom:**
```
WARNING: /sys/kernel/btf/vmlinux not found, BTF support unavailable
```

**Solution 1: Verify kernel version**
```bash
uname -r
# Must be 5.8 or higher
```

**Solution 2: Check if kernel was compiled with BTF**
```bash
grep CONFIG_DEBUG_INFO_BTF /boot/config-$(uname -r)
# Should output: CONFIG_DEBUG_INFO_BTF=y
```

**Solution 3: Upgrade kernel**

Fedora:
```bash
sudo dnf upgrade kernel
sudo reboot
```

Ubuntu:
```bash
# Install HWE kernel if on 20.04
sudo apt install -y linux-generic-hwe-20.04
sudo reboot

# Or upgrade to newer Ubuntu release
```

---

### Issue: "libbpf not found"

**Symptom:**
```
ERROR: libbpf development files not found
```

**Fedora/RHEL:**
```bash
sudo dnf install -y libbpf-devel

# Verify
pkg-config --modversion libbpf
```

**Ubuntu/Debian:**
```bash
sudo apt install -y libbpf-dev

# Verify
pkg-config --modversion libbpf
```

**Build from source (if distro version too old):**
```bash
git clone https://github.com/libbpf/libbpf.git
cd libbpf/src
make
sudo make install

# Update library cache
sudo ldconfig
```

---

### Issue: "libnl-3 not found"

**Fedora/RHEL:**
```bash
sudo dnf install -y libnl3-devel
```

**Ubuntu/Debian:**
```bash
sudo apt install -y libnl-3-dev libnl-route-3-dev
```

---

### Issue: Clang too old

**Symptom:**
```
error: unknown argument: '-mllvm'
```

**Solution:**

Fedora:
```bash
# Install from newer repository
sudo dnf install -y clang llvm

# Or install specific version
sudo dnf install -y clang-13 llvm-13
```

Ubuntu:
```bash
# Add LLVM repository for newer version
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
sudo add-apt-repository "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-14 main"
sudo apt update
sudo apt install -y clang-14 llvm-14

# Set as default
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 100
```

---

### Issue: Permission denied when loading XDP program

**Symptom:**
```
Error: Operation not permitted
```

**Solution:**

You need CAP_BPF or CAP_NET_ADMIN capabilities:

```bash
# Option 1: Run as root
sudo ip link set dev eth0 xdp obj build/xdp_router.bpf.o sec xdp

# Option 2: Grant capabilities to binary (production)
sudo setcap cap_net_admin,cap_bpf+ep build/xdp-routerd

# Option 3: Add user to appropriate group (varies by distro)
```

---

## Minimum Versions

The following minimum versions are required:

| Component | Minimum Version | Recommended Version | Notes |
|-----------|----------------|---------------------|-------|
| **Linux Kernel** | 5.8 | 5.15+ | BTF required |
| **clang** | 10.0 | 13.0+ | Better BPF support |
| **LLVM** | 10.0 | 13.0+ | Better optimizations |
| **libbpf** | 0.3 | 0.6+ | CO-RE support |
| **bpftool** | 5.8 | 5.15+ | Must match kernel |
| **gcc** | 8.0 | 11.0+ | For user-space code |
| **make** | 4.0 | 4.3+ | Standard build tool |
| **libnl** | 3.2 | 3.5+ | Netlink support |
| **libelf** | 0.170 | 0.186+ | ELF parsing |

### Checking Versions

```bash
# Create a version check script
cat > check-versions.sh << 'EOF'
#!/bin/bash

echo "=== Version Check ==="

echo -n "Kernel: "
uname -r

echo -n "clang: "
clang --version | head -1 | grep -oP '\d+\.\d+\.\d+'

echo -n "LLVM: "
llc --version | grep version | head -1 | grep -oP '\d+\.\d+\.\d+'

echo -n "gcc: "
gcc --version | head -1 | grep -oP '\d+\.\d+\.\d+'

echo -n "libbpf: "
pkg-config --modversion libbpf 2>/dev/null || echo "NOT FOUND"

echo -n "bpftool: "
bpftool version 2>/dev/null | grep -oP 'v\d+\.\d+' || echo "NOT FOUND"

echo -n "libnl-3: "
pkg-config --modversion libnl-3.0 2>/dev/null || echo "NOT FOUND"

echo -n "libelf: "
pkg-config --modversion libelf 2>/dev/null || echo "NOT FOUND"

echo -n "BTF: "
if [ -f /sys/kernel/btf/vmlinux ]; then
    echo "Available"
else
    echo "NOT AVAILABLE"
fi

echo "===================="
EOF

chmod +x check-versions.sh
./check-versions.sh
```

---

## Container-Based Development

If you cannot install dependencies system-wide, use Docker:

### Docker Build Environment

```dockerfile
# Dockerfile.build
FROM fedora:37

RUN dnf install -y \
    clang llvm gcc make \
    libbpf-devel bpftool kernel-headers kernel-devel \
    libnl3-devel elfutils-libelf-devel \
    git clang-tools-extra

WORKDIR /build
VOLUME /build

CMD ["/bin/bash"]
```

Build and use:
```bash
# Build container
docker build -t xdp-router-build -f Dockerfile.build .

# Use container for building
docker run --rm -v $(pwd):/build xdp-router-build make

# Interactive development
docker run -it --rm -v $(pwd):/build xdp-router-build
```

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build xdp-router

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y clang llvm gcc make \
            libbpf-dev linux-tools-$(uname -r) linux-headers-$(uname -r) \
            libnl-3-dev libnl-route-3-dev libelf-dev

      - name: Verify dependencies
        run: make check-deps

      - name: Build
        run: make

      - name: Run BPF verifier
        run: make verify
```

---

## See Also

- [README.md](../README.md) - Project overview
- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture
- [Makefile](../Makefile) - Build system
- [libbpf documentation](https://libbpf.readthedocs.io/)
- [BPF and XDP Reference Guide](https://docs.cilium.io/en/stable/bpf/)
- [Kernel BPF documentation](https://www.kernel.org/doc/html/latest/bpf/)

---

**Document Version**: 1.0
**Last Updated**: 2026-02-20
**Maintained By**: Development Team
