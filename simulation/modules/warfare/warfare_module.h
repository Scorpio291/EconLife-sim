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

#include <algorithm>
#include <cstdint>
#include <string_view>
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

    // war_death_fraction is published here and consumed by population_aging in the
    // SAME annual tick, so the ordering must be declared rather than left to the
    // topological sort's alphabetical tie-break. It was undeclared, and the
    // tie-break happened to place population_aging first: every year's battle dead
    // were applied a year late, were lost entirely if the game was saved in
    // between (the field is transient by design), and were wiped without ever
    // being applied if the era left the pre-market regime mid-year (the one-time
    // reset below fired first). Same-tick consumption removes all three.
    std::vector<std::string_view> runs_before() const override { return {"population_aging"}; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Lanchester square-law contest: P(attacker wins) = Sa^2 / (Sa^2 + Sb^2).
    // Pure/static for unit testing.
    static float p_attacker_wins(float attacker_strength, float defender_strength);

    // ASABIYA (R3C) — one year of Ibn Khaldun's law.
    //
    //     dA/dt = growth * A * (1 - A) * frontier  -  decay * A * (1 - frontier)
    //
    // Solidarity is forged where a people lives against an out-group and must hold
    // together or die, and it decays where safety makes it unnecessary. `frontier` is the
    // share of a province's neighbours under another polity: 1 means surrounded by
    // strangers, 0 means deep inside one's own realm.
    //
    // Logistic on the way up — which is why a people with no solidarity at all could
    // never develop any, and why the stock is seeded above zero — and exponential on the
    // way down, so softening is slower than cohering but never quite complete. Bounded in
    // [0,1] by the form itself, not by a clamp. Pure/static.
    static float asabiya_year(float asabiya, float frontier_share, const WarfareConfig& cfg) {
        const float a = std::clamp(asabiya, 0.0f, 1.0f);
        const float f = std::clamp(frontier_share, 0.0f, 1.0f);
        const float growth = std::max(0.0f, cfg.asabiya_growth_per_year) * a * (1.0f - a) * f;
        const float decay = std::max(0.0f, cfg.asabiya_decay_per_year) * a * (1.0f - f);
        return std::clamp(a + growth - decay, 0.0f, 1.0f);
    }

    // What a people fights with besides numbers: a wholly cohesive one fights as though
    // it were (1 + asabiya_strength_weight) times its size. This is why frontier peoples
    // conquer settled empires that outnumber them. Pure/static.
    static float asabiya_strength_mult(float asabiya, const WarfareConfig& cfg) {
        return 1.0f + std::max(0.0f, cfg.asabiya_strength_weight) * std::clamp(asabiya, 0.0f, 1.0f);
    }

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
