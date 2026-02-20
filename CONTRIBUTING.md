# Contributing to xdp-router

Thank you for your interest in contributing to xdp-router!

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/yourusername/xdp-router.git`
3. Create a feature branch: `git checkout -b feature/your-feature-name`
4. Make your changes
5. Test your changes
6. Commit with a clear message
7. Push to your fork
8. Open a Pull Request

## Development Setup

### Prerequisites

See [README.md](README.md) for the full list of dependencies.

### Building

```bash
make clean
make
```

### Testing

```bash
# Run all tests
make test

# Run specific test suites
make test-unit
make test-integration
make test-performance
```

## Code Style

### C Code

- Follow Linux kernel coding style for BPF code
- Use clang-format for user-space code
- Run `make format` before committing

### Formatting

```bash
# Format all code
make format

# Lint code
make lint
```

## Commit Guidelines

### Commit Message Format

```
<type>: <subject>

<body>

<footer>
```

### Types

- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, etc.)
- `refactor`: Code refactoring
- `test`: Adding or updating tests
- `perf`: Performance improvements
- `chore`: Build system, dependencies, etc.

### Example

```
feat: add SRv6 End.DT4 behavior support

Implement the End.DT4 SRv6 function which performs IPv4 table lookup
after decapsulation. This adds:
- Parser for IPv4-in-SRv6
- Decapsulation logic
- FIB lookup for inner IPv4 packet

Closes #42
```

## Adding a New Protocol

To add support for a new protocol (e.g., MPLS):

1. **Create Parser** (`src/xdp/parsers/protocol.c`)
   - Implement `parse_protocol()` function
   - Update `parser_ctx` if needed

2. **Create Handler** (`src/xdp/handlers/protocol.c`)
   - Implement `handle_protocol()` function
   - Add to handler registry

3. **Add Feature Flag** (`src/common/common.h`)
   - Add `FEATURE_PROTOCOL` define
   - Update feature bitmap

4. **Control Plane** (if needed) (`src/control/protocol/`)
   - Add Netlink handling
   - Implement map updates

5. **Tests** (`tests/`)
   - Add unit tests
   - Add integration tests

6. **Documentation**
   - Update README.md
   - Add protocol-specific docs in `docs/`

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed architecture information.

## Testing Guidelines

### Unit Tests

- Test parsers with various packet formats
- Test handlers with edge cases
- Mock BPF map operations

### Integration Tests

- Test with real FRR integration
- Test multi-hop scenarios
- Test failover cases

### Performance Tests

- Benchmark throughput (MPPS)
- Measure latency
- Profile with `perf`

## Pull Request Process

1. **Ensure CI passes**: All tests must pass
2. **Update documentation**: README, ARCHITECTURE, etc.
3. **Add tests**: For new features
4. **Follow code style**: Run `make format`
5. **Describe changes**: Clear PR description
6. **Reference issues**: Link to related issues

### PR Checklist

- [ ] Code follows project style
- [ ] Tests added/updated
- [ ] Documentation updated
- [ ] CI passes
- [ ] No merge conflicts
- [ ] Signed commits (if required)

## Code Review

- Be respectful and constructive
- Address all review comments
- Update PR based on feedback
- Request re-review when ready

## Reporting Bugs

### Before Reporting

- Search existing issues
- Test on latest version
- Collect relevant information

### Bug Report Template

```markdown
**Description**
Clear description of the bug.

**To Reproduce**
Steps to reproduce:
1. Configure FRR with...
2. Load XDP program on...
3. Send traffic...
4. Observe error...

**Expected Behavior**
What should happen.

**Environment**
- OS: [e.g., Fedora 39]
- Kernel: [e.g., 6.5.0]
- NIC: [e.g., Intel X710]
- FRR version: [e.g., 9.0]

**Logs**
```
Paste relevant logs
```

**Additional Context**
Any other relevant information.
```

## Feature Requests

### Template

```markdown
**Problem**
What problem does this solve?

**Proposed Solution**
How would you solve it?

**Alternatives**
Other approaches considered?

**Additional Context**
Any other relevant information.
```

## Questions?

- Open an issue for questions
- Join discussions in existing issues
- Check [ARCHITECTURE.md](ARCHITECTURE.md) for design details

## License

By contributing, you agree that your contributions will be licensed under the same license as the project.

## Code of Conduct

- Be welcoming and inclusive
- Be respectful of differing viewpoints
- Accept constructive criticism
- Focus on what's best for the community
- Show empathy towards others
