#pragma once

// Production module types.
// Core production types (NPCBusiness, RegionalMarket) are in economy_types.h.
// This header defines production-module-specific constants, recipe types,
// and facility types.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// Production Constants — defaults; overridable via production_config.json
// ---------------------------------------------------------------------------
struct ProductionConstants {
    float tech_tier_output_bonus_per_tier = 0.08f;
    float tech_tier_cost_reduction_per_tier = 0.05f;
    float tech_quality_ceiling_base = 0.5f;
    float tech_quality_ceiling_step = 0.1f;
    float worker_productivity_diminishing_factor = 0.15f;
    float minimum_input_fraction_to_produce = 0.1f;
};

// ---------------------------------------------------------------------------
// RecipeInput — single input requirement for a recipe
// ---------------------------------------------------------------------------
struct RecipeInput {
    std::string good_id;      // string identifier matching goods data file
    float quantity_per_tick;  // units consumed per tick at baseline
};

// ---------------------------------------------------------------------------
// RecipeOutput — single output product of a recipe
// ---------------------------------------------------------------------------
struct RecipeOutput {
    std::string good_id;      // string identifier matching goods data file
    float quantity_per_tick;  // units produced per tick at baseline
    float quality_base;       // base quality grade of the output (0.0-1.0)
    bool is_byproduct;        // true = secondary/waste output
};

// ---------------------------------------------------------------------------
// Recipe — defines a production process
//
// Recipes are loaded from CSV files in packages/base_game/ at startup.
// They are immutable after loading.
// ---------------------------------------------------------------------------
struct Recipe {
    std::string id;                     // unique string identifier (recipe_key)
    std::string name;                   // human-readable name
    std::string facility_type_key;      // which facility type runs this recipe
    std::vector<RecipeInput> inputs;    // input goods required
    std::vector<RecipeOutput> outputs;  // output goods produced
    uint32_t min_tech_tier;             // minimum technology tier required
    float labor_per_tick;               // labor units required per tick
    float energy_per_tick;              // energy units required per tick
    float base_cost_per_tick;           // operating cost per tick at baseline
    bool is_technology_intensive;       // if true, quality is capped by maturation level
    std::string key_technology_node;    // tech node for maturation cap; "" for commodities
    uint8_t era_available;              // era when recipe becomes available (1-5)
};

// ---------------------------------------------------------------------------
// Facility — a physical production site within a business
//
// Each facility runs exactly one recipe. A business may have multiple
// facilities. Facilities are assigned to exactly one province.
// ---------------------------------------------------------------------------
struct Facility {
    uint32_t id;
    uint32_t business_id;        // owning NPCBusiness.id
    uint32_t province_id;        // province where the facility is located
    std::string recipe_id;       // recipe this facility runs
    uint32_t tech_tier;          // facility's technology tier (>= recipe.min_tech_tier)
    float output_rate_modifier;  // multiplicative modifier on output rate (1.0 = baseline)
    float soil_health;           // 0.0-1.0; relevant for farm facilities only (1.0 = pristine)
    uint32_t worker_count;       // number of workers assigned to this facility
    bool is_operational;         // false = facility is shut down or under construction
};

// ---------------------------------------------------------------------------
// FacilityType — defines a category of production facility
//
// Loaded from CSV at startup. Immutable after loading.
// Signal weights feed the facility_signals module for evidence generation.
// ---------------------------------------------------------------------------
struct FacilityType {
    std::string key;                // unique string identifier
    std::string display_name;       // human-readable name
    std::string category;           // extraction, agriculture, processing, manufacturing
    float base_construction_cost;   // initial build cost
    float base_operating_cost;      // per-tick operating cost
    uint32_t max_workers;           // hard cap on labor input
    float signal_weight_noise;      // 0.0-1.0; noise observability
    float signal_weight_waste;      // 0.0-1.0; chemical waste observability
    float signal_weight_traffic;    // 0.0-1.0; foot/vehicle traffic
    float signal_weight_pollution;  // 0.0-1.0; air/water pollution
    float signal_weight_odor;       // 0.0-1.0; odor emissions
};

}  // namespace econlife
