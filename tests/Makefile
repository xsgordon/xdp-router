# Tests Makefile

.PHONY: all test clean test-unit test-integration test-performance

all: test

test: test-unit
	@echo "All tests completed"

test-unit:
	@echo "Running unit tests..."
	@cd unit && $(MAKE) test

test-integration:
	@echo "Integration tests not yet implemented (Phase 3+)"
	@echo "See tests/integration/README.md"

test-performance:
	@echo "Performance tests not yet implemented (Phase 7)"
	@echo "See tests/performance/README.md"

clean:
	@cd unit && $(MAKE) clean
