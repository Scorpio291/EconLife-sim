#include "lod_system_module.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "core/world_state/apply_deltas.h"  // markets_in_province
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
                for (uint32_t i : markets_in_province(state, prov_id)) {
                    const auto& market = state.regional_markets[i];

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

    // LOD 2 annual batch: emit per-good Lod2PriceIndexDelta entries with
    // raw_modifier = compute_lod2_price_modifier(consumption, production,
    // supply_floor). apply_lod2_price_deltas applies lerp smoothing using
    // cfg_.lod2_smoothing_rate and writes into
    // world.lod2_price_index->lod2_price_modifier[good_id].
    //
    // Per-good aggregation walks regional markets in LOD 2 statistical
    // provinces: supply (this-tick production) is the production signal;
    // demand_buffer (previous-tick demand carried over) is the consumption
    // signal. Goods absent from LOD 2 markets do not appear in the index.
    if (annual) {
        // Mark each LOD 2 statistical province for fast lookup. Sized to
        // match provinces vector; index == province_id.
        std::vector<uint8_t> is_lod2(state.provinces.size(), 0);
        for (size_t i = 0; i < state.provinces.size(); ++i) {
            if (state.provinces[i].lod_level == SimulationLOD::statistical) {
                is_lod2[i] = 1;
            }
        }

        // Per-good aggregation: ordered map keeps the emission order canonical
        // (good_id ascending) so delta ordering is deterministic regardless
        // of regional_markets iteration order.
        std::map<uint32_t, std::pair<float, float>> per_good;  // good_id -> (supply, demand)
        for (const auto& market : state.regional_markets) {
            if (market.province_id >= is_lod2.size() || !is_lod2[market.province_id])
                continue;
            auto& bucket = per_good[market.good_id];
            bucket.first += market.supply;
            bucket.second += market.demand_buffer;
        }

        for (const auto& [good_id, signals] : per_good) {
            const float production = signals.first;
            const float consumption = signals.second;
            if (production <= 0.0f && consumption <= 0.0f)
                continue;
            Lod2PriceIndexDelta lpd{};
            lpd.good_id = good_id;
            lpd.raw_modifier =
                compute_lod2_price_modifier(consumption, production, cfg_.supply_floor);
            delta.lod2_price_index_deltas.push_back(lpd);
        }
    }
}

}  // namespace econlife
