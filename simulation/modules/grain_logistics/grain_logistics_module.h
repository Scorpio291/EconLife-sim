#pragma once

// Grain Logistics Module — the "tyranny of the ox" (medieval band, design §3.5).
//
// A draft team eats the grain it hauls, so surplus has a hard economic radius:
// beyond it the load is entirely consumed in transit and delivers nothing. Land
// transport is the tyranny; water (river/coast) is an order of magnitude cheaper.
// This module computes each province's NET FEEDABLE SURPLUS — its own haulable
// surplus plus what neighbours can deliver before the oxen eat it — the catchment
// surplus a town/castle can be fed from (consumed by genesis in M3).
//
// Conserved: the grain consumed in transit is accounted as draft sustenance, never
// vanished (delivered + eaten == exported). Global (cross-province: reads the link
// topology), so NOT province-parallel. Regime-gated to the pre-market (commons) eras
// like subsistence; inert in market eras (no commons surplus to haul).
//
// Built as the general "link -> deliverable fraction" case so the space age can
// extend it with latency for the lightspeed limit — see
// docs/design/EconLife_Logistics_and_Political_Scale_v01.md.

#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "core/world_state/geography.h"  // LinkType

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class GrainLogisticsModule : public ITickModule {
   public:
    explicit GrainLogisticsModule(const GrainLogisticsConfig& cfg = {},
                                  const SubsistenceConfig& subsistence_cfg = {})
        : cfg_(cfg), subsistence_cfg_(subsistence_cfg) {}

    std::string_view name() const noexcept override { return "grain_logistics"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    // Reads the per-province grain_surplus subsistence publishes; order after it.
    std::vector<std::string_view> runs_after() const override { return {"subsistence"}; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Fraction of a load delivered across a link after the draft team eats its share.
    // Pure and static (unit-testable). Water << land; mountains/heavy-gravity shrink
    // it toward 0 (no economic haul); roads raise it.
    static float delivered_fraction(LinkType type, float terrain_cost, float infra_bonus,
                                    float gravity_g, const GrainLogisticsConfig& cfg);

   private:
    bool regime_active(std::string_view regime) const;
    GrainLogisticsConfig cfg_;
    SubsistenceConfig subsistence_cfg_;  // granary targets (reserve years, per-capita
                                         // need) — one source of truth for the store
                                         // scale the diffusion flows against
};

}  // namespace econlife
