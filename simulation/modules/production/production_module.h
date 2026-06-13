#pragma once

// Production Module — province-parallel tick module that processes NPCBusiness
// entities each tick: consumes input goods, produces output goods, records
// derived demand, and updates business financials.
//
// See docs/interfaces/production/INTERFACE.md for the canonical specification.

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/config/package_config.h"
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
    explicit ProductionModule(const ProductionConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "production"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {}; }
    std::vector<std::string_view> runs_before() const override { return {"supply_chain"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Runs on the main thread before parallel dispatch each tick. Picks up
    // facilities appended to WorldState since the last sync — Phase 11
    // construction contracts deliver new facilities via NewFacilityDelta,
    // and without this hook the cached facility_registry_ would never see
    // them.
    void init_for_tick(const WorldState& state) override;

    // Populate registries from WorldState data at init.
    void init_from_world_state(const WorldState& state);

    // --- Registry access (for test injection and runtime loading) ---
    RecipeRegistry& recipe_registry() { return recipe_registry_; }
    const RecipeRegistry& recipe_registry() const { return recipe_registry_; }

    FacilityRegistry& facility_registry() { return facility_registry_; }
    const FacilityRegistry& facility_registry() const { return facility_registry_; }

    // --- Utility: convert string good_id to uint32_t ---
    static uint32_t good_id_from_string(const std::string& good_id_str);

    // --- Configuration ---
    ProductionConstants& config() { return config_; }
    const ProductionConstants& config() const { return config_; }

   private:
    ProductionConstants config_;
    RecipeRegistry recipe_registry_;
    FacilityRegistry facility_registry_;
    std::once_flag init_flag_;
    // Number of WorldState::facilities entries already registered into
    // facility_registry_. Used by init_for_tick() to register only the
    // tail (facilities appended since last tick) rather than re-walking
    // the whole vector or double-registering existing entries.
    std::size_t last_synced_facility_count_ = 0;

    void process_business(const NPCBusiness& biz, const WorldState& state, DeltaBuffer& delta,
                          std::unordered_map<std::string, float>& available_supply,
                          float electricity_ratio, DeterministicRNG& rng);

    void process_facility(const NPCBusiness& biz, const Facility& facility, const WorldState& state,
                          DeltaBuffer& delta,
                          std::unordered_map<std::string, float>& available_supply,
                          float electricity_ratio, DeterministicRNG& rng);

    // Generate the province's electricity for this tick from its endowment and
    // emit the resulting market deltas (electricity supply produced/consumed; fossil
    // fuel burned). Returns the brownout ratio = clamp(generation / demand, 0, 1):
    // 1.0 = demand fully met, < 1.0 = energy shortfall throttling production output.
    // Renewables (solar/wind/geothermal capacity + hydro) generate matter-free; the
    // shortfall burns the province's fossil-fuel stock, consuming that matter — so
    // energy is bound to physics: fuel matter -> electricity -> work.
    float generate_province_energy(uint32_t province_idx, const WorldState& state,
                                   const std::vector<const NPCBusiness*>& province_businesses,
                                   std::unordered_map<std::string, float>& available_supply,
                                   DeltaBuffer& delta) const;

    float get_price_for_business(const NPCBusiness& biz, uint32_t good_id,
                                 const WorldState& state) const;

    ProductionConfig cfg_;
};

}  // namespace econlife
