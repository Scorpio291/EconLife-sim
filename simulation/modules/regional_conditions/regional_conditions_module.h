#pragma once

#include "core/tick/tick_module.h"
#include "regional_conditions_types.h"
#include <vector>
#include <algorithm>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class RegionalConditionsModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "regional_conditions"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"political_cycle", "influence_network"}; }
    bool is_province_parallel() const noexcept override { return true; }
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static float compute_stability_recovery(float current_stability, uint32_t instability_events);
    static float compute_criminal_dominance(float criminal_revenue, float total_revenue);
    static float compute_drought_recovery(float current_modifier, float recovery_rate);
    static float compute_inequality_from_gini(float gini_coefficient);

    // Constants
    static constexpr float STABILITY_RECOVERY_RATE    = 0.001f;
    static constexpr float EVENT_STABILITY_IMPACT     = 0.05f;
    static constexpr float INFRASTRUCTURE_DECAY_RATE  = 0.0002f;
    static constexpr float DROUGHT_RECOVERY_RATE      = 0.005f;
    static constexpr float FLOOD_RECOVERY_RATE        = 0.01f;
};

}  // namespace econlife
