#include "config.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string requireEnv(const char* name) {
    const char* val = std::getenv(name);
    if (!val || val[0] == '\0')
        throw std::runtime_error(std::string("Missing required env var: ") + name);
    return val;
}

static std::string optEnv(const char* name, const std::string& def) {
    const char* val = std::getenv(name);
    return (val && val[0] != '\0') ? val : def;
}

static uint64_t optEnvUint64(const char* name, uint64_t def) {
    const char* val = std::getenv(name);
    if (!val || val[0] == '\0') return def;
    return static_cast<uint64_t>(std::stoull(val));
}

static uint32_t optEnvUint32(const char* name, uint32_t def) {
    const char* val = std::getenv(name);
    if (!val || val[0] == '\0') return def;
    return static_cast<uint32_t>(std::stoul(val));
}

static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        // trim whitespace
        auto b = tok.find_first_not_of(" \t\r\n");
        auto e = tok.find_last_not_of(" \t\r\n");
        if (b != std::string::npos)
            out.push_back(tok.substr(b, e - b + 1));
    }
    return out;
}

// ── Config::fromEnv ──────────────────────────────────────────────────────────

Config Config::fromEnv() {
    Config cfg;

    cfg.rpc_url             = requireEnv("ETH_RPC_URL");
    cfg.keeper_private_key  = requireEnv("KEEPER_PRIVATE_KEY");

    auto addrs = requireEnv("STRATEGY_ADDRESSES");
    cfg.strategy_addresses  = splitComma(addrs);
    if (cfg.strategy_addresses.empty())
        throw std::runtime_error("STRATEGY_ADDRESSES must contain at least one address");

    cfg.harvest_interval_seconds = optEnvUint64("HARVEST_INTERVAL_SECONDS", 86400);
    cfg.max_gas_price_gwei       = optEnvUint64("MAX_GAS_PRICE_GWEI",       30);
    cfg.max_priority_fee_gwei    = optEnvUint64("MAX_PRIORITY_FEE_GWEI",    2);

    std::string policy = optEnv("RETRY_POLICY", "exponential");
    if (policy == "fixed_cooldown")
        cfg.retry_policy = RetryPolicy::FixedCooldown;
    else if (policy == "exponential")
        cfg.retry_policy = RetryPolicy::Exponential;
    else
        throw std::runtime_error("RETRY_POLICY must be 'exponential' or 'fixed_cooldown'");

    cfg.base_retry_delay_seconds = optEnvUint64("BASE_RETRY_DELAY_SECONDS", 3600);
    cfg.max_retry_delay_seconds  = optEnvUint64("MAX_RETRY_DELAY_SECONDS",  86400);
    cfg.max_retry_attempts       = optEnvUint32("MAX_RETRY_ATTEMPTS",       3);
    cfg.retry_cooldown_seconds   = optEnvUint64("RETRY_COOLDOWN_SECONDS",   21600);
    cfg.max_cooldown_cycles      = optEnvUint32("MAX_COOLDOWN_CYCLES",      4);

    // Validate private key format
    if (cfg.keeper_private_key.size() != 66 ||
        cfg.keeper_private_key.substr(0, 2) != "0x")
        throw std::runtime_error("KEEPER_PRIVATE_KEY must be 0x-prefixed 32-byte hex (66 chars)");

    return cfg;
}
