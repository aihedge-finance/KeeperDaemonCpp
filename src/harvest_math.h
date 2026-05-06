// harvest_math.h — Pure arithmetic for the harvest profitability guard.
//
// These helpers contain zero I/O and zero RPC calls, making them trivially
// unit-testable. keeper.cpp calls them after obtaining the raw on-chain values.

#pragma once

#include <cstdint>
#include <string>

// ── Uint256 ABI decoder ───────────────────────────────────────────────────────
// Decodes a 0x-prefixed 64-hex-char ABI-encoded uint256 returned by eth_call.
// Only the lower 64 bits are used — sufficient for USDC amounts up to ~1.8e13.
inline uint64_t decodeUint256(const std::string& hex) {
    std::string raw = (hex.size() > 2 && hex[0] == '0' && hex[1] == 'x')
                      ? hex.substr(2) : hex;
    if (raw.size() > 16) raw = raw.substr(raw.size() - 16); // keep low 64 bits
    return static_cast<uint64_t>(std::stoull(raw, nullptr, 16));
}

// ── Profitability check result ────────────────────────────────────────────────
struct ProfitabilityResult {
    bool     profitable         = false;
    uint64_t gas_cost_usd_cents = 0; // in USD cents
    uint64_t profit_usd_cents   = 0; // in USD cents (assumes 6-decimal stablecoin)
    uint64_t threshold_cents    = 0; // profit_usd_cents * ratio_bps / 10000
    std::string reason;
};

// ── computeHarvestDecision ────────────────────────────────────────────────────
// Pure arithmetic; no I/O. Returns whether the harvest is profitable given:
//   total_assets       — raw on-chain uint (6-decimal stablecoin, e.g. USDC)
//   total_debt         — raw on-chain uint (same units)
//   gas_estimate       — gas units for report()
//   base_fee_wei       — current block base fee in wei
//   priority_fee_wei   — max priority fee in wei
//   eth_usd_price_cents— ETH price in USD cents (e.g. 300000 = $3000)
//   min_profit_ratio_bps — max gas as % of profit in basis points
//                          (0 = guard disabled → always profitable)
inline ProfitabilityResult computeHarvestDecision(
    uint64_t total_assets,
    uint64_t total_debt,
    uint64_t gas_estimate,
    uint64_t base_fee_wei,
    uint64_t priority_fee_wei,
    uint64_t eth_usd_price_cents,
    uint64_t min_profit_ratio_bps)
{
    ProfitabilityResult r;

    // 1. Profit check
    if (total_assets <= total_debt) {
        r.reason = "no profit: totalAssets <= totalDebt";
        return r;
    }
    uint64_t profit_raw = total_assets - total_debt;

    // 2. Guard disabled?
    if (min_profit_ratio_bps == 0) {
        r.profitable = true;
        r.reason     = "guard disabled";
        return r;
    }

    // 3. Gas cost in wei → USD cents
    //    gas_cost_usd_cents = gas_cost_wei * eth_usd_price_cents / 1e18
    //    Use __uint128_t to safely handle the intermediate product.
    uint64_t    effective_fee_wei = base_fee_wei + priority_fee_wei;
    uint64_t    gas_cost_wei      = gas_estimate * effective_fee_wei;
    __uint128_t gas128 =
        static_cast<__uint128_t>(gas_cost_wei) * eth_usd_price_cents
        / 1'000'000'000'000'000'000ULL;

    // 4. Profit in USD cents
    //    Assumes 6-decimal stablecoin: 1 raw unit = 1e-6 USD = 1e-4 cents
    //    profit_usd_cents = profit_raw * 100 / 1e6 = profit_raw / 10000
    __uint128_t profit128 =
        static_cast<__uint128_t>(profit_raw) * 100ULL / 1'000'000ULL;

    // 5. Threshold: gas must not exceed ratio_bps% of profit
    __uint128_t threshold = profit128 * min_profit_ratio_bps / 10'000ULL;

    r.gas_cost_usd_cents = static_cast<uint64_t>(gas128);
    r.profit_usd_cents   = static_cast<uint64_t>(profit128);
    r.threshold_cents    = static_cast<uint64_t>(threshold);

    if (gas128 > threshold) {
        r.reason = "gas cost exceeds ratio threshold";
        return r;
    }

    r.profitable = true;
    r.reason     = "profitable";
    return r;
}
