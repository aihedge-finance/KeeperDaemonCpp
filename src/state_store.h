#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class StrategyStatus { OK, SKIPPED, COOLDOWN, SUSPENDED };

struct StrategyState {
    std::string    address;
    StrategyStatus status              = StrategyStatus::OK;
    int64_t        last_harvest_ts     = 0;
    std::string    last_harvest_tx;
    uint32_t       consecutive_failures = 0;
    int64_t        next_retry_ts       = -1; // -1 = not scheduled
};

// Loads and saves per-strategy state to/from state.json.
// Writes are atomic (tmp file + rename).
class StateStore {
public:
    // Merge saved state with configured addresses:
    //   - New addresses start as OK.
    //   - Known addresses resume from saved state.
    std::vector<StrategyState> load(const std::string&              path,
                                    const std::vector<std::string>& configured_addresses);

    void save(const std::string&                   path,
              const std::vector<StrategyState>&    states);
};
