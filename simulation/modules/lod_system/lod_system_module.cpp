#include "lod_system_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>
#include <cmath>

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
    return std::clamp(ratio, LOD2_MIN_MODIFIER, LOD2_MAX_MODIFIER);
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

    if (!monthly && !annual) return;

    // LOD 1 monthly update: process LOD 1 nations
    if (monthly) {
        for (const auto& nation : state.nations) {
            if (!nation.lod1_profile.has_value()) continue;
            // LOD 1 nation: regenerate trade offers
            // Full implementation deferred to orchestrator pass
        }
    }

    // LOD 2 annual batch: update global price index
    if (annual && state.lod2_price_index) {
        // Aggregate production/consumption across LOD 2 nations
        // and update price modifiers
        // Full implementation deferred to orchestrator pass
    }
}

}  // namespace econlife
