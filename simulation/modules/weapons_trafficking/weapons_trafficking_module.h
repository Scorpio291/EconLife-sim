#pragma once

// weapons_trafficking module header.
// Province-parallel execution: each province's weapons supply chain operates
// independently. Cross-province effects go through CrossProvinceDeltaBuffer.
//
// Processes weapons trafficking: legal market diversion, corrupt official
// procurement, informal market pricing driven by territorial conflict demand,
// and chain-of-custody evidence generation.
//
// See docs/interfaces/weapons_trafficking/INTERFACE.md for the canonical specification.

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "weapons_trafficking_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPCBusiness;

class WeaponsTraffickingModule : public ITickModule {
   public:
    explicit WeaponsTraffickingModule(const WeaponsTraffickingConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "weapons_trafficking"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"criminal_operations", "production"};
    }

    std::vector<std::string_view> runs_before() const override { return {"investigator_engine"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void init_for_tick(const WorldState& state) override;
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (public for testing) ---

    // Compute informal spot price for a weapon type in a province
    static float compute_informal_spot_price(float base_price, float conflict_demand_modifier,
                                             float supply_this_tick, float price_floor_supply);

    // Get territorial conflict demand modifier for a conflict stage
    static float get_conflict_demand_modifier(uint8_t conflict_stage);

    // Compute diversion output from manufacturing
    static float compute_diversion_output(float total_output, float diversion_fraction,
                                          float max_diversion_fraction);

    // Clamp diversion fraction to maximum
    static float clamp_diversion_fraction(float requested_fraction, float max_diversion_fraction);

    // Check if heavy weapons transfer triggers embargo investigation
    static bool is_embargo_item(WeaponType weapon_type);

    // Compute chain-of-custody evidence actionability
    static float compute_chain_custody_actionability(float base_actionability);

   private:
    WeaponsTraffickingConfig cfg_;

    // Internal state: diversion records per tick
    std::vector<WeaponDiversionRecord> diversion_records_;
    std::vector<WeaponProcurementRecord> procurement_records_;
};

}  // namespace econlife
