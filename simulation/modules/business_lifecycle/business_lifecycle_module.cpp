// business_lifecycle module — implementation.
// See business_lifecycle_module.h for class declarations and
// docs/interfaces/business_lifecycle/INTERFACE.md for the canonical specification.

#include "modules/business_lifecycle/business_lifecycle_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/economy/economy_types.h"

namespace econlife {

// ===========================================================================
// BusinessLifecycleModule — main tick execution
// ===========================================================================

void BusinessLifecycleModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Era detection: apply_deltas sets era_started_tick = current_tick when the
    // era transitions. We fire effects one tick later so the new era is visible
    // in WorldState regardless of orchestration step grouping.
    if (state.current_tick == 0 || state.technology.era_started_tick + 1 != state.current_tick) {
        return;
    }

    uint8_t new_era = static_cast<uint8_t>(state.technology.current_era);
    apply_stranded_asset_penalties(state, delta, new_era);
    spawn_era_entrants(state, delta, new_era);
}

// ===========================================================================
// Stranded-asset penalties
// ===========================================================================

void BusinessLifecycleModule::apply_stranded_asset_penalties(const WorldState& state,
                                                             DeltaBuffer& delta,
                                                             uint8_t new_era) const {
    auto it = cfg_.stranded_sectors.find(new_era);
    if (it == cfg_.stranded_sectors.end())
        return;

    // Process businesses in id-ascending order for deterministic delta accumulation.
    for (const auto& biz : state.npc_businesses) {
        for (const auto& entry : it->second) {
            if (biz.sector != entry.sector)
                continue;

            float new_rev = biz.revenue_per_tick * (1.0f - entry.revenue_penalty);
            // Clamp to floor so a single era shock cannot kill the business outright.
            float floor_rev = biz.revenue_per_tick * cfg_.stranded_revenue_floor;
            new_rev = std::max(new_rev, floor_rev);

            BusinessDelta bd{};
            bd.business_id = biz.id;
            bd.revenue_per_tick_update = new_rev;
            bd.cost_per_tick_update = biz.cost_per_tick * (1.0f + entry.cost_increase);
            delta.business_deltas.push_back(bd);
        }
    }
}

// ===========================================================================
// Era-entrant spawning
// ===========================================================================

void BusinessLifecycleModule::spawn_era_entrants(const WorldState& state, DeltaBuffer& delta,
                                                 uint8_t new_era) const {
    auto it = cfg_.emerging_sectors.find(new_era);
    if (it == cfg_.emerging_sectors.end())
        return;

    // Compute next unique business id (max existing + 1).
    uint32_t next_id = 1000u;
    for (const auto& b : state.npc_businesses) {
        if (b.id >= next_id)
            next_id = b.id + 1u;
    }

    // Fork RNG from world seed + era for full determinism.
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(new_era * 10000u);

    for (uint32_t pi = 0; pi < static_cast<uint32_t>(state.provinces.size()); ++pi) {
        // Count current businesses in this province.
        uint32_t province_biz_count = 0;
        for (const auto& b : state.npc_businesses) {
            if (b.province_id == pi)
                ++province_biz_count;
        }

        float province_wealth = 1.0f;
        if (pi < state.provinces.size()) {
            province_wealth = state.provinces[pi].demographics.income_high_fraction * 2.0f +
                              state.provinces[pi].demographics.income_middle_fraction;
        }

        for (const auto& entry : it->second) {
            uint32_t to_spawn = static_cast<uint32_t>(std::max(
                1.0f, std::round(static_cast<float>(province_biz_count) * entry.spawn_fraction)));

            for (uint32_t s = 0; s < to_spawn; ++s) {
                // Find the first province NPC that does not already own a business.
                uint32_t owner_id = 0;
                for (const auto& npc : state.significant_npcs) {
                    if (npc.home_province_id != pi)
                        continue;
                    bool already_owns = false;
                    for (const auto& b : state.npc_businesses) {
                        if (b.owner_id == npc.id) {
                            already_owns = true;
                            break;
                        }
                    }
                    if (!already_owns) {
                        owner_id = npc.id;
                        break;
                    }
                }
                // owner_id == 0 means no free NPC; financial_distribution will
                // log a warning and skip compensation, which is acceptable.

                NPCBusiness biz{};
                biz.id = next_id++;
                biz.owner_id = owner_id;
                biz.province_id = pi;
                biz.sector = entry.sector;
                biz.profile = entry.profile;
                biz.criminal_sector = false;

                // New entrants start lean relative to incumbents.
                biz.cash = (5000.0f + static_cast<float>(rng.next_uint(50000))) * province_wealth;
                biz.revenue_per_tick =
                    (50.0f + static_cast<float>(rng.next_uint(200))) * province_wealth;
                biz.cost_per_tick = biz.revenue_per_tick * (0.7f + rng.next_float() * 0.2f);
                biz.market_share = 0.01f + rng.next_float() * 0.05f;

                // Stagger quarterly decisions to avoid thundering-herd on
                // the first decision tick after spawn.
                biz.strategic_decision_tick = state.current_tick + rng.next_uint(90);
                biz.dispatch_day_offset = static_cast<uint8_t>(biz.id % 30);

                // Era-appropriate tech tier: new entrants launch with current-era
                // technology rather than Era 1 baseline.
                biz.actor_tech_state.effective_tech_tier = static_cast<float>(new_era);

                biz.default_activity_scope = VisibilityScope::institutional;

                NewBusinessDelta nbd{};
                nbd.new_business = std::move(biz);
                delta.new_businesses.push_back(std::move(nbd));
            }
        }
    }
}

}  // namespace econlife
