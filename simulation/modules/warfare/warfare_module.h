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

class DeterministicRNG;

struct WorldState;
struct DeltaBuffer;

class WarfareModule : public ITickModule {
   public:
    explicit WarfareModule(const WarfareConfig& cfg = {},
                           const GrainLogisticsConfig& grain_cfg = {})
        : cfg_(cfg), grain_cfg_(grain_cfg) {}

    std::string_view name() const noexcept override { return "warfare"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"subsistence"}; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Lanchester square-law contest: P(attacker wins) = Sa^2 / (Sa^2 + Sb^2).
    // Pure/static for unit testing.
    static float p_attacker_wins(float attacker_strength, float defender_strength);

    // How well-fed an army fights: forage covers cfg.forage_share of its rations
    // (off the land); the rest must be DRAWN from the granary. Returns the strength
    // factor in [forage_share, 1]. Pure/static for unit testing.
    static float campaign_fed_factor(float rations_needed, float rations_drawn,
                                     const WarfareConfig& cfg);

    // Symmetric key for a province pair (relations are mutual).
    static uint64_t pair_key(uint32_t a, uint32_t b);

    // Diplomatic relation for a pair (0 if never interacted). For tests/observability.
    float relation(uint32_t a, uint32_t b) const;

    // Polity a province belongs to (M6c-5). Lazily emergent: a province that has
    // never been conquered is its own polity (== its index). For tests/observability.
    uint32_t polity_of(uint32_t province) const;

    // Does the polity seat currently host a great commander (M6c-6, Alexander)?
    // For tests/observability.
    bool has_leader(uint32_t seat, uint32_t year) const;

    // Module state round-trip (relations_, polities, win ledger, reset flag):
    // diplomacy and the political map must survive save/load or a loaded game
    // diverges from the same-seed uninterrupted run.
    void serialize_state(std::vector<uint8_t>& out) const override;
    bool deserialize_state(const uint8_t* data, size_t size) override;

   private:
    bool regime_active(std::string_view regime) const;
    WarfareConfig cfg_;
    GrainLogisticsConfig grain_cfg_;  // military supply moves on the same carts (one
                                      // logistics law: delivered_fraction per link)
    // Per-province-pair diplomatic relations in [-1, 1] (module state; persists across
    // ticks within a run). Warm pairs are de-facto allies that do not fight.
    std::map<uint64_t, float> relations_;
    // True while the last published war_death_fraction may be > 0;
    // lets the regime-exit path publish a one-time reset (no stale phantom war).
    bool war_state_dirty_ = false;
    // Emergent nesting polities (M6c-5): province -> polity id. Sparse — a province
    // absent from the map is its own polity (ownership emerges from settlement).
    // Conquest reassigns the loser's WHOLE polity (nesting: a beaten kingdom joins the
    // empire with all its provinces); hold-failure secedes a member back to itself.
    std::map<uint32_t, uint32_t> polity_of_;
    // Decisive-win ledger per DIRECTED (attacker,defender) pair; at
    // cfg_.absorb_after_wins the defender's polity is absorbed.
    std::map<uint64_t, uint32_t> win_counts_;
    // Conqueror state (M6c-6). ALEXANDER: polity seat -> the year its great
    // commander's tenure ends (power multiplied while active; fragmentation follows
    // the death via the hold problem). ROME: member province -> the year it was
    // absorbed (integration/cohesion grows with tenure).
    std::map<uint32_t, uint32_t> leader_until_;
    std::map<uint32_t, uint32_t> member_since_;
};

}  // namespace econlife
