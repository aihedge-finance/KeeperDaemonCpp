#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class RetryPolicy { Exponential, FixedCooldown };

struct Config {
    // Required
    std::string rpc_url;
    std::string keeper_private_key; // 0x-prefixed 32-byte hex

    // Strategy addresses to harvest
    std::vector<std::string> strategy_addresses;

    // Timing
    uint64_t harvest_interval_seconds = 86400;

    // Gas guard
    uint64_t max_gas_price_gwei    = 30;
    uint64_t max_priority_fee_gwei = 2;

    // Retry
    RetryPolicy retry_policy = RetryPolicy::Exponential;

    // Option A — exponential backoff
    uint64_t base_retry_delay_seconds = 3600;
    uint64_t max_retry_delay_seconds  = 86400;
    uint32_t max_retry_attempts       = 3;

    // Option B — fixed cooldown
    uint64_t retry_cooldown_seconds = 21600;
    uint32_t max_cooldown_cycles    = 4;

    // Parses all env vars; throws std::runtime_error on missing required vars.
    static Config fromEnv();
};
