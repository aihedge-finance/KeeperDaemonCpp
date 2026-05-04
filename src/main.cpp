#include "config.h"
#include "eth_client.h"
#include "keeper.h"
#include "state_store.h"
#include "tx_signer.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

// ── Signal handler ────────────────────────────────────────────────────────────

static void onSignal(int) {
    g_shutdown = true;
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    // Register graceful shutdown on SIGINT (Ctrl-C) and SIGTERM (Docker stop)
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    try {
        // 1. Load and validate configuration from environment variables
        Config cfg = Config::fromEnv();

        std::cout << "[INFO] curve_keeper starting\n"
                  << "[INFO]   RPC: " << cfg.rpc_url << "\n"
                  << "[INFO]   Strategies: " << cfg.strategy_addresses.size() << "\n"
                  << "[INFO]   Interval: " << cfg.harvest_interval_seconds << "s\n"
                  << "[INFO]   Max gas: " << cfg.max_gas_price_gwei << " gwei\n"
                  << "[INFO]   Retry policy: "
                  << (cfg.retry_policy == RetryPolicy::Exponential
                          ? "exponential"
                          : "fixed_cooldown")
                  << "\n";

        // 2. Initialise components
        EthClient  client(cfg.rpc_url);
        TxSigner   signer(cfg.keeper_private_key);

        std::cout << "[INFO]   Keeper address: " << signer.getAddress() << "\n";

        // 3. Load per-strategy state (resumes suspended/cooldown from disk)
        StateStore store;
        auto states = store.load("state.json", cfg.strategy_addresses);

        // 4. Run — blocks until SIGINT/SIGTERM
        Keeper keeper(cfg, client, signer, store, std::move(states));
        keeper.run();

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
