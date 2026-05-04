#include "state_store.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ── Serialization helpers ─────────────────────────────────────────────────────

static std::string statusToString(StrategyStatus s) {
    switch (s) {
        case StrategyStatus::OK:        return "OK";
        case StrategyStatus::COOLDOWN:  return "COOLDOWN";
        case StrategyStatus::SUSPENDED: return "SUSPENDED";
    }
    return "OK";
}

static StrategyStatus statusFromString(const std::string& s) {
    if (s == "COOLDOWN")  return StrategyStatus::COOLDOWN;
    if (s == "SUSPENDED") return StrategyStatus::SUSPENDED;
    return StrategyStatus::OK;
}

// ── StateStore::load ──────────────────────────────────────────────────────────

std::vector<StrategyState> StateStore::load(
        const std::string&              path,
        const std::vector<std::string>& configured_addresses) {

    // Build map from saved file (if it exists)
    std::unordered_map<std::string, StrategyState> saved;

    if (fs::exists(path)) {
        try {
            std::ifstream f(path);
            auto j = json::parse(f);
            for (auto& entry : j["strategies"]) {
                StrategyState s;
                s.address              = entry["address"].get<std::string>();
                s.status               = statusFromString(entry.value("status", "OK"));
                s.last_harvest_ts      = entry.value("last_harvest_ts", 0LL);
                s.last_harvest_tx      = entry.value("last_harvest_tx", "");
                s.consecutive_failures = entry.value("consecutive_failures", 0U);
                s.next_retry_ts        = entry.value("next_retry_ts", -1LL);
                saved[s.address]       = s;
            }
            std::cout << "[INFO] Loaded state from " << path << " ("
                      << saved.size() << " entries)\n";
        } catch (const std::exception& e) {
            std::cerr << "[WARN] Failed to parse state.json, starting fresh: "
                      << e.what() << "\n";
        }
    }

    // Merge: configured addresses take priority; unknown saved entries are dropped
    std::vector<StrategyState> states;
    states.reserve(configured_addresses.size());
    for (auto& addr : configured_addresses) {
        auto it = saved.find(addr);
        if (it != saved.end()) {
            states.push_back(it->second);
        } else {
            StrategyState s;
            s.address = addr;
            states.push_back(s);
        }
    }
    return states;
}

// ── StateStore::save ──────────────────────────────────────────────────────────

void StateStore::save(const std::string&                path,
                      const std::vector<StrategyState>& states) {
    json j;
    j["strategies"] = json::array();
    for (auto& s : states) {
        j["strategies"].push_back({
            {"address",              s.address},
            {"status",               statusToString(s.status)},
            {"last_harvest_ts",      s.last_harvest_ts},
            {"last_harvest_tx",      s.last_harvest_tx},
            {"consecutive_failures", s.consecutive_failures},
            {"next_retry_ts",        s.next_retry_ts}
        });
    }

    // Atomic write: write to .tmp then rename
    std::string tmp_path = path + ".tmp";
    {
        std::ofstream f(tmp_path);
        if (!f) throw std::runtime_error("Cannot write " + tmp_path);
        f << j.dump(2) << "\n";
    }
    fs::rename(tmp_path, path);
}
