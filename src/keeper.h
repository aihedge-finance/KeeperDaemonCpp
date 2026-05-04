#pragma once

#include "config.h"
#include "eth_client.h"
#include "state_store.h"
#include "tx_signer.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <vector>

// Atomic flag — set by signal handler to trigger graceful shutdown.
extern std::atomic<bool> g_shutdown;

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
