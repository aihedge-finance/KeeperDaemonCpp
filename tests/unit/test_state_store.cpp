// Unit tests for StateStore: load/save/merge logic.
// Tests use /tmp paths and clean up after themselves.

#include <catch2/catch_test_macros.hpp>
#include "state_store.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const std::string TMP = "/tmp/keeper_test_state_";

TEST_CASE("New addresses start with OK status when no file exists", "[state_store]") {
    StateStore store;
    auto states = store.load("/tmp/nonexistent_keeper_xyz_abc.json",
                             {"0xAAA", "0xBBB"});
    REQUIRE(states.size() == 2);
    CHECK(states[0].address == "0xAAA");
    CHECK(states[0].status == StrategyStatus::OK);
    CHECK(states[0].consecutive_failures == 0);
    CHECK(states[0].next_retry_ts == -1);
    CHECK(states[1].status == StrategyStatus::OK);
}

TEST_CASE("Save and reload preserves all fields", "[state_store]") {
    const std::string path = TMP + "save_reload.json";
    StateStore store;

    StrategyState s;
    s.address              = "0xdeadbeef";
    s.status               = StrategyStatus::COOLDOWN;
    s.last_harvest_ts      = 1234567890;
    s.last_harvest_tx      = "0xabc123";
    s.consecutive_failures = 2;
    s.next_retry_ts        = 9999999999LL;

    store.save(path, {s});
    auto loaded = store.load(path, {"0xdeadbeef"});

    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].status               == StrategyStatus::COOLDOWN);
    CHECK(loaded[0].last_harvest_ts      == 1234567890);
    CHECK(loaded[0].last_harvest_tx      == "0xabc123");
    CHECK(loaded[0].consecutive_failures == 2);
    CHECK(loaded[0].next_retry_ts        == 9999999999LL);

    fs::remove(path);
}

TEST_CASE("SUSPENDED status survives save/load round-trip", "[state_store]") {
    const std::string path = TMP + "suspended.json";
    StateStore store;

    StrategyState s;
    s.address = "0xfeed";
    s.status  = StrategyStatus::SUSPENDED;
    s.consecutive_failures = 3;
    store.save(path, {s});

    auto loaded = store.load(path, {"0xfeed"});
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].status == StrategyStatus::SUSPENDED);
    CHECK(loaded[0].consecutive_failures == 3);

    fs::remove(path);
}

TEST_CASE("Merge: saved address resumes, new address starts OK, unknown dropped", "[state_store]") {
    const std::string path = TMP + "merge.json";
    StateStore store;

    // Save two addresses
    StrategyState s1; s1.address = "0xSAVED"; s1.status = StrategyStatus::SUSPENDED;
    StrategyState s2; s2.address = "0xDROPPED"; s2.status = StrategyStatus::OK;
    store.save(path, {s1, s2});

    // Reload with 0xSAVED + 0xNEW (0xDROPPED is not in config any more)
    auto loaded = store.load(path, {"0xSAVED", "0xNEW"});

    REQUIRE(loaded.size() == 2);
    // 0xSAVED resumes as SUSPENDED
    CHECK(loaded[0].address == "0xSAVED");
    CHECK(loaded[0].status  == StrategyStatus::SUSPENDED);
    // 0xNEW starts fresh
    CHECK(loaded[1].address == "0xNEW");
    CHECK(loaded[1].status  == StrategyStatus::OK);
    CHECK(loaded[1].consecutive_failures == 0);

    fs::remove(path);
}

TEST_CASE("Atomic write: tmp file is removed after successful save", "[state_store]") {
    const std::string path = TMP + "atomic.json";
    StateStore store;
    store.save(path, {});
    CHECK(!fs::exists(path + ".tmp"));
    CHECK(fs::exists(path));
    fs::remove(path);
}
