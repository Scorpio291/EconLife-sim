#pragma once

#include <cstdint>
#include <optional>

namespace econlife {

// =============================================================================
// Seasonal Agricultural Production Types — TDD §44
// =============================================================================

// --- §44.1 — CropCategory ---
// Aligns with the FARM_STRESS_SENSITIVITY table in Commodities & Factories v2.3.
// Each recipe's CSV row carries a crop_category column.

enum class CropCategory : uint8_t {
    annual_grain = 0,  // Wheat, corn, rice, barley.
                       // One harvest per year in temperate climates.
                       // Two harvests per year in humid tropical (Af, Am, Aw)
                       //   with irrigation.
                       // Stress sensitivity: 0.55 (Commodities Climate Effects).

    annual_oilseed = 1,  // Soybean, rapeseed, sunflower.
                         // One harvest per year. Shorter growing season than grain.
                         // Stress sensitivity: 0.50.

    annual_fiber = 2,  // Cotton.
                       // One harvest per year. Long growing season.
                       // Stress sensitivity: 0.60.

    perennial_tree = 3,  // Coffee, rubber, cocoa, palm oil.
                         // Does NOT follow SeasonPhase cycle -- produces continuously
                         //   with a seasonal yield_multiplier peak.
                         // Trees take 3-5 years to reach productive maturity (§44.4).
                         // Stress sensitivity varies (coffee: 0.70, rubber: 0.65).

    sugarcane = 4,  // Annual or ratoon cycle (replant every 3-5 years, ratoon
                    //   harvests annually in between). V1: modelled as annual.
                    // Stress sensitivity: 0.35.

    livestock = 5,  // Cattle, pigs, poultry.
                    // Continuous output with seasonal feed-cost variation.
                    // Does NOT follow SeasonPhase cycle.
                    // Stress sensitivity: 0.30.

    timber = 6,  // Softwood, hardwood plantation.
                 // Harvest cycle 8-30 years depending on species.
                 // V1 simplified: continuous output at slow rate.
                 // Stress sensitivity: 0.25.
};

// V1 seasonal cycle applies to: annual_grain, annual_oilseed, annual_fiber, sugarcane.
// Continuous-with-modifier applies to: perennial_tree, livestock, timber (§44.5).

// --- §44.2 — SeasonPhase ---

enum class SeasonPhase : uint8_t {
    fallow = 0,  // Off-season. No growth. Soil_health recovers at
                 // config.seasonal.fallow_soil_recovery_rate per tick.
                 // Duration: TICKS_PER_YEAR - growing_season_length
                 //           - planting_ticks - harvest_ticks.

    planting = 1,  // Planting window (config.seasonal.planting_duration_ticks).
                   // Consumes seed_input from Facility.input_buffer.
                   // If seed not available -> phase stays planting; farm cannot grow this year
                   //   -> generates a supply gap that begins feeding into price the following
                   //      harvest period when supply fails to materialise.
                   // Transition to growing when seed consumed.

    growing = 2,  // Active growth period. No output from the facility.
                  // Accumulates yield in FarmSeasonState.pending_harvest each tick:
                  //   daily_growth = base_growth_rate * drought_modifier * flood_modifier
                  //                  * soil_health * fertilizer_efficiency
                  //   pending_harvest += daily_growth
                  // Duration: growing_season_length_ticks (climate-zone dependent).
                  // Transition to harvest when growing season ends.

    harvest = 3,  // Harvest window (config.seasonal.harvest_duration_ticks).
                  // Each tick during harvest window:
                  //   release_per_tick = pending_harvest / harvest_duration_ticks
                  //   RegionalMarket.supply(good_id, province_id) += release_per_tick
                  //   pending_harvest -= release_per_tick
                  // After harvest_duration_ticks: pending_harvest = 0; transition to fallow.
                  // Rationale for spreading harvest over multiple ticks rather than
                  //   one-tick burst: mechanised harvest takes days to weeks; spreading
                  //   over ~7 ticks prevents an artificial single-tick price crash.
};

// --- §44.3 — FarmSeasonState ---
// Added as a field on Facility for all facilities with sector == agriculture
// and crop_category in the annual-cycle set.

struct FarmSeasonState {
    CropCategory crop_category;
    SeasonPhase current_phase;
    uint32_t phase_started_tick;     // tick when current phase began
    uint32_t growing_season_start;   // tick-of-year (0-364) when growing begins;
                                     // derived from province KoppenZone + CropCategory
                                     // at facility creation (§44.3a)
    uint32_t growing_season_length;  // ticks; from config.seasonal.growing_length table

    float pending_harvest;  // accumulated yield not yet released to market;
                            // units: same as recipe output_per_tick * TICKS_PER_YEAR.
                            // Built during growing phase; depleted during harvest phase.

    float base_growth_rate;  // output units per tick during growing phase.
                             // = recipe.base_output_per_tick * TICKS_PER_YEAR
                             //   / growing_season_length_ticks
                             // Ensures total annual output equals the recipe's
                             // intended annual production if no climate stress.
                             // Re-derived when recipe or facility tech_tier changes.

    uint32_t harvest_remaining_ticks;  // countdown during harvest phase

    float annual_yield_last;   // pending_harvest at start of last harvest;
                               // used by NPCBusiness strategic decision and UI display.
    bool seed_planted;         // true once planting phase successfully consumed seed input.
    uint16_t years_same_crop;  // consecutive years with same CropCategory;
                               // monoculture penalty to soil_health applies above
                               // config.seasonal.monoculture_penalty_threshold years.

    // Invariants:
    //   pending_harvest >= 0.0
    //   base_growth_rate >= 0.0
    //   growing_season_start in [0, TICKS_PER_YEAR)
    //   If current_phase == harvest: harvest_remaining_ticks > 0
    //   If current_phase != harvest: harvest_remaining_ticks == 0 (or stale)
};

// FIELD ADDITION to Facility:
//   std::optional<FarmSeasonState> farm_season_state;
//     Present only for agriculture-sector facilities with annual cycle.
//     Absent for perennial_tree, livestock, timber categories (§44.5).
//     Absent for all non-agriculture facilities.
//
// FIELD ADDITION to Facility (non-FarmSeasonState agricultural facilities, §44.5):
//   float seasonal_yield_multiplier;
//     0.5-1.3; recomputed monthly from tick_of_year + CropCategory profile.
//     Perennial tree (coffee, rubber): peaks at harvest month for the zone; 0.5 in off-peak.
//     Livestock: 0.85-1.05 (summer heat stress lowers output; spring flush for dairy).
//     Timber: constant 1.0 (multi-year cycle abstracted to flat rate in V1).

}  // namespace econlife
