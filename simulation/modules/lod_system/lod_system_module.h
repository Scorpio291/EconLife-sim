#pragma once

#include <algorithm>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "lod_system_types.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class LodSystemModule : public ITickModule {
   public:
    explicit LodSystemModule(const LodSystemConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "lod_system"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"regional_conditions"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utilities for testing ---
    static float compute_lod1_production(float base_production, float tech_modifier,
                                         float climate_penalty, float trade_openness);
    static float compute_lod1_consumption(float base_consumption, float population_modifier,
                                          float era_modifier);
    static float compute_lod2_price_modifier(float consumption, float production,
                                             float supply_floor);
    static float compute_smoothed_modifier(float old_modifier, float raw_modifier,
                                           float smoothing_rate);
    static bool is_monthly_tick(uint32_t current_tick);
    static bool is_annual_tick(uint32_t current_tick);

    // Time calibration constants
    static constexpr uint32_t TICKS_PER_MONTH = 30;
    static constexpr uint32_t TICKS_PER_YEAR = 365;

   private:
    LodSystemConfig cfg_;
};

}  // namespace econlife
