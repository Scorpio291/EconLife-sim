#pragma once

// Seasonal Agriculture Module -- province-parallel tick module that manages
// crop growth cycles (fallow -> planting -> growing -> harvest), applies
// climate and soil effects to yields, and handles continuous-output facilities
// (perennial trees, livestock, timber) with seasonal yield multipliers.
//
// See docs/interfaces/seasonal_agriculture/INTERFACE.md for canonical spec.

#include "core/tick/tick_module.h"
#include "modules/seasonal_agriculture/agriculture_types.h"
#include "modules/production/production_types.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct Province;

// ---------------------------------------------------------------------------
// Configuration constants for seasonal agriculture (TDD S44)
// ---------------------------------------------------------------------------
struct SeasonalAgricultureConstants {
    static constexpr uint32_t TICKS_PER_YEAR = 365;

    static constexpr uint32_t planting_duration_ticks = 7;
    static constexpr uint32_t harvest_duration_ticks = 14;

    static constexpr float fallow_soil_recovery_rate = 0.003f;
    static constexpr float soil_health_max = 1.0f;
    static constexpr float soil_health_min_monoculture = 0.5f;

    static constexpr uint16_t monoculture_penalty_threshold = 3;  // years
    static constexpr float monoculture_soil_penalty_rate = 0.002f;

    static constexpr uint32_t southern_hemisphere_offset = 182;

    // Continuous-output cosine curve parameters
    static constexpr float perennial_base = 0.85f;
    static constexpr float perennial_amplitude = 0.25f;

    static constexpr float livestock_base = 0.85f;
    static constexpr float livestock_amplitude = 0.10f;

    static constexpr float timber_multiplier = 1.0f;
};

// ---------------------------------------------------------------------------
// SeasonalAgricultureModule -- ITickModule implementation
// ---------------------------------------------------------------------------
class SeasonalAgricultureModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "seasonal_agriculture"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"production"}; }
    std::vector<std::string_view> runs_before() const override { return {"price_engine"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx,
                          const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Access to internal farm season states (for testing and inspection) ---
    const std::unordered_map<uint32_t, FarmSeasonState>& farm_states() const {
        return farm_states_;
    }

    std::unordered_map<uint32_t, FarmSeasonState>& farm_states() {
        return farm_states_;
    }

    // --- Facility registry delegation (module stores its own view of ag facilities) ---
    void register_facility(const Facility& facility, CropCategory category,
                           uint32_t growing_season_start, uint32_t growing_season_length,
                           float base_growth_rate);

    // Register a continuous-output facility (perennial, livestock, timber).
    void register_continuous_facility(const Facility& facility, CropCategory category,
                                      uint32_t peak_tick = 182);

    // --- Utility ---
    static uint32_t good_id_from_string(const std::string& good_id_str);

    // Compute the seasonal yield multiplier for continuous-output categories.
    static float compute_seasonal_multiplier(CropCategory category,
                                              uint32_t tick_of_year,
                                              uint32_t peak_tick);

    // Determine the effective tick_of_year for a province, accounting for
    // Southern Hemisphere offset.
    static uint32_t effective_tick_of_year(uint32_t current_tick, float latitude);

    // Check whether a crop category follows the annual cycle (fallow/planting/growing/harvest).
    static bool is_annual_cycle(CropCategory category);

private:
    // Internal map: facility_id -> FarmSeasonState (for annual-cycle facilities).
    std::unordered_map<uint32_t, FarmSeasonState> farm_states_;

    // Facility tracking: facility_id -> Facility (lightweight copy for lookup).
    std::unordered_map<uint32_t, Facility> facilities_;

    // Continuous-output facilities: facility_id -> (category, peak_tick, output_good_id).
    struct ContinuousFacilityInfo {
        CropCategory category;
        uint32_t     peak_tick;
        std::string  output_good_id;  // string id of what this facility produces
    };
    std::unordered_map<uint32_t, ContinuousFacilityInfo> continuous_facilities_;

    // Output good for annual-cycle facilities (set at registration).
    std::unordered_map<uint32_t, std::string> annual_output_goods_;

    void process_annual_facility(uint32_t facility_id,
                                 const Facility& facility,
                                 const Province& province,
                                 const WorldState& state,
                                 DeltaBuffer& delta);

    void process_continuous_facility(uint32_t facility_id,
                                     const ContinuousFacilityInfo& info,
                                     const Facility& facility,
                                     const Province& province,
                                     const WorldState& state,
                                     DeltaBuffer& delta);
};

}  // namespace econlife
