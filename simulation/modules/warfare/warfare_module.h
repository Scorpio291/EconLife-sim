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
#include <cstdint>
#include <map>
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

    // Symmetric key for a province pair (relations are mutual).
    static uint64_t pair_key(uint32_t a, uint32_t b);

    // Diplomatic relation for a pair (0 if never interacted). For tests/observability.
    float relation(uint32_t a, uint32_t b) const;

    // Module state round-trip (relations_ + the reset flag): diplomacy must survive
    // save/load or a loaded game diverges from the same-seed uninterrupted run.
    void serialize_state(std::vector<uint8_t>& out) const override;
    bool deserialize_state(const uint8_t* data, size_t size) override;

   private:
    bool regime_active(std::string_view regime) const;
    WarfareConfig cfg_;
    // Per-province-pair diplomatic relations in [-1, 1] (module state; persists across
    // ticks within a run). Warm pairs are de-facto allies that do not fight.
    std::map<uint64_t, float> relations_;
    // True while the last published war_mortality state may contain a value > 1.0;
    // lets the regime-exit path publish a one-time reset (no stale phantom war).
    bool war_state_dirty_ = false;
};

}  // namespace econlife
