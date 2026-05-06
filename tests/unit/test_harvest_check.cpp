// Unit tests for the harvest profitability guard.
//
// Tests are split into two areas:
//   1. decodeUint256() — ABI hex → uint64 decoding
//   2. computeHarvestDecision() — pure profitability arithmetic
//   3. Config defaults for the two new env vars
//
// No EthClient or network calls are involved; all functions under test are
// in harvest_math.h (header-only, pure arithmetic).

#include <catch2/catch_test_macros.hpp>
#include "harvest_math.h"
#include "config.h"

#include <cstdlib>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 1. decodeUint256
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("decodeUint256: zero value", "[harvest_math]") {
    // ABI-encoded uint256(0): 64 zero hex digits
    std::string zero(66, '0'); // "0x" + 64 '0'
    zero[0] = '0'; zero[1] = 'x';
    CHECK(decodeUint256(zero) == 0);
}

TEST_CASE("decodeUint256: known USDC-scale value (1000 USDC = 1e9 raw)", "[harvest_math]") {
    // 1 000 000 000 decimal = 0x3B9ACA00
    // ABI-padded to 64 hex chars
    std::string hex = "0x000000000000000000000000000000000000000000000000000000003b9aca00";
    CHECK(decodeUint256(hex) == 1'000'000'000ULL);
}

TEST_CASE("decodeUint256: max uint64 saturates correctly (low 64 bits)", "[harvest_math]") {
    // uint256 with low 64 bits all set = 0xFFFFFFFFFFFFFFFF
    std::string hex = "0x000000000000000000000000000000000000000000000000ffffffffffffffff";
    CHECK(decodeUint256(hex) == UINT64_MAX);
}

TEST_CASE("decodeUint256: strips 0x prefix correctly", "[harvest_math]") {
    CHECK(decodeUint256("0x0000000000000001") == 1ULL);
}

TEST_CASE("decodeUint256: handles hex without 0x prefix", "[harvest_math]") {
    CHECK(decodeUint256("0000000000000064") == 100ULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. computeHarvestDecision — guard disabled
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("computeHarvestDecision: guard disabled (ratio=0) always harvests when profit exists",
          "[harvest_math]") {
    // 1000 USDC profit, any gas, ratio=0
    auto r = computeHarvestDecision(
        /*total_assets=*/  2'000'000'000ULL,  // 2000 USDC (6 dec)
        /*total_debt=*/    1'000'000'000ULL,  // 1000 USDC
        /*gas_estimate=*/  200'000,
        /*base_fee_wei=*/  50'000'000'000ULL, // 50 gwei
        /*priority_wei=*/  2'000'000'000ULL,  // 2 gwei
        /*eth_usd_cents=*/ 300'000,           // $3000
        /*ratio_bps=*/     0);                // disabled

    CHECK(r.profitable == true);
    CHECK(r.reason == "guard disabled");
}

TEST_CASE("computeHarvestDecision: guard disabled but no profit → not profitable",
          "[harvest_math]") {
    auto r = computeHarvestDecision(
        /*total_assets=*/  1'000'000'000ULL,
        /*total_debt=*/    1'000'000'000ULL,  // equal → no profit
        /*gas_estimate=*/  200'000,
        /*base_fee_wei=*/  50'000'000'000ULL,
        /*priority_wei=*/  2'000'000'000ULL,
        /*eth_usd_cents=*/ 300'000,
        /*ratio_bps=*/     0);

    CHECK(r.profitable == false);
    REQUIRE(r.reason.find("no profit") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. computeHarvestDecision — profit vs gas ratio
// ─────────────────────────────────────────────────────────────────────────────

// Scenario: $10 profit, gas costs $0.30 (30 cents), limit is 5% = $0.50 (50 cents)
// → gas < threshold → should harvest
TEST_CASE("computeHarvestDecision: profitable when gas well under threshold",
          "[harvest_math]") {
    // $10 profit: 10_000_000 raw USDC units (6 dec) = 10 USDC
    //   profit_usd_cents = 10_000_000 * 100 / 1_000_000 = 1000 cents = $10.00
    // gas = 100_000 units @ 1 gwei (base) + 0 priority = 1e14 wei total
    //   gas_cost_usd_cents = 1e14 * 300_000 / 1e18 = 30 cents = $0.30
    // threshold = 1000 cents * 500 / 10000 = 50 cents = $0.50
    // 30 <= 50 → profitable ✓
    auto r = computeHarvestDecision(
        /*total_assets=*/  20'000'000ULL,    // 20 USDC
        /*total_debt=*/    10'000'000ULL,    // 10 USDC → $10 profit
        /*gas_estimate=*/  100'000,
        /*base_fee_wei=*/  1'000'000'000ULL, // 1 gwei
        /*priority_wei=*/  0,
        /*eth_usd_cents=*/ 300'000,          // $3000/ETH
        /*ratio_bps=*/     500);             // 5% → $0.50 threshold

    CHECK(r.profitable == true);
    CHECK(r.profit_usd_cents == 1000); // $10.00
    CHECK(r.gas_cost_usd_cents == 30); // $0.30
    CHECK(r.threshold_cents == 50);    // $0.50
}

// Scenario: $0.01 profit, gas costs $5 → NOT profitable even at ratio=10000 bps (100%)
TEST_CASE("computeHarvestDecision: not profitable when gas exceeds profit",
          "[harvest_math]") {
    // $0.01 profit: 10_000 raw (6 dec) → profit_usd_cents = 1 cent
    // gas = 500_000 units @ (50+2)gwei = 2.6e13 wei
    //   gas_cost_usd_cents = 2.6e13 * 300_000 / 1e18 ≈ 7800 cents = $78
    // threshold = 1 cent * 10000 / 10000 = 1 cent
    // 7800 > 1 → not profitable
    auto r = computeHarvestDecision(
        /*total_assets=*/  10'010'000ULL,
        /*total_debt=*/    10'000'000ULL,    // 0.01 USDC profit
        /*gas_estimate=*/  500'000,
        /*base_fee_wei=*/  50'000'000'000ULL,
        /*priority_wei=*/  2'000'000'000ULL,
        /*eth_usd_cents=*/ 300'000,
        /*ratio_bps=*/     10'000);           // 100% — even then, gas > profit

    CHECK(r.profitable == false);
    REQUIRE(r.reason.find("threshold") != std::string::npos);
}

// Scenario: exactly at the threshold boundary — should harvest (≤, not <)
TEST_CASE("computeHarvestDecision: harvests when gas equals threshold exactly",
          "[harvest_math]") {
    // profit_raw = 100_000_000 (100 USDC, 6 decimals)
    //   profit_usd_cents = 100_000_000 * 100 / 1_000_000 = 10_000 cents = $100
    // ratio = 100 bps = 1%  → threshold = 10_000 * 100 / 10_000 = 100 cents = $1
    //
    // Target: gas_cost_usd_cents == 100 cents with no truncation.
    //   gas_cost_wei * eth_usd_cents / 1e18 must divide evenly.
    //
    // Use 1_000_000 gas @ 1 gwei (1e9 wei) base, 0 priority, ETH = $1000:
    //   gas_cost_wei = 1_000_000 * 1e9 = 1e15 wei
    //   gas_cost_usd_cents = 1e15 * 100_000 / 1e18
    //                      = 1e20 / 1e18 = 100 cents  (exact, no remainder)
    // threshold = 100 cents
    // 100 <= 100 → profitable ✓
    auto r = computeHarvestDecision(
        /*total_assets=*/  200'000'000ULL,   // 200 USDC
        /*total_debt=*/    100'000'000ULL,   // 100 USDC profit
        /*gas_estimate=*/  1'000'000,
        /*base_fee_wei=*/  1'000'000'000ULL, // 1 gwei
        /*priority_wei=*/  0,
        /*eth_usd_cents=*/ 100'000,          // $1000/ETH
        /*ratio_bps=*/     100);             // 1% → $1 threshold

    CHECK(r.profitable == true);
    CHECK(r.profit_usd_cents   == 10'000); // $100.00
    CHECK(r.gas_cost_usd_cents == 100);    // $1.00 — exactly at threshold
    CHECK(r.threshold_cents    == 100);    // $1.00
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Config: new field defaults
// ─────────────────────────────────────────────────────────────────────────────

static const char* VALID_KEY =
    "0x0000000000000000000000000000000000000000000000000000000000000001";

static void setRequiredVars() {
    setenv("ETH_RPC_URL",        "https://example.com/rpc", 1);
    setenv("KEEPER_PRIVATE_KEY", VALID_KEY,                  1);
    setenv("STRATEGY_ADDRESSES", "0xAAA",                    1);
}

static void clearProfitabilityVars() {
    unsetenv("MIN_PROFIT_RATIO_BPS");
    unsetenv("ETH_USD_PRICE_CENTS");
}

TEST_CASE("Config: min_profit_ratio_bps defaults to 500", "[config][harvest_guard]") {
    setRequiredVars();
    clearProfitabilityVars();
    auto cfg = Config::fromEnv();
    CHECK(cfg.min_profit_ratio_bps == 500);
}

TEST_CASE("Config: eth_usd_price_cents defaults to 300000", "[config][harvest_guard]") {
    setRequiredVars();
    clearProfitabilityVars();
    auto cfg = Config::fromEnv();
    CHECK(cfg.eth_usd_price_cents == 300'000);
}

TEST_CASE("Config: min_profit_ratio_bps=0 disables guard", "[config][harvest_guard]") {
    setRequiredVars();
    setenv("MIN_PROFIT_RATIO_BPS", "0", 1);
    auto cfg = Config::fromEnv();
    CHECK(cfg.min_profit_ratio_bps == 0);
    clearProfitabilityVars();
}

TEST_CASE("Config: custom ETH_USD_PRICE_CENTS is parsed", "[config][harvest_guard]") {
    setRequiredVars();
    setenv("ETH_USD_PRICE_CENTS", "250000", 1);
    auto cfg = Config::fromEnv();
    CHECK(cfg.eth_usd_price_cents == 250'000);
    clearProfitabilityVars();
}

TEST_CASE("Config: custom MIN_PROFIT_RATIO_BPS is parsed", "[config][harvest_guard]") {
    setRequiredVars();
    setenv("MIN_PROFIT_RATIO_BPS", "1000", 1);
    auto cfg = Config::fromEnv();
    CHECK(cfg.min_profit_ratio_bps == 1000);
    clearProfitabilityVars();
}
