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
// Production Constants
// ---------------------------------------------------------------------------
// Technology tier bonuses applied during production.
struct ProductionConstants {
    static constexpr float tech_tier_output_bonus_per_tier = 0.08f;    // +8% output per tier
    static constexpr float tech_tier_cost_reduction_per_tier = 0.05f;  // -5% input cost per tier

    // Quality ceiling parameters.
    // quality_ceiling = base + step * (facility.tech_tier - recipe.min_tech_tier)
    // Further capped by maturation_of(key_technology_node) for technology-intensive recipes.
    static constexpr float tech_quality_ceiling_base = 0.5f;
    static constexpr float tech_quality_ceiling_step = 0.1f;
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
};

// ---------------------------------------------------------------------------
// Recipe — defines a production process
//
// Recipes are loaded from CSV files in packages/base_game/ at startup.
// They are immutable after loading.
// ---------------------------------------------------------------------------
struct Recipe {
    std::string id;                     // unique string identifier
    std::string name;                   // human-readable name
    std::vector<RecipeInput> inputs;    // input goods required
    std::vector<RecipeOutput> outputs;  // output goods produced
    uint32_t min_tech_tier;             // minimum technology tier required
    float base_cost_per_tick;           // operating cost per tick at baseline
    bool is_technology_intensive;       // if true, quality is capped by maturation level
    std::string
        key_technology_node;  // technology node for maturation cap; "" for commodity recipes
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

}  // namespace econlife
