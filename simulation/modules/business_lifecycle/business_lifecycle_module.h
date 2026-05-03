#pragma once

// business_lifecycle module header.
// Sequential (not province-parallel): era-transition effects span all provinces.
//
// Detects era transitions and applies two effects on the tick immediately after
// a transition:
//   1. Stranded-asset penalties — revenue/cost shocks to businesses in sectors
//      made obsolete by the new era (e.g., fossil-fuel energy in Era 3+).
//   2. Era-entrant spawning — new businesses in sectors that emerge with the
//      new era (e.g., technology fast-expanders in Era 2+).
//
// All tuning constants are in packages/base_game/config/business_lifecycle.json.
// See docs/interfaces/business_lifecycle/INTERFACE.md for the canonical specification.

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;

class BusinessLifecycleModule : public ITickModule {
   public:
    explicit BusinessLifecycleModule(const BusinessLifecycleConfig& cfg) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "business_lifecycle"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"technology"}; }

    std::vector<std::string_view> runs_before() const override {
        return {"npc_business", "production"};
    }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

   private:
    BusinessLifecycleConfig cfg_;

    // Apply revenue/cost penalties to businesses in sectors stranded by new_era.
    void apply_stranded_asset_penalties(const WorldState& state, DeltaBuffer& delta,
                                        uint8_t new_era) const;

    // Spawn new businesses in sectors that emerge with new_era.
    void spawn_era_entrants(const WorldState& state, DeltaBuffer& delta, uint8_t new_era) const;
};

}  // namespace econlife
