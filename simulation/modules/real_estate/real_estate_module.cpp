// Real Estate Module — implementation.
// See real_estate_module.h for class declarations and
// docs/interfaces/real_estate/INTERFACE.md for the canonical specification.
//
// Per-tick processing per province:
//   1. Collect rental income for all rented properties (every tick).
//   2. On monthly ticks (current_tick % 30 == 0):
//      a. Recompute market_value from provincial conditions.
//      b. Derive rental_income_per_tick = market_value * rental_yield_rate.
//      c. Converge asking_price toward market_value.
//      d. Compute avg_property_value for province.
//   3. Assign commercial tenants to unoccupied commercial properties.

#include "modules/real_estate/real_estate_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"  // PlayerCharacter complete type
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// Property management
// ===========================================================================

void RealEstateModule::add_property(PropertyListing listing) {
    properties_.push_back(listing);
    // Maintain sorted order by id ascending for deterministic processing.
    std::sort(properties_.begin(), properties_.end(),
              [](const PropertyListing& a, const PropertyListing& b) { return a.id < b.id; });
}

const std::vector<PropertyListing>& RealEstateModule::properties() const {
    return properties_;
}

std::vector<PropertyListing>& RealEstateModule::properties() {
    return properties_;
}

// ===========================================================================
// Utilities
// ===========================================================================

float RealEstateModule::compute_market_value(const PropertyListing& prop,
                                             const Province& province) const {
    // Start with current market_value as baseline.
    float base_value = prop.market_value;
    if (base_value <= 0.0f) {
        base_value = 1.0f;  // prevent zero/negative base
    }

    // Criminal dominance penalty: reduces value by penalty_rate * dominance_index.
    // criminal_dominance_index is on RegionConditions.
    float dominance = province.cohort_stats->criminal_dominance_index;
    float dominance_penalty = dominance * cfg_.criminal_dominance_penalty;

    // Laundering premium: inflates value for launder-eligible properties.
    float launder_bonus = 0.0f;
    if (prop.launder_eligible) {
        launder_bonus = cfg_.laundering_premium;
    }

    // Apply modifiers: base_value * (1.0 - dominance_penalty + launder_bonus)
    // Clamp the multiplier to [0.1, 5.0] to prevent negative or extreme values.
    float multiplier = 1.0f - dominance_penalty + launder_bonus;
    multiplier = std::max(0.1f, std::min(5.0f, multiplier));

    return base_value * multiplier;
}

float RealEstateModule::compute_rental_income(float market_value, float rental_yield_rate) const {
    return market_value * rental_yield_rate;
}

void RealEstateModule::converge_asking_price(PropertyListing& prop, float rate) const {
    float gap = prop.market_value - prop.asking_price;
    prop.asking_price += gap * rate;

    // Ensure non-negative.
    if (prop.asking_price < 0.0f) {
        prop.asking_price = 0.0f;
    }
}

float RealEstateModule::compute_avg_property_value(const std::vector<PropertyListing>& props,
                                                   uint32_t province_id) const {
    float sum = 0.0f;
    uint32_t count = 0;

    // Process in id ascending order (vector is maintained sorted).
    for (const auto& prop : props) {
        if (prop.province_id == province_id) {
            sum += prop.market_value;
            ++count;
        }
    }

    if (count == 0) {
        return 0.0f;
    }

    return sum / static_cast<float>(count);
}

// ===========================================================================
// RealEstateModule — pre-tick initialization (main thread, before dispatch)
// ===========================================================================

void RealEstateModule::init_for_tick(const WorldState& /*state*/) {
    // Build per-province index so execute_province() only touches properties
    // belonging to its own province — no cross-province data races.
    province_property_indices_.clear();

    for (size_t i = 0; i < properties_.size(); ++i) {
        province_property_indices_[properties_[i].province_id].push_back(i);
    }
    // properties_ is already sorted by id ascending (maintained by add_property),
    // so the per-province index vectors inherit that order — deterministic.
}

// ===========================================================================
// RealEstateModule — per-province tick execution
// ===========================================================================

void RealEstateModule::execute_province(uint32_t province_idx, const WorldState& state,
                                        DeltaBuffer& province_delta) {
    // Build per-province index if not pre-built by init_for_tick()
    // (supports direct test calls without orchestrator).
    if (province_property_indices_.empty() && !properties_.empty()) {
        for (size_t i = 0; i < properties_.size(); ++i) {
            province_property_indices_[properties_[i].province_id].push_back(i);
        }
    }

    // Look up the pre-built index for this province. May be absent if no
    // properties exist in this province; Step 5 (homeless rate) still runs
    // against an empty `indices` vector so the rent_floor fallback path is
    // exercised whenever there are NPCs to sample.
    auto it = province_property_indices_.find(province_idx);
    static const std::vector<size_t> kEmptyIndices;
    const auto& indices = (it == province_property_indices_.end()) ? kEmptyIndices : it->second;

    const bool is_monthly_tick = (state.current_tick % cfg_.convergence_interval == 0);

    // Locate the province for market value computation.
    const Province* province = nullptr;
    if (province_idx < static_cast<uint32_t>(state.provinces.size())) {
        province = &state.provinces[province_idx];
    }

    // Determine player id (0 if no player).
    uint32_t player_id = 0;
    if (state.player != nullptr) {
        player_id = state.player->id;
    }

    // --- Step 1: Monthly market value recomputation and price convergence ---
    if (is_monthly_tick && province != nullptr) {
        for (size_t idx : indices) {
            auto& prop = properties_[idx];

            // Recompute market_value from provincial conditions.
            prop.market_value = compute_market_value(prop, *province);

            // Derive rental_income_per_tick from market_value (invariant).
            prop.rental_income_per_tick =
                compute_rental_income(prop.market_value, prop.rental_yield_rate);

            // Converge asking_price toward market_value.
            converge_asking_price(prop, cfg_.price_convergence_rate);
        }
    }

    // --- Step 2: Collect rental income for rented properties (every tick) ---
    for (size_t idx : indices) {
        const auto& prop = properties_[idx];

        if (!prop.rented) {
            continue;
        }

        float rental = compute_rental_income(prop.market_value, prop.rental_yield_rate);

        if (prop.owner_id == player_id && player_id != 0) {
            // Credit rental income to player.
            if (province_delta.player_delta.wealth_delta.has_value()) {
                province_delta.player_delta.wealth_delta.value() += rental;
            } else {
                province_delta.player_delta.wealth_delta = rental;
            }
        } else if (prop.owner_id != 0) {
            // Credit rental income to NPC owner.
            NPCDelta npc_delta{};
            npc_delta.npc_id = prop.owner_id;
            npc_delta.capital_delta = rental;
            province_delta.npc_deltas.push_back(npc_delta);
        }
    }

    // --- Step 3: Commercial tenant assignment ---
    // Find unoccupied commercial properties in this province and match them
    // to businesses that lack owned premises.
    for (size_t idx : indices) {
        auto& prop = properties_[idx];

        if (prop.type != PropertyType::commercial || prop.rented) {
            continue;
        }

        // Find a business in this province without owned commercial premises.
        for (const auto& biz : state.npc_businesses) {
            if (biz.province_id != province_idx) {
                continue;
            }

            // Check if this business already occupies a property.
            // Only search properties in the same province (our own indices).
            bool already_has_premises = false;
            for (size_t other_idx : indices) {
                const auto& other_prop = properties_[other_idx];
                if (other_prop.type == PropertyType::commercial && other_prop.rented &&
                    other_prop.tenant_id == biz.id) {
                    already_has_premises = true;
                    break;
                }
            }

            if (!already_has_premises) {
                prop.rented = true;
                prop.tenant_id = biz.id;
                break;  // one business per property
            }
        }
    }

    // --- Step 4: Monthly province avg_property_value update ---
    // Route through the province's region_id so apply_region_deltas matches
    // it correctly even if a future world generator decouples region_id
    // from province_idx (V1 worldgen assigns them 1:1).
    if (is_monthly_tick) {
        float avg_value = compute_avg_property_value(properties_, province_idx);
        RegionDelta region_delta{};
        region_delta.region_id = state.provinces[province_idx].region_id;
        region_delta.avg_property_value_update = avg_value;
        province_delta.region_deltas.push_back(region_delta);
    }

    // --- Step 5: Homeless rate sample (cohort_stats.homeless_rate) ---
    // An NPC is "housed" when their capital covers `homeless_rent_buffer_months`
    // of the province's mean residential rent. Provinces without residential
    // listings fall back to the configured rent_floor. Sample is converged
    // toward the stored rate to smooth per-tick sampling noise, matching the
    // pattern used by healthcare (sick_rate) and labor_market (unemployment).
    if (province_idx < state.npc_indices_by_province.size()) {
        float mean_residential_rent = 0.0f;
        uint32_t residential_count = 0;
        for (size_t i : indices) {
            const auto& prop = properties_[i];
            if (prop.type != PropertyType::residential)
                continue;
            mean_residential_rent += prop.rental_income_per_tick;
            ++residential_count;
        }
        if (residential_count > 0) {
            mean_residential_rent /= static_cast<float>(residential_count);
        } else {
            mean_residential_rent = cfg_.homeless_rent_floor;
        }
        const float affordability_threshold =
            mean_residential_rent * cfg_.homeless_rent_buffer_months;

        uint32_t active_count = 0;
        uint32_t unhoused_count = 0;
        for (uint32_t npc_idx : state.npc_indices_by_province[province_idx]) {
            if (npc_idx >= state.significant_npcs.size())
                continue;
            const NPC& npc = state.significant_npcs[npc_idx];
            if (npc.status != NPCStatus::active)
                continue;
            ++active_count;
            if (npc.capital < affordability_threshold) {
                ++unhoused_count;
            }
        }
        if (active_count > 0) {
            const float sample_homeless_fraction =
                static_cast<float>(unhoused_count) / static_cast<float>(active_count);
            const float current_homeless_rate =
                state.provinces[province_idx].cohort_stats
                    ? state.provinces[province_idx].cohort_stats->homeless_rate
                    : 0.0f;
            RegionDelta homeless_delta{};
            homeless_delta.region_id = state.provinces[province_idx].region_id;
            homeless_delta.homeless_rate_delta = cfg_.homeless_rate_convergence *
                                                 (sample_homeless_fraction - current_homeless_rate);
            province_delta.region_deltas.push_back(homeless_delta);
        }
    }
}

void RealEstateModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

}  // namespace econlife
