#pragma once

// Production Module — province-parallel tick module that processes NPCBusiness
// entities each tick: consumes input goods, produces output goods, records
// derived demand, and updates business financials.
//
// See docs/interfaces/production/INTERFACE.md for the canonical specification.

#include <string>
#include <unordered_map>
#include <vector>

#include "core/tick/tick_module.h"
#include "modules/production/production_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPCBusiness;
class DeterministicRNG;

// ---------------------------------------------------------------------------
// RecipeRegistry — holds all loaded recipes, keyed by recipe id.
// Populated at startup from package content files. Immutable after loading.
// ---------------------------------------------------------------------------
class RecipeRegistry {
   public:
    void register_recipe(Recipe recipe);
    const Recipe* find(const std::string& recipe_id) const;
    const std::unordered_map<std::string, Recipe>& all() const { return recipes_; }

   private:
    std::unordered_map<std::string, Recipe> recipes_;
};

// ---------------------------------------------------------------------------
// FacilityRegistry — holds all facilities, indexed by business_id.
// ---------------------------------------------------------------------------
class FacilityRegistry {
   public:
    void register_facility(Facility facility);
    const std::vector<Facility>* find_by_business(uint32_t business_id) const;

   private:
    std::unordered_map<uint32_t, std::vector<Facility>> facilities_by_business_;
};

// ---------------------------------------------------------------------------
// ProductionModule — ITickModule implementation for production
// ---------------------------------------------------------------------------
class ProductionModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "production"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {}; }
    std::vector<std::string_view> runs_before() const override { return {"supply_chain"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Populate registries from WorldState data at init.
    void init_from_world_state(const WorldState& state);

    // --- Registry access (for test injection and runtime loading) ---
    RecipeRegistry& recipe_registry() { return recipe_registry_; }
    const RecipeRegistry& recipe_registry() const { return recipe_registry_; }

    FacilityRegistry& facility_registry() { return facility_registry_; }
    const FacilityRegistry& facility_registry() const { return facility_registry_; }

    // --- Utility: convert string good_id to uint32_t ---
    static uint32_t good_id_from_string(const std::string& good_id_str);

    // Informal market price discount factor (criminal sector).
    static constexpr float informal_price_discount = 0.7f;

    // --- Configuration ---
    ProductionConstants& config() { return config_; }
    const ProductionConstants& config() const { return config_; }

   private:
    ProductionConstants config_;
    RecipeRegistry recipe_registry_;
    FacilityRegistry facility_registry_;
    bool initialized_ = false;

    void process_business(const NPCBusiness& biz, const WorldState& state, DeltaBuffer& delta,
                          std::unordered_map<std::string, float>& available_supply,
                          DeterministicRNG& rng);

    void process_facility(const NPCBusiness& biz, const Facility& facility, const WorldState& state,
                          DeltaBuffer& delta,
                          std::unordered_map<std::string, float>& available_supply,
                          DeterministicRNG& rng);

    float get_price_for_business(const NPCBusiness& biz, uint32_t good_id,
                                 const WorldState& state) const;
};

}  // namespace econlife
