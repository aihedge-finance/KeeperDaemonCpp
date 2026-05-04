#!/usr/bin/env bash
# Integration test: fork Ethereum mainnet with Anvil, run one keeper harvest
# cycle, and verify that state.json is written with status=OK.
#
# Requirements (installed in devcontainer):
#   - anvil (Foundry)
#   - cast  (Foundry)
#   - ./build/curve_keeper (built with `make build`)
#   - ETH_RPC_URL set in environment

set -euo pipefail

ANVIL_PORT=18545
TEST_RPC="http://localhost:${ANVIL_PORT}"
STATE_FILE="/tmp/keeper_integration_state.json"

# Anvil's well-known pre-funded account #0 (10,000 ETH, publicly known test key).
# Safe to hardcode — never used on mainnet.
ANVIL_KEEPER_KEY="0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80"

STRATEGY="0x80cbe6dac50064d2d695be7f3a3f580776d69224"

# ── Cleanup trap ──────────────────────────────────────────────────────────────
cleanup() {
    echo ""
    echo "Stopping Anvil (PID ${ANVIL_PID:-unknown})..."
    kill "${ANVIL_PID}" 2>/dev/null || true
    rm -f "${STATE_FILE}" "${STATE_FILE}.tmp"
}
trap cleanup EXIT

# ── Start Anvil fork ──────────────────────────────────────────────────────────
echo "=== Starting Anvil fork from ${ETH_RPC_URL} on port ${ANVIL_PORT} ==="
anvil \
    --fork-url "${ETH_RPC_URL}" \
    --port "${ANVIL_PORT}" \
    --silent \
    &
ANVIL_PID=$!

echo "Waiting for Anvil to be ready..."
for i in $(seq 1 15); do
    if cast block-number --rpc-url "${TEST_RPC}" &>/dev/null; then
        echo "Anvil ready at block $(cast block-number --rpc-url "${TEST_RPC}")"
        break
    fi
    sleep 1
done

# ── Run keeper for one cycle ──────────────────────────────────────────────────
echo ""
echo "=== Running keeper (1 cycle, 30s timeout) ==="
echo "Strategy:   ${STRATEGY}"
echo "Keeper key: ${ANVIL_KEEPER_KEY:0:12}... (Anvil account #0)"

rm -f "${STATE_FILE}"

# HARVEST_INTERVAL_SECONDS=999999 means it won't start a second cycle.
# timeout 30 gives the keeper enough time to complete the tx + receipt wait.
KEEPER_EXIT=0
timeout 30 env \
    ETH_RPC_URL="${TEST_RPC}" \
    KEEPER_PRIVATE_KEY="${ANVIL_KEEPER_KEY}" \
    STRATEGY_ADDRESSES="${STRATEGY}" \
    HARVEST_INTERVAL_SECONDS=999999 \
    MAX_GAS_PRICE_GWEI=9999 \
    MAX_PRIORITY_FEE_GWEI=2 \
    RETRY_POLICY=exponential \
    ./build/curve_keeper 2>&1 || KEEPER_EXIT=$?

echo ""
echo "=== Keeper exited with code ${KEEPER_EXIT} (124 = timeout, expected) ==="

# ── Verify state.json ─────────────────────────────────────────────────────────
echo ""
echo "=== Checking state.json ==="

if [ ! -f state.json ]; then
    echo "❌ FAIL: state.json was not created"
    exit 1
fi

cp state.json "${STATE_FILE}"
cat "${STATE_FILE}"
echo ""

STATUS=$(python3 -c "
import json, sys
data = json.load(open('${STATE_FILE}'))
strategies = data.get('strategies', [])
if not strategies:
    print('NO_ENTRIES')
    sys.exit(0)
for s in strategies:
    if s['address'].lower() == '${STRATEGY}'.lower():
        print(s['status'])
        sys.exit(0)
print('NOT_FOUND')
")

echo "Strategy status: ${STATUS}"

if [ "${STATUS}" = "OK" ]; then
    echo "✅ PASS: Strategy harvested successfully, status=OK"
    exit 0
else
    echo "❌ FAIL: Expected status=OK, got status=${STATUS}"
    exit 1
fi
