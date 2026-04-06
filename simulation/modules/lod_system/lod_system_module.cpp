#include "lod_system_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

float LodSystemModule::compute_lod1_production(float base_production, float tech_modifier,
                                               float climate_penalty, float trade_openness) {
    return base_production * tech_modifier * (1.0f - climate_penalty) * trade_openness;
}

float LodSystemModule::compute_lod1_consumption(float base_consumption, float population_modifier,
                                                float era_modifier) {
    return base_consumption * population_modifier * era_modifier;
}

float LodSystemModule::compute_lod2_price_modifier(float consumption, float production,
                                                   float supply_floor) {
    float denom = std::max(production, supply_floor);
    float ratio = consumption / denom;
    constexpr LodSystemConfig kDefaults{};
    return std::clamp(ratio, kDefaults.lod2_min_modifier, kDefaults.lod2_max_modifier);
}

float LodSystemModule::compute_smoothed_modifier(float old_modifier, float raw_modifier,
                                                 float smoothing_rate) {
    // Linear interpolation: lerp(old, raw, rate)
    return old_modifier + smoothing_rate * (raw_modifier - old_modifier);
}

bool LodSystemModule::is_monthly_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_MONTH) == 0;
}

bool LodSystemModule::is_annual_tick(uint32_t current_tick) {
    return (current_tick % TICKS_PER_YEAR) == 0;
}

void LodSystemModule::execute(const WorldState& state, DeltaBuffer& delta) {
    bool monthly = is_monthly_tick(state.current_tick);
    bool annual = is_annual_tick(state.current_tick);

    if (!monthly && !annual)
        return;

    // LOD 1 monthly update: compute simplified production/consumption for LOD 1 nations
    // and write MarketDelta supply adjustments for their trade offers.
    if (monthly) {
        for (const auto& nation : state.nations) {
            if (!nation.lod1_profile.has_value())
                continue;

            const auto& profile = nation.lod1_profile.value();

            // Iterate provinces of this LOD 1 nation. Each LOD 1 province gets a
            // supply-side market delta based on simplified production estimate.
            for (uint32_t prov_id : nation.province_ids) {
                if (prov_id >= state.provinces.size())
                    continue;
                const auto& province = state.provinces[prov_id];
                if (province.lod_level == SimulationLOD::full)
                    continue;  // skip LOD 0 provinces

                // Estimate LOD 1 production for this province using the utility function.
                // climate_penalty derived from current climate stress.
                float climate_penalty =
                    std::clamp(province.climate.climate_stress_current, 0.0f, 1.0f);
                float lod1_production = compute_lod1_production(
                    province.agricultural_productivity, profile.tech_tier_modifier, climate_penalty,
                    province.trade_openness);

                // Estimate LOD 1 consumption using population modifier from profile.
                float lod1_consumption =
                    compute_lod1_consumption(1.0f,  // base consumption unit
                                             profile.population_modifier,
                                             profile.tech_tier_modifier);  // era_modifier proxy

                // Write a supply delta for each regional market in this province.
                // V1 simplified: apply net production surplus/deficit across all markets.
                for (const auto& market : state.regional_markets) {
                    if (market.province_id != prov_id)
                        continue;

                    float net_surplus = lod1_production - lod1_consumption;
                    if (net_surplus == 0.0f)
                        continue;

                    MarketDelta md;
                    md.good_id = market.good_id;
                    md.region_id = prov_id;
                    md.supply_delta = net_surplus;
                    delta.market_deltas.push_back(md);
                }
            }
        }
    }

    // LOD 2 annual batch: update smoothed price modifier in the global price index
    // using aggregated production/consumption across LOD 2 nations.
    // lod2_price_index pointer may be null when no LOD 2 nations exist.
    if (annual && state.lod2_price_index) {
        // Aggregate total LOD 2 production and consumption signals.
        // V1 simplified: walk regional markets for statistical provinces and sum supply/demand.
        float total_production = 0.0f;
        float total_consumption = 0.0f;

        for (const auto& nation : state.nations) {
            if (!nation.lod1_profile.has_value())
                continue;

            const auto& profile = nation.lod1_profile.value();

            for (uint32_t prov_id : nation.province_ids) {
                if (prov_id >= state.provinces.size())
                    continue;
                const auto& province = state.provinces[prov_id];
                if (province.lod_level != SimulationLOD::statistical)
                    continue;

                float climate_penalty =
                    std::clamp(province.climate.climate_stress_current, 0.0f, 1.0f);

                total_production += compute_lod1_production(
                    province.agricultural_productivity, profile.tech_tier_modifier, climate_penalty,
                    province.trade_openness);

                total_consumption += compute_lod1_consumption(1.0f, profile.population_modifier,
                                                              profile.tech_tier_modifier);
            }
        }

        if (total_production > 0.0f || total_consumption > 0.0f) {
            float raw_modifier =
                compute_lod2_price_modifier(total_consumption, total_production, cfg_.supply_floor);

            // lod2_price_index carries a smoothed_modifier field; apply lerp smoothing.
            // Access pattern: lod2_price_index is a unique_ptr in WorldState.
            // The LOD system is the only module that writes the global price index.
            // Using LOD2_SMOOTHING_RATE constant from the module header.
            (void)raw_modifier;  // applied by engine via DeltaBuffer in full implementation
            // Full price-index write is EX scope (requires GlobalCommodityPriceIndex delta type).
            // The computations above are performed and values are available for that delta.
        }
    }
}

}  // namespace econlife
