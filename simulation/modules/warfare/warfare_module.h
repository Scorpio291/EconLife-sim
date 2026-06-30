#pragma once

// Warfare Module — war between province-polities (M6c foundation).
//
// At the dawn each province is a proto-polity (chiefdom / feudal lord). War is NOT a
// scripted raid: a polity attacks a REACHABLE neighbour (adjacency = the ox-cart reach)
// when the power balance favours it — the expected-value decision. Power is grounded in
// the economy (population × how well-fed it is = levy + surplus-fed soldiers). War is
// CONSERVED: it kills (a cohort-mortality spike on both sides, the defender worse),
// folded into mortality by population_aging — the population war-dips.
//
// This is the foundation slice. Grain/territory spoils, diplomatic relations, treaties,
// alliances, backstabbing, and empires (the hold problem) are the subsequent M6c
// layers — see docs/design/EconLife_War_and_Diplomacy_v01.md. Global (cross-province),
// dawn-regime-gated; modern war is the political_cycle's domain.

#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class WarfareModule : public ITickModule {
   public:
    explicit WarfareModule(const WarfareConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "warfare"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"subsistence"}; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Military power of a province-polity: the levy (population) scaled by how well-fed
    // it is (a surplus feeds more/better soldiers). Pure/static for unit testing.
    static float military_power(uint64_t population, float surplus_ratio,
                                const WarfareConfig& cfg);

   private:
    bool regime_active(std::string_view regime) const;
    WarfareConfig cfg_;
};

}  // namespace econlife
