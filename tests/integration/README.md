# Integration Tests

## Overview

Integration tests verify the complete XDP router BPF program using the
BPF skeleton. These tests load the actual BPF program into the kernel
and execute it with test packets.

## Requirements

- Linux kernel >=5.10
- Root access or CAP_BPF capability
- libbpf development headers
- bpftool

## Architecture

Integration tests use the BPF skeleton (`build/xdp_router.skel.h`) to:

1. Load the BPF program into the kernel
2. Configure maps (stats, config)
3. Execute test packets via `bpf_prog_test_run()`
4. Verify program behavior (XDP actions, stats, drops)

## Test Categories

### Handler Tests (Planned - 15+ tests)

**IPv4 Handler Tests:**
- [ ] `test_ipv4_ttl_expired()` - Verify TTL=0/1 returns XDP_PASS
- [ ] `test_ipv4_fib_success()` - Successful FIB lookup and forwarding
- [ ] `test_ipv4_fib_blackhole()` - Blackhole route returns XDP_DROP
- [ ] `test_ipv4_fib_no_route()` - No route returns XDP_DROP
- [ ] `test_ipv4_fragment_pass()` - Fragmented packets passed to kernel
- [ ] `test_ipv4_stats_update()` - Verify RX/TX stats incremented
- [ ] `test_ipv4_checksum_update()` - Verify checksum updated after TTL decrement
- [ ] `test_ipv4_mac_rewrite()` - Verify L2 address rewrite

**IPv6 Handler Tests:**
- [ ] `test_ipv6_hop_limit_expired()` - Verify hop_limit=0/1 returns XDP_PASS
- [ ] `test_ipv6_fib_success()` - Successful FIB lookup and forwarding
- [ ] `test_ipv6_fib_blackhole()` - Blackhole route returns XDP_DROP
- [ ] `test_ipv6_extension_headers()` - Extension headers passed to kernel
- [ ] `test_ipv6_icmpv6_pass()` - ICMPv6 passed to kernel (NDP, RA)
- [ ] `test_ipv6_stats_update()` - Verify RX/TX stats incremented
- [ ] `test_ipv6_mac_rewrite()` - Verify L2 address rewrite

**Statistics Tests:**
- [ ] `test_stats_saturation()` - Verify counters saturate at UINT64_MAX
- [ ] `test_stats_percpu()` - Verify PERCPU map behavior
- [ ] `test_drop_reason_tracking()` - Verify drop reasons recorded

**Configuration Tests:**
- [ ] `test_config_feature_toggle()` - Enable/disable features at runtime
- [ ] `test_config_defaults()` - Verify default configuration

## Implementation Status

**Current Status:** Framework planned, not yet implemented

**Reason:** Integration tests require:
- Root/CAP_BPF privileges
- Complex test infrastructure
- FIB lookup mocking or test routing tables
- Kernel BPF program testing support

**Alternative Testing:**
- ✅ Parser unit tests (25 tests) - Comprehensive coverage
- ✅ Security regression tests (12 tests) - All security issues covered
- ✅ Smoke tests - Basic end-to-end validation (existing)

## Future Work

### Phase 1: Test Infrastructure (Week 1-2)

1. **BPF Test Runner:**
   ```c
   // tests/integration/bpf_test_runner.h
   struct bpf_test_ctx {
       struct xdp_router_bpf *skel;
       int prog_fd;
       int stats_fd;
       int drops_fd;
       int config_fd;
   };

   int setup_bpf_test(struct bpf_test_ctx *ctx);
   int run_xdp_test(struct bpf_test_ctx *ctx, void *data, size_t len);
   void teardown_bpf_test(struct bpf_test_ctx *ctx);
   ```

2. **Test Packet Builder:**
   - Reuse existing `tests/common/packet_builder.h`
   - Add XDP context creation helpers

3. **FIB Lookup Mocking:**
   - Option 1: Create test routing table in kernel
   - Option 2: Mock FIB responses (requires kernel support)
   - Option 3: Use network namespaces for isolation

### Phase 2: Core Handler Tests (Week 3)

Implement 15+ handler tests covering:
- All FIB lookup return codes
- TTL/hop limit edge cases
- Statistics accuracy
- Drop reason tracking
- MAC address rewriting

### Phase 3: Advanced Tests (Week 4)

- Stress tests (high packet rate)
- Concurrent access tests (multiple CPUs)
- Configuration change tests (runtime toggles)
- Error injection tests

## Running Tests

```bash
# Once implemented:
sudo make test-integration

# Or individually:
sudo ./tests/integration/test_handlers
```

## References

- BPF skeleton documentation: https://www.kernel.org/doc/html/latest/bpf/
- bpf_prog_test_run(): https://man7.org/linux/man-pages/man2/bpf.2.html
- libbpf testing examples: https://github.com/libbpf/libbpf

## Notes

- Integration tests require privileged access
- Tests should clean up routing tables/namespaces
- Use test namespaces to avoid affecting host networking
- Consider CI/CD integration with appropriate permissions
