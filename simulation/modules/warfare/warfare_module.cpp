#include "modules/warfare/warfare_module.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/geography.h"
#include "core/world_state/world_state.h"

namespace econlife {

namespace {
constexpr uint32_t kTicksPerYear = 365;
}

float WarfareModule::military_power(uint64_t population, float surplus_ratio,
                                   const WarfareConfig& cfg) {
    const float fed =
        cfg.power_surplus_floor + (1.0f - cfg.power_surplus_floor) * std::clamp(surplus_ratio, 0.0f, 2.0f);
    return static_cast<float>(population) * std::max(0.0f, fed);
}

bool WarfareModule::regime_active(std::string_view regime) const {
    for (const auto& r : cfg_.active_regimes) {
        if (r == regime)
            return true;
    }
    return false;
}

void WarfareModule::execute(const WorldState& state, DeltaBuffer& delta) {
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    const uint32_t n = static_cast<uint32_t>(state.provinces.size());
    if (n == 0)
        return;

    // Per-polity military power, and an h3 -> index map for resolving neighbours.
    std::vector<float> power(n, 0.0f);
    std::unordered_map<H3Index, uint32_t> h3_to_idx;
    h3_to_idx.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        h3_to_idx[state.provinces[i].h3_index] = i;
        if (state.provinces[i].cohort_stats) {
            power[i] = military_power(state.provinces[i].cohort_stats->total_population,
                                     state.provinces[i].cohort_stats->subsistence_surplus_ratio, cfg_);
        }
    }

    // Seed by YEAR so a war is consistent across the year at any tick resolution.
    const uint32_t year = state.current_tick / kTicksPerYear;

    // war_mortality accumulates per province from each conflict it is drawn into.
    std::vector<float> war_mortality(n, 1.0f);

    // Each polity considers attacking each REACHABLE neighbour (adjacency = ox-cart
    // reach). It attacks a weaker neighbour with some probability — the EV decision:
    // strike where you are strong enough to win. Directional and deterministic.
    for (uint32_t a = 0; a < n; ++a) {
        if (power[a] <= 0.0f)
            continue;
        for (const auto& link : state.provinces[a].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            const uint32_t b = it->second;
            if (power[b] <= 0.0f)
                continue;
            if (power[a] < cfg_.aggression_ratio * power[b])
                continue;  // not strong enough to make the attack worthwhile
            DeterministicRNG rng(state.world_seed ^
                                 (static_cast<uint64_t>(year) * 0x9E3779B97F4A7C15ull) ^
                                 (static_cast<uint64_t>(a) << 21) ^
                                 (static_cast<uint64_t>(b) << 41) ^ 0x4A1207ull);
            if (rng.next_float() >= cfg_.base_aggression_prob)
                continue;  // no war this year
            // War: a strikes b. Both bleed; the defender worse (war is worse to lose).
            war_mortality[a] = std::min(cfg_.war_mortality_cap, war_mortality[a] + cfg_.attacker_loss);
            war_mortality[b] = std::min(cfg_.war_mortality_cap, war_mortality[b] + cfg_.defender_loss);
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
        RegionDelta rd{};
        rd.region_id = state.provinces[i].region_id;
        rd.war_mortality_replacement = war_mortality[i];
        delta.region_deltas.push_back(rd);
    }
}

}  // namespace econlife
