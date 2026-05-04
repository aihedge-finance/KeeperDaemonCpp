// Unit tests for Config::fromEnv().
// Each test sets the required env vars, calls Config::fromEnv(), then cleans up.

#include <catch2/catch_test_macros.hpp>
#include "config.h"

#include <cstdlib>
#include <string>

// Known 66-char valid key
static const char* VALID_KEY =
    "0x0000000000000000000000000000000000000000000000000000000000000001";

static void setRequired() {
    setenv("ETH_RPC_URL",          "https://example.com/rpc", 1);
    setenv("KEEPER_PRIVATE_KEY",   VALID_KEY,                  1);
    setenv("STRATEGY_ADDRESSES",   "0xAAA",                    1);
}

static void clearAll() {
    unsetenv("ETH_RPC_URL");
    unsetenv("KEEPER_PRIVATE_KEY");
    unsetenv("STRATEGY_ADDRESSES");
    unsetenv("HARVEST_INTERVAL_SECONDS");
    unsetenv("MAX_GAS_PRICE_GWEI");
    unsetenv("MAX_PRIORITY_FEE_GWEI");
    unsetenv("RETRY_POLICY");
    unsetenv("BASE_RETRY_DELAY_SECONDS");
    unsetenv("MAX_RETRY_DELAY_SECONDS");
    unsetenv("MAX_RETRY_ATTEMPTS");
    unsetenv("RETRY_COOLDOWN_SECONDS");
    unsetenv("MAX_COOLDOWN_CYCLES");
}

// ── Required vars ─────────────────────────────────────────────────────────────

TEST_CASE("Config throws when ETH_RPC_URL is missing", "[config]") {
    clearAll();
    setenv("KEEPER_PRIVATE_KEY", VALID_KEY, 1);
    setenv("STRATEGY_ADDRESSES", "0xAAA",   1);
    CHECK_THROWS(Config::fromEnv());
}

TEST_CASE("Config throws when KEEPER_PRIVATE_KEY is missing", "[config]") {
    clearAll();
    setenv("ETH_RPC_URL",        "https://example.com", 1);
    setenv("STRATEGY_ADDRESSES", "0xAAA",               1);
    CHECK_THROWS(Config::fromEnv());
}

TEST_CASE("Config throws when STRATEGY_ADDRESSES is missing", "[config]") {
    clearAll();
    setenv("ETH_RPC_URL",        "https://example.com", 1);
    setenv("KEEPER_PRIVATE_KEY", VALID_KEY,              1);
    CHECK_THROWS(Config::fromEnv());
}

TEST_CASE("Config throws on malformed KEEPER_PRIVATE_KEY", "[config]") {
    clearAll();
    setRequired();
    setenv("KEEPER_PRIVATE_KEY", "0xdeadbeef", 1); // too short
    CHECK_THROWS(Config::fromEnv());
}

// ── Defaults ──────────────────────────────────────────────────────────────────

TEST_CASE("Config applies correct defaults when optional vars absent", "[config]") {
    clearAll();
    setRequired();
    auto cfg = Config::fromEnv();
    CHECK(cfg.harvest_interval_seconds == 86400);
    CHECK(cfg.max_gas_price_gwei       == 30);
    CHECK(cfg.max_priority_fee_gwei    == 2);
    CHECK(cfg.retry_policy             == RetryPolicy::Exponential);
    CHECK(cfg.base_retry_delay_seconds == 3600);
    CHECK(cfg.max_retry_delay_seconds  == 86400);
    CHECK(cfg.max_retry_attempts       == 3);
    CHECK(cfg.retry_cooldown_seconds   == 21600);
    CHECK(cfg.max_cooldown_cycles      == 4);
    clearAll();
}

// ── Parsing ───────────────────────────────────────────────────────────────────

TEST_CASE("Config parses comma-separated strategy addresses with whitespace", "[config]") {
    clearAll();
    setRequired();
    setenv("STRATEGY_ADDRESSES", " 0xAAA , 0xBBB ,0xCCC ", 1);
    auto cfg = Config::fromEnv();
    REQUIRE(cfg.strategy_addresses.size() == 3);
    CHECK(cfg.strategy_addresses[0] == "0xAAA");
    CHECK(cfg.strategy_addresses[1] == "0xBBB");
    CHECK(cfg.strategy_addresses[2] == "0xCCC");
    clearAll();
}

TEST_CASE("Config selects fixed_cooldown retry policy via RETRY_POLICY env var", "[config]") {
    clearAll();
    setRequired();
    setenv("RETRY_POLICY", "fixed_cooldown", 1);
    auto cfg = Config::fromEnv();
    CHECK(cfg.retry_policy == RetryPolicy::FixedCooldown);
    clearAll();
}

TEST_CASE("Config throws on unknown RETRY_POLICY value", "[config]") {
    clearAll();
    setRequired();
    setenv("RETRY_POLICY", "invalid_value", 1);
    CHECK_THROWS(Config::fromEnv());
    clearAll();
}

TEST_CASE("Config reads HARVEST_INTERVAL_SECONDS correctly", "[config]") {
    clearAll();
    setRequired();
    setenv("HARVEST_INTERVAL_SECONDS", "3600", 1);
    auto cfg = Config::fromEnv();
    CHECK(cfg.harvest_interval_seconds == 3600);
    clearAll();
}
