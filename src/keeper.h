#pragma once

#include "config.h"
#include "eth_client.h"
#include "state_store.h"
#include "tx_signer.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

// Atomic flag — set by signal handler to trigger graceful shutdown.
extern std::atomic<bool> g_shutdown;

// Result of the pre-harvest profitability check.
struct HarvestCheck {
    bool     should_harvest = false;
    uint64_t total_assets   = 0;  // raw on-chain value (asset decimals)
    uint64_t total_debt     = 0;  // raw on-chain value (asset decimals)
    uint64_t profit_raw     = 0;  // total_assets - total_debt (asset decimals)
    uint64_t gas_estimate   = 0;  // gas units for report()
    uint64_t gas_cost_wei   = 0;  // gas_estimate * (baseFee + priorityFee)
    std::string reason;           // human-readable verdict for logging
};

class Keeper {
public:
    Keeper(const Config&  cfg,
           EthClient&     client,
           TxSigner&      signer,
           StateStore&    store,
           std::vector<StrategyState> initial_states);

    // Blocking run loop. Returns when g_shutdown is set.
    void run();

private:
    const Config&              cfg_;
    EthClient&                 client_;
    TxSigner&                  signer_;
    StateStore&                store_;
    std::vector<StrategyState> states_;

    // ── Harvest cycle ────────────────────────────────────────────────────────
    void runHarvestCycle();

    // Harvest a single strategy; updates state and saves on completion.
    void harvestStrategy(StrategyState& state, uint64_t base_fee_wei);

    // ── Pre-harvest check ────────────────────────────────────────────────────
    // Reads totalAssets() and totalDebt() from the strategy via eth_call,
    // then compares estimated gas cost (in USD) against the expected profit.
    // Works generically for any Yearn V3 TokenizedStrategy.
    HarvestCheck shouldHarvest(const std::string& strategy_addr,
                               uint64_t           base_fee_wei);

    // ── Retry policy ─────────────────────────────────────────────────────────
    // Increments failure count, transitions to COOLDOWN or SUSPENDED.
    void applyRetryPolicy(StrategyState& state);

    // ── Receipt polling ──────────────────────────────────────────────────────
    // Polls every 10 s up to `timeout`. Returns nullopt on timeout.
    std::optional<TxReceipt> waitForReceipt(const std::string&    tx_hash,
                                             std::chrono::seconds  timeout);

    // ── Logging ──────────────────────────────────────────────────────────────
    static void log(const std::string& level, const std::string& msg);
};
