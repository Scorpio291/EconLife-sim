// Seasonal Agriculture Module -- implementation.
// See seasonal_agriculture_module.h for class declarations and
// docs/interfaces/seasonal_agriculture/INTERFACE.md for canonical spec.
//
// Core algorithm:
//   1. Annual-cycle facilities: fallow -> planting -> growing -> harvest
//   2. Growing phase: accumulate yield via daily_growth formula
//   3. Harvest phase: spread release over 14 ticks into MarketDelta.supply_delta
//   4. Fallow phase: soil recovery
//   5. Continuous-output facilities: cosine-curve seasonal multiplier

#include "modules/seasonal_agriculture/seasonal_agriculture_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"

#include <algorithm>
#include <cmath>

// Mathematical constant - defined outside namespace to avoid MSVC issues.
#ifndef M_PI
static constexpr double LOCAL_PI = 3.14159265358979323846;
#else
static constexpr double LOCAL_PI = M_PI;
#endif

namespace econlife {

// ===========================================================================
// Utility
// ===========================================================================

uint32_t SeasonalAgricultureModule::good_id_from_string(const std::string& good_id_str) {
    // Same deterministic hash as ProductionModule for V1 prototype consistency.
    uint32_t hash = 0;
    for (char c : good_id_str) {
        hash = hash * 31 + static_cast<uint32_t>(c);
    }
    return hash;
}

bool SeasonalAgricultureModule::is_annual_cycle(CropCategory category) {
    switch (category) {
        case CropCategory::annual_grain:
        case CropCategory::annual_oilseed:
        case CropCategory::annual_fiber:
        case CropCategory::sugarcane:
            return true;
        case CropCategory::perennial_tree:
        case CropCategory::livestock:
        case CropCategory::timber:
            return false;
    }
    return false;  // unreachable; silences MSVC warning
}

uint32_t SeasonalAgricultureModule::effective_tick_of_year(uint32_t current_tick,
                                                           float latitude) {
    uint32_t tick_of_year = current_tick % SeasonalAgricultureConstants::TICKS_PER_YEAR;
    if (latitude < 0.0f) {
        tick_of_year = (tick_of_year + SeasonalAgricultureConstants::southern_hemisphere_offset)
                       % SeasonalAgricultureConstants::TICKS_PER_YEAR;
    }
    return tick_of_year;
}

float SeasonalAgricultureModule::compute_seasonal_multiplier(CropCategory category,
                                                              uint32_t tick_of_year,
                                                              uint32_t peak_tick) {
    switch (category) {
        case CropCategory::perennial_tree: {
            float phase = 2.0f * static_cast<float>(LOCAL_PI)
                        * static_cast<float>(static_cast<int32_t>(tick_of_year) - static_cast<int32_t>(peak_tick))
                        / static_cast<float>(SeasonalAgricultureConstants::TICKS_PER_YEAR);
            return SeasonalAgricultureConstants::perennial_base
                 + SeasonalAgricultureConstants::perennial_amplitude * std::cos(phase);
        }
        case CropCategory::livestock: {
            float phase = 2.0f * static_cast<float>(LOCAL_PI)
                        * static_cast<float>(static_cast<int32_t>(tick_of_year) - static_cast<int32_t>(peak_tick))
                        / static_cast<float>(SeasonalAgricultureConstants::TICKS_PER_YEAR);
            return SeasonalAgricultureConstants::livestock_base
                 + SeasonalAgricultureConstants::livestock_amplitude * std::cos(phase);
        }
        case CropCategory::timber:
            return SeasonalAgricultureConstants::timber_multiplier;
        default:
            return 1.0f;  // non-continuous categories should not call this
    }
}

// ===========================================================================
// Registration
// ===========================================================================

void SeasonalAgricultureModule::register_facility(
        const Facility& facility,
        CropCategory category,
        uint32_t growing_season_start,
        uint32_t growing_season_length,
        float base_growth_rate) {
    facilities_[facility.id] = facility;

    FarmSeasonState state{};
    state.crop_category = category;
    state.current_phase = SeasonPhase::fallow;
    state.phase_started_tick = 0;
    state.growing_season_start = growing_season_start;
    state.growing_season_length = growing_season_length;
    state.pending_harvest = 0.0f;
    state.base_growth_rate = base_growth_rate;
    state.harvest_remaining_ticks = 0;
    state.annual_yield_last = 0.0f;
    state.seed_planted = false;
    state.years_same_crop = 0;

    farm_states_[facility.id] = state;

    // Default output good_id based on recipe_id. The caller may override this
    // by calling annual_output_goods_ directly, but for simplicity in V1
    // we use the recipe_id as the output good identifier.
    annual_output_goods_[facility.id] = facility.recipe_id;
}

void SeasonalAgricultureModule::register_continuous_facility(
        const Facility& facility,
        CropCategory category,
        uint32_t peak_tick) {
    facilities_[facility.id] = facility;
    continuous_facilities_[facility.id] = ContinuousFacilityInfo{
        category,
        peak_tick,
        facility.recipe_id  // use recipe_id as output good for V1
    };
}

// ===========================================================================
// Tick Execution
// ===========================================================================

void SeasonalAgricultureModule::execute_province(
        uint32_t province_idx,
        const WorldState& state,
        DeltaBuffer& province_delta) {
    // Skip provinces not at full LOD.
    if (province_idx >= state.provinces.size()) {
        return;
    }
    const Province& province = state.provinces[province_idx];
    if (province.lod_level != SimulationLOD::full) {
        return;
    }

    // Process annual-cycle facilities in this province.
    // Iterate in ascending facility_id order for determinism.
    std::vector<uint32_t> annual_ids;
    annual_ids.reserve(farm_states_.size());
    for (const auto& [fid, fstate] : farm_states_) {
        auto fit = facilities_.find(fid);
        if (fit != facilities_.end() && fit->second.province_id == province_idx) {
            annual_ids.push_back(fid);
        }
    }
    std::sort(annual_ids.begin(), annual_ids.end());

    for (uint32_t fid : annual_ids) {
        const Facility& fac = facilities_.at(fid);
        if (!fac.is_operational) {
            continue;
        }
        process_annual_facility(fid, fac, province, state, province_delta);
    }

    // Process continuous-output facilities in this province.
    std::vector<uint32_t> continuous_ids;
    continuous_ids.reserve(continuous_facilities_.size());
    for (const auto& [fid, info] : continuous_facilities_) {
        auto fit = facilities_.find(fid);
        if (fit != facilities_.end() && fit->second.province_id == province_idx) {
            continuous_ids.push_back(fid);
        }
    }
    std::sort(continuous_ids.begin(), continuous_ids.end());

    for (uint32_t fid : continuous_ids) {
        const Facility& fac = facilities_.at(fid);
        if (!fac.is_operational) {
            continue;
        }
        const ContinuousFacilityInfo& info = continuous_facilities_.at(fid);
        process_continuous_facility(fid, info, fac, province, state, province_delta);
    }
}

void SeasonalAgricultureModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Province-parallel modules dispatch through execute_province().
    // This fallback processes all provinces sequentially if called directly.
    for (uint32_t p = 0; p < static_cast<uint32_t>(state.provinces.size()); ++p) {
        execute_province(p, state, delta);
    }
}

// ===========================================================================
// Annual-Cycle Facility Processing
// ===========================================================================

void SeasonalAgricultureModule::process_annual_facility(
        uint32_t facility_id,
        const Facility& facility,
        const Province& province,
        const WorldState& state,
        DeltaBuffer& delta) {
    auto it = farm_states_.find(facility_id);
    if (it == farm_states_.end()) {
        return;
    }
    FarmSeasonState& fs = it->second;

    const float latitude = province.geography.latitude;
    const uint32_t tick_of_year = effective_tick_of_year(state.current_tick, latitude);

    // Compute the planting start tick-of-year.
    // Planting starts planting_duration_ticks before growing_season_start.
    uint32_t planting_start;
    if (fs.growing_season_start >= SeasonalAgricultureConstants::planting_duration_ticks) {
        planting_start = fs.growing_season_start
                       - SeasonalAgricultureConstants::planting_duration_ticks;
    } else {
        // Wrap around year boundary.
        planting_start = SeasonalAgricultureConstants::TICKS_PER_YEAR
                       - (SeasonalAgricultureConstants::planting_duration_ticks - fs.growing_season_start);
    }

    // Harvest start tick-of-year.
    uint32_t harvest_start = (fs.growing_season_start + fs.growing_season_length)
                           % SeasonalAgricultureConstants::TICKS_PER_YEAR;

    // --- Climate modifiers ---
    // Use drought_modifier and flood_modifier from RegionConditions if available.
    float drought_mod = province.conditions.drought_modifier;
    float flood_mod = province.conditions.flood_modifier;

    // Clamp to valid range.
    drought_mod = std::max(0.0f, std::min(1.0f, drought_mod));
    flood_mod = std::max(0.0f, std::min(1.0f, flood_mod));

    // Current soil_health from facility struct.
    float soil_health = facility.soil_health;

    // --- Phase state machine ---
    switch (fs.current_phase) {
        case SeasonPhase::fallow: {
            // Soil recovery during fallow.
            soil_health += SeasonalAgricultureConstants::fallow_soil_recovery_rate;
            soil_health = std::min(soil_health, SeasonalAgricultureConstants::soil_health_max);

            // Transition: fallow -> planting when tick_of_year reaches planting_start.
            if (tick_of_year == planting_start) {
                fs.current_phase = SeasonPhase::planting;
                fs.phase_started_tick = state.current_tick;
                fs.seed_planted = false;
            }
            break;
        }

        case SeasonPhase::planting: {
            // Check if planting duration has elapsed.
            uint32_t ticks_in_phase = state.current_tick - fs.phase_started_tick;
            if (ticks_in_phase >= SeasonalAgricultureConstants::planting_duration_ticks) {
                // Seed availability check: in V1, we assume seed is available
                // if the facility is operational. Full seed tracking is deferred.
                // For testing purposes, seed_planted can be set externally.
                if (fs.seed_planted) {
                    fs.current_phase = SeasonPhase::growing;
                    fs.phase_started_tick = state.current_tick;
                    fs.pending_harvest = 0.0f;
                }
                // If seed not planted, stay in planting phase (missed season).
            }
            break;
        }

        case SeasonPhase::growing: {
            // Accumulate yield.
            float daily_growth = fs.base_growth_rate * drought_mod * flood_mod * soil_health;

            // Monoculture penalty: if years_same_crop >= threshold, degrade soil.
            if (fs.years_same_crop >= SeasonalAgricultureConstants::monoculture_penalty_threshold) {
                soil_health -= SeasonalAgricultureConstants::monoculture_soil_penalty_rate;
                soil_health = std::max(SeasonalAgricultureConstants::soil_health_min_monoculture,
                                       soil_health);
            }

            fs.pending_harvest += daily_growth;

            // Transition: growing -> harvest when tick_of_year reaches harvest_start.
            if (tick_of_year == harvest_start) {
                fs.annual_yield_last = fs.pending_harvest;
                fs.current_phase = SeasonPhase::harvest;
                fs.phase_started_tick = state.current_tick;
                fs.harvest_remaining_ticks = SeasonalAgricultureConstants::harvest_duration_ticks;
            }
            break;
        }

        case SeasonPhase::harvest: {
            // Spread harvest over harvest_remaining_ticks.
            if (fs.harvest_remaining_ticks > 0) {
                float release_per_tick = fs.pending_harvest
                                       / static_cast<float>(fs.harvest_remaining_ticks);

                // Write supply delta to market.
                MarketDelta supply_delta{};
                auto good_it = annual_output_goods_.find(facility_id);
                if (good_it != annual_output_goods_.end()) {
                    supply_delta.good_id = good_id_from_string(good_it->second);
                } else {
                    supply_delta.good_id = good_id_from_string(facility.recipe_id);
                }
                supply_delta.region_id = facility.province_id;
                supply_delta.supply_delta = release_per_tick;
                delta.market_deltas.push_back(supply_delta);

                fs.pending_harvest -= release_per_tick;
                fs.harvest_remaining_ticks--;
            }

            // Transition: harvest -> fallow when harvest_remaining_ticks reaches 0.
            if (fs.harvest_remaining_ticks <= 0) {
                fs.current_phase = SeasonPhase::fallow;
                fs.phase_started_tick = state.current_tick;
                fs.pending_harvest = 0.0f;
                fs.seed_planted = false;
                fs.years_same_crop++;
            }
            break;
        }
    }

    // Write back soil_health to our tracked facility copy.
    // (The real Facility in WorldState is const; we update our internal copy.
    //  In the full system, a FacilityDelta would propagate this.)
    facilities_[facility_id].soil_health = soil_health;
}

// ===========================================================================
// Continuous-Output Facility Processing
// ===========================================================================

void SeasonalAgricultureModule::process_continuous_facility(
        uint32_t facility_id,
        const ContinuousFacilityInfo& info,
        const Facility& facility,
        const Province& province,
        const WorldState& state,
        DeltaBuffer& delta) {
    const float latitude = province.geography.latitude;
    const uint32_t tick_of_year = effective_tick_of_year(state.current_tick, latitude);

    float seasonal_mult = compute_seasonal_multiplier(info.category, tick_of_year, info.peak_tick);

    // Climate modifier: use (1.0 - climate_stress_current) as a combined modifier.
    float climate_modifier = 1.0f - province.climate.climate_stress_current;
    climate_modifier = std::max(0.0f, std::min(1.0f, climate_modifier));

    float output = facility.output_rate_modifier * seasonal_mult * climate_modifier;

    // Clamp NaN or negative.
    if (std::isnan(output) || output < 0.0f) {
        output = 0.0f;
    }

    if (output <= 0.0f) {
        return;
    }

    // Write supply delta.
    MarketDelta supply_delta{};
    supply_delta.good_id = good_id_from_string(info.output_good_id);
    supply_delta.region_id = facility.province_id;
    supply_delta.supply_delta = output;
    delta.market_deltas.push_back(supply_delta);
}

}  // namespace econlife
