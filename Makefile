# Curve Keeper Daemon — Makefile
# All targets are designed to run inside the Dev Container.

.PHONY: help deps build clean lint test-unit test-integration smoke

help: ## Show available targets
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / \
	  {printf "  %-22s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

deps: ## Install Conan dependencies into build/
	@if ! conan profile list | grep -q "default"; then \
		echo "Creating default Conan profile..."; \
		conan profile detect --force; \
	fi
	conan install . --output-folder=build --build=missing \
	    -s build_type=Release \
	    -s compiler.cppstd=20

build: deps ## Configure and compile curve_keeper + run_tests
	cmake -B build \
	    -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	    -G Ninja
	cmake --build build -j$$(nproc)
	@echo "✅ Build complete: build/curve_keeper  build/run_tests"

clean: ## Remove all build artifacts
	rm -rf build

lint: ## Run cppcheck static analysis on src/ and tests/
	cppcheck --enable=all --std=c++20 \
	    -I src/ \
	    --suppress=missingIncludeSystem \
	    --error-exitcode=1 \
	    src/ tests/unit/

# ── Tests ─────────────────────────────────────────────────────────────────────

test-unit: build ## Run Catch2 unit tests (keccak, signing, state_store, config)
	@echo "=== Unit Tests ==="
	./build/run_tests --reporter console --success

test-integration: build ## Run Anvil fork integration test (requires ETH_RPC_URL)
	@echo "=== Integration Test (Anvil fork) ==="
	@if [ -z "$${ETH_RPC_URL:-}" ]; then \
		echo "❌ ETH_RPC_URL is not set. Export it first:"; \
		echo "   export ETH_RPC_URL=https://rpc.ankr.com/eth/..."; \
		exit 1; \
	fi
	chmod +x tests/integration/run_keeper_fork.sh
	bash tests/integration/run_keeper_fork.sh

smoke: build ## Smoke test: MAX_GAS_PRICE_GWEI=0 forces gas-skip (no tx sent)
	@echo "Running smoke test (no tx will be sent)..."
	MAX_GAS_PRICE_GWEI=0 \
	HARVEST_INTERVAL_SECONDS=5 \
	STRATEGY_ADDRESSES=0x80cbe6dac50064d2d695be7f3a3f580776d69224 \
	KEEPER_PRIVATE_KEY=0x0000000000000000000000000000000000000000000000000000000000000001 \
	ETH_RPC_URL=$${ETH_RPC_URL:-https://rpc.ankr.com/eth} \
	./build/curve_keeper
