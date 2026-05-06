#include "keeper.h"
#include "harvest_math.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

std::atomic<bool> g_shutdown{false};

// ── Logging ───────────────────────────────────────────────────────────────────

void Keeper::log(const std::string& level, const std::string& msg) {
    auto now  = std::chrono::system_clock::now();
    auto t    = std::chrono::system_clock::to_time_t(now);
    struct tm tm_info {};
    gmtime_r(&t, &tm_info);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    auto& out = (level == "ERROR") ? std::cerr : std::cout;
    out << "[" << buf << "] [" << level << "] " << msg << "\n";
    out.flush();
}

// ── Constructor ───────────────────────────────────────────────────────────────

Keeper::Keeper(const Config& cfg, EthClient& client, TxSigner& signer,
               StateStore& store, std::vector<StrategyState> initial_states)
    : cfg_(cfg), client_(client), signer_(signer),
      store_(store), states_(std::move(initial_states)) {}

// ── Main run loop ─────────────────────────────────────────────────────────────

void Keeper::run() {
    log("INFO", "Keeper started. Managing " + std::to_string(states_.size()) +
        " strategies. Interval: " + std::to_string(cfg_.harvest_interval_seconds) + "s.");

    while (!g_shutdown) {
        auto cycle_start = std::chrono::steady_clock::now();
        runHarvestCycle();

        auto next_cycle = cycle_start +
                          std::chrono::seconds(cfg_.harvest_interval_seconds);

        // Sleep until next cycle, waking every 100 ms to check for shutdown
        while (!g_shutdown && std::chrono::steady_clock::now() < next_cycle)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log("INFO", "Shutdown signal received — exiting cleanly.");
}

// ── Harvest cycle ─────────────────────────────────────────────────────────────

void Keeper::runHarvestCycle() {
    log("INFO", "=== Harvest cycle start ===");

    // Check gas price once for the entire cycle
    auto base_fee_opt = client_.getBaseFeePerGas();
    if (!base_fee_opt) {
        log("WARN", "Failed to fetch baseFeePerGas — skipping cycle.");
        return;
    }
    uint64_t base_fee_wei  = *base_fee_opt;
    uint64_t base_fee_gwei = base_fee_wei / 1'000'000'000ULL;

    if (base_fee_gwei > cfg_.max_gas_price_gwei) {
        log("INFO", "Gas price " + std::to_string(base_fee_gwei) +
            " gwei exceeds limit " + std::to_string(cfg_.max_gas_price_gwei) +
            " gwei — skipping cycle.");
        return;
    }

    log("INFO", "Base fee: " + std::to_string(base_fee_gwei) + " gwei — proceeding.");

    for (auto& state : states_) {
        if (g_shutdown) break;

        if (state.status == StrategyStatus::SUSPENDED) {
            log("WARN", "[SUSPENDED] " + state.address + " — skipping (restart to reset).");
            continue;
        }

        if (state.status == StrategyStatus::COOLDOWN) {
            int64_t now_ts = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            if (now_ts < state.next_retry_ts) {
                int64_t remaining = state.next_retry_ts - now_ts;
                log("INFO", "[COOLDOWN] " + state.address +
                    " — " + std::to_string(remaining) + "s remaining.");
                continue;
            }
            log("INFO", "[COOLDOWN] " + state.address + " — cooldown expired, retrying.");
        }

        // ── Pre-harvest profitability check ───────────────────────────────────────
        HarvestCheck chk = shouldHarvest(state.address, base_fee_wei);
        log("INFO", "[CHECK] " + state.address + " — " + chk.reason);

        if (!chk.should_harvest) {
            // Not ready yet — mark SKIPPED so the state file reflects it,
            // but do NOT apply retry policy (this is not a failure).
            state.status = StrategyStatus::SKIPPED;
            store_.save("state.json", states_);
            continue;
        }

        harvestStrategy(state, base_fee_wei);
    }

    log("INFO", "=== Harvest cycle end ===");
}

// ── Pre-harvest check ──────────────────────────────────────────────────────────────

// Yearn V3 TokenizedStrategy view selectors (standard, same on all chains):
//   totalAssets() → bytes4 keccak256("totalAssets()")      = 0x01e1d114
//   totalDebt()   → bytes4 keccak256("totalDebt()")        = 0x890ba44c
// Both return a single ABI-encoded uint256 (32 bytes, big-endian).
//
// The profitability arithmetic lives in harvest_math.h (pure, testable).
// This function is responsible only for the RPC calls + bridging to HarvestCheck.

HarvestCheck Keeper::shouldHarvest(const std::string& strategy_addr,
                                    uint64_t           base_fee_wei) {
    HarvestCheck chk;

    // ── 1. Read totalAssets() ────────────────────────────────────────────────────
    auto ta_hex = client_.ethCall(strategy_addr, "0x01e1d114");
    if (!ta_hex) {
        chk.reason = "eth_call totalAssets() failed — skipping";
        return chk;
    }
    chk.total_assets = decodeUint256(*ta_hex);

    // ── 2. Read totalDebt() ─────────────────────────────────────────────────────
    auto td_hex = client_.ethCall(strategy_addr, "0x890ba44c");
    if (!td_hex) {
        chk.reason = "eth_call totalDebt() failed — skipping";
        return chk;
    }
    chk.total_debt = decodeUint256(*td_hex);

    // ── 3. Estimate gas for report() ────────────────────────────────────────────
    //    (skip when guard disabled — no need to waste an RPC call)
    uint64_t gas_estimate = 0;
    if (cfg_.min_profit_ratio_bps > 0) {
        const std::string report_selector = "0x51cff8d9";
        auto gas_opt = client_.estimateGas(
            signer_.getAddress(), strategy_addr, report_selector);
        if (!gas_opt) {
            // estimateGas reverts when report() would revert (e.g. health-check
            // blocks harvest). Treat this as not-harvestable, not an error.
            chk.reason = "estimateGas(report()) reverted — strategy not harvestable";
            return chk;
        }
        gas_estimate      = *gas_opt;
        chk.gas_estimate  = gas_estimate;
        uint64_t prio_wei = cfg_.max_priority_fee_gwei * 1'000'000'000ULL;
        chk.gas_cost_wei  = gas_estimate * (base_fee_wei + prio_wei);
    }

    // ── 4. Delegate pure arithmetic to harvest_math.h ──────────────────────────
    uint64_t priority_fee_wei = cfg_.max_priority_fee_gwei * 1'000'000'000ULL;
    auto dec = computeHarvestDecision(
        chk.total_assets,
        chk.total_debt,
        gas_estimate,
        base_fee_wei,
        priority_fee_wei,
        cfg_.eth_usd_price_cents,
        cfg_.min_profit_ratio_bps);

    chk.profit_raw     = (chk.total_assets > chk.total_debt)
                         ? chk.total_assets - chk.total_debt : 0;
    chk.should_harvest = dec.profitable;
    chk.reason         = dec.reason
        + " | profit=$" + std::to_string(dec.profit_usd_cents / 100)
        + " gas=$"      + std::to_string(dec.gas_cost_usd_cents / 100)
        + " limit_bps=" + std::to_string(cfg_.min_profit_ratio_bps);
    return chk;
}

// ── Per-strategy harvest ──────────────────────────────────────────────────────

void Keeper::harvestStrategy(StrategyState& state, uint64_t base_fee_wei) {
    // report() function selector (no arguments)
    const std::vector<uint8_t> calldata = {0x51, 0xcf, 0xf8, 0xd9};
    const std::string          data_hex = "0x51cff8d9";

    log("INFO", "Harvesting " + state.address);

    // 1. Estimate gas (skip on revert — not a retry-eligible failure)
    auto gas_est = client_.estimateGas(signer_.getAddress(), state.address, data_hex);
    if (!gas_est) {
        // report() reverted — strategy is not harvestable right now (no profit, or
        // protocol-level condition not met). This is NOT a retryable failure; record
        // SKIPPED so the state file is always written and callers can observe it.
        log("WARN", "Gas estimate failed for " + state.address +
            " — not harvestable this cycle (report() reverted). Marking SKIPPED.");
        state.status = StrategyStatus::SKIPPED;
        store_.save("state.json", states_);
        return;
    }
    uint64_t gas_limit = (*gas_est * 12) / 10; // +20% buffer
    log("INFO", "Gas estimate: " + std::to_string(*gas_est) +
        " → limit: " + std::to_string(gas_limit));

    // 2. Fetch nonce
    auto nonce_opt = client_.getNonce(signer_.getAddress());
    if (!nonce_opt) {
        log("ERROR", "Failed to fetch nonce for " + state.address);
        applyRetryPolicy(state);
        store_.save("state.json", states_);
        return;
    }

    // 3. Compute EIP-1559 fee fields
    uint64_t priority_fee = cfg_.max_priority_fee_gwei * 1'000'000'000ULL;
    uint64_t max_fee      = base_fee_wei * 2 + priority_fee;

    // 4. Sign transaction
    std::string raw_tx;
    try {
        raw_tx = signer_.signTransaction(
            *nonce_opt, priority_fee, max_fee, gas_limit, state.address, calldata);
    } catch (const std::exception& e) {
        log("ERROR", "Signing failed for " + state.address + ": " + e.what());
        applyRetryPolicy(state);
        store_.save("state.json", states_);
        return;
    }

    // 5. Broadcast
    auto tx_hash_opt = client_.sendRawTransaction(raw_tx);
    if (!tx_hash_opt) {
        log("ERROR", "sendRawTransaction failed for " + state.address);
        applyRetryPolicy(state);
        store_.save("state.json", states_);
        return;
    }
    log("INFO", "Broadcast tx " + *tx_hash_opt + " for " + state.address);

    // 6. Wait for receipt (3-minute timeout)
    auto receipt = waitForReceipt(*tx_hash_opt, std::chrono::seconds(180));
    if (!receipt) {
        log("ERROR", "Receipt timeout for tx " + *tx_hash_opt +
            " strategy " + state.address);
        applyRetryPolicy(state);
        store_.save("state.json", states_);
        return;
    }

    if (!receipt->success) {
        log("ERROR", "Transaction REVERTED: tx=" + *tx_hash_opt +
            " strategy=" + state.address);
        applyRetryPolicy(state);
        store_.save("state.json", states_);
        return;
    }

    // 7. Success
    log("INFO", "SUCCESS: strategy=" + state.address +
        " tx=" + *tx_hash_opt +
        " gasUsed=" + std::to_string(receipt->gas_used));

    int64_t now_ts = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    state.status               = StrategyStatus::OK;
    state.last_harvest_ts      = now_ts;
    state.last_harvest_tx      = *tx_hash_opt;
    state.consecutive_failures = 0;
    state.next_retry_ts        = -1;
    store_.save("state.json", states_);
}

// ── Retry policy ──────────────────────────────────────────────────────────────

void Keeper::applyRetryPolicy(StrategyState& state) {
    state.consecutive_failures++;

    int64_t now_ts = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (cfg_.retry_policy == RetryPolicy::Exponential) {
        if (state.consecutive_failures >= cfg_.max_retry_attempts) {
            state.status = StrategyStatus::SUSPENDED;
            log("ERROR", "[SUSPENDED] " + state.address +
                " suspended after " + std::to_string(state.consecutive_failures) +
                " consecutive failures. Restart process to re-enable.");
            return;
        }
        // delay = base * 2^(failures-1), capped at max
        uint64_t delay = cfg_.base_retry_delay_seconds;
        for (uint32_t i = 1; i < state.consecutive_failures; ++i) {
            delay = std::min(delay * 2, cfg_.max_retry_delay_seconds);
        }
        state.status        = StrategyStatus::COOLDOWN;
        state.next_retry_ts = now_ts + static_cast<int64_t>(delay);
        log("WARN", "Strategy " + state.address + " → COOLDOWN for " +
            std::to_string(delay) + "s (failure #" +
            std::to_string(state.consecutive_failures) + ")");

    } else { // FixedCooldown
        if (state.consecutive_failures >= cfg_.max_cooldown_cycles) {
            state.status = StrategyStatus::SUSPENDED;
            log("ERROR", "[SUSPENDED] " + state.address +
                " suspended after " + std::to_string(state.consecutive_failures) +
                " cooldown cycles. Restart process to re-enable.");
            return;
        }
        state.status        = StrategyStatus::COOLDOWN;
        state.next_retry_ts = now_ts + static_cast<int64_t>(cfg_.retry_cooldown_seconds);
        log("WARN", "Strategy " + state.address + " → COOLDOWN for " +
            std::to_string(cfg_.retry_cooldown_seconds) + "s (cycle #" +
            std::to_string(state.consecutive_failures) + ")");
    }
}

// ── Receipt poller ────────────────────────────────────────────────────────────

std::optional<TxReceipt> Keeper::waitForReceipt(const std::string&   tx_hash,
                                                  std::chrono::seconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline && !g_shutdown) {
        auto receipt = client_.getTransactionReceipt(tx_hash);
        if (receipt) return receipt;

        // Sleep 10 s between polls, breaking early on shutdown
        auto wake = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!g_shutdown && std::chrono::steady_clock::now() < wake)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return std::nullopt;
}
