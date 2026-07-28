#pragma once

// Subsistence Module — the commons food economy of the dawn.
//
// Before money and markets, the founding population feeds itself by working the
// land directly: foraging, subsistence farming, hunting, fishing, herding. There
// are no firms and no facilities — production is the whole province population
// applied to the province's natural capital, capped by a carrying ceiling (fixed
// land feeds only so many). This module models that directly, outside the
// firm/facility/market machinery, and records the food surplus over need —
// the master variable that later frees labour for specialists and trade.
//
// It is regime-gated: active only in eras whose economic_regime is in the
// configured set (subsistence/barter by default). In market eras it is inert, so
// the modern economy is untouched. See docs/design/
// EconLife_Origin_Economy_and_Early_Jobs_v01.md.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "core/world_gen/world_class.h"  // WorldHazardSettings, earth_hazard
#include "core/world_state/geography.h"  // Province (inline shared-law statics)

namespace econlife {

struct WorldState;
struct DeltaBuffer;
class DeterministicRNG;

class SubsistenceModule : public ITickModule {
   public:
    explicit SubsistenceModule(const SubsistenceConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "subsistence"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    // Needs the current era resolved (technology sets it); produces a signal other
    // province-level consumers can read, so run it early.
    std::vector<std::string_view> runs_after() const override { return {"technology"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Pure, testable production model ---

    // The province's natural capital available to the commons food economy — the
    // weighted blend of arable land, forage, fisheries, and agricultural technique.
    // Shared with entry materialization (one law). Pure/static. Defined inline:
    // core/world_gen/premarket_genesis.cpp runs the SAME law, and core must not
    // need module object code to link.
    // `soil_health` is the fraction of pristine fertility the worked land retains
    // (cohort_stats->soil_health). It scales the FARMED portion of natural capital —
    // worn-out land grows less — while forage and fisheries are untouched by tillage.
    // This is the channel through which the carrying ceiling can FALL; without it
    // knowledge only ever raises it and no society can overshoot its land.
    static float natural_capital_of(const Province& province, const SubsistenceConfig& cfg,
                                    float soil_health = 1.0f) {
        const float soil = std::clamp(soil_health, 0.0f, 1.0f);
        return soil * (cfg.weight_agricultural_productivity * province.agricultural_productivity +
                       cfg.weight_arable_land * province.geography.arable_land_fraction) +
               cfg.weight_forest_forage * province.geography.forest_coverage +
               cfg.weight_fisheries * province.fisheries.current_stock;
    }

    // Food the province's natural capital yields when worked by `labor` people.
    // Rises with labour toward a ceiling (carrying capacity) set by natural
    // capital — diminishing returns, so fixed land caps the head count it feeds.
    // Inline for the same reason as natural_capital_of.
    static float subsistence_output(float natural_capital, float labor,
                                    const SubsistenceConfig& cfg) {
        if (natural_capital <= 0.0f || labor <= 0.0f)
            return 0.0f;
        const float ceiling = cfg.ceiling_per_capital_unit * natural_capital;
        // Diminishing returns on labour: output -> ceiling as labour grows. At
        // labor == labor_half_saturation, output is ~63% (1 - 1/e) of the ceiling.
        const float half = cfg.labor_half_saturation > 0.0f ? cfg.labor_half_saturation : 1.0f;
        const float saturation = 1.0f - std::exp(-labor / half);
        return ceiling * saturation;
    }

    // produced / needed for `population` people. 1.0 = exactly fed.
    static float surplus_ratio(float output, uint32_t population, const SubsistenceConfig& cfg);

    // Is the commons food path active in `regime`? (regime ∈ cfg.active_regimes)
    bool regime_active(std::string_view regime) const;

    // Episodic harvest-failure output multiplier (<= 1.0) for one province-year (M6a):
    // a bad harvest, probability and depth scaled by the world's `seasonality` dial.
    // 1.0 in a normal year. Pure given the RNG.
    static float harvest_failure_factor(float seasonality_dial, DeterministicRNG& rng,
                                        const SubsistenceConfig& cfg);

    // Chronic food multiplier (<= 1.0) from predators preying on herds/draft animals
    // (M6a coupling): scaled by the `predators` dial, strongest early and WANING as
    // accumulated knowledge clears them. 1.0 once cleared / on a predator-free world.
    static float predator_food_factor(float predators_dial, float knowledge_level,
                                      const SubsistenceConfig& cfg);

    // Chronic carrying-ceiling multiplier (<= 1.0) from a hostile/toxic atmosphere
    // (M6a chronic): scaled by the `atmosphere` dial. Planetary — never wanes.
    static float atmosphere_ceiling_factor(float atmosphere_dial, const SubsistenceConfig& cfg);

    // The product of every CHRONIC multiplier on the carrying ceiling: accumulated
    // technique (knowledge), climate reliability (seasonality relative to Earth),
    // era food technology, predator pressure on herds, and atmospheric hostility.
    // Episodic harvest failure is deliberately NOT included — that is a per-year
    // RNG draw, and world-gen has no year to draw for.
    //
    // Shared with entry materialization (core/world_gen/premarket_genesis.cpp) so a
    // fresh pre-modern start sizes its towns against the SAME ceiling the climb
    // would have produced. Genesis previously applied only the food-tech multiplier
    // and so overestimated the sustainable town by ~8-9% on a default Earth world
    // (and more on hazard worlds), founding workshops the first real harvest could
    // not feed. NOTE: execute_province deliberately keeps its own expanded form of
    // this product to preserve exact floating-point association for the golden
    // determinism dumps — if you change the law here, change it there too.
    static float chronic_ceiling_factors(float knowledge_level, float tech_food_mult,
                                         const WorldHazardSettings& hazards,
                                         const SubsistenceConfig& cfg) {
        const float K = std::max(0.0f, knowledge_level);
        const float knowledge_factor =
            1.0f + cfg.knowledge_productivity_max * K /
                       (K + std::max(1.0f, cfg.knowledge_productivity_halfsat));
        const float seasonality_factor =
            std::clamp(1.0f - cfg.seasonality_food_penalty *
                                  (hazards.seasonality - earth_hazard().seasonality),
                       0.3f, 1.3f);
        return knowledge_factor * seasonality_factor * tech_food_mult *
               predator_food_factor(hazards.predators, K, cfg) *
               atmosphere_ceiling_factor(hazards.atmosphere, cfg);
    }

    // Non-farming share the regime can sustain (rises along the pre-market arc:
    // subsistence <= barter < coinage < money < feudal < mercantile < industrial;
    // other regimes -> max_specialist_fraction).
    float specialist_ceiling(std::string_view regime) const;

    // How many of `residents` a surplus can free into Layer-2 specialists.
    // 0 when surplus <= 1; rises with surplus toward max_specialist_fraction.
    static uint32_t specialist_count(uint32_t residents, float surplus,
                                     const SubsistenceConfig& cfg);

    // Is `regime` a stratified (manorial) regime? (regime ∈ cfg.manorial_regimes)
    bool regime_manorial(std::string_view regime) const;

    // Per-resident proto-capital share of `total_proto`. In the egalitarian commons
    // (manorial=false) this is the even split. Under manorialism the LORDS take the
    // tithe on top of the even peasant base — conserved (summed over all residents ==
    // total_proto). Who is a lord is EMERGENT: the wealthiest residents (ranked by
    // capital, ties by id) — wealth buys the retinue and the hall that collect the
    // tithe, and the tithe compounds the wealth; a plundered dynasty can fall and a
    // richer upstart displace it. Pure/static for unit testing.
    static float proto_share_for(bool is_lord, uint32_t lords_count, uint32_t residents_count,
                                 float total_proto, bool manorial, const SubsistenceConfig& cfg);

    // How many lords a manorial province of `residents_count` heads supports
    // (manorial_lord_fraction, at least one). Pure/static. Inline for the same
    // reason as natural_capital_of.
    static uint32_t lord_count(uint32_t residents_count, const SubsistenceConfig& cfg) {
        if (residents_count == 0)
            return 0;
        uint32_t lords = static_cast<uint32_t>(
            std::lround(cfg.manorial_lord_fraction * static_cast<float>(residents_count)));
        if (lords < 1)
            lords = 1;
        if (lords > residents_count)
            lords = residents_count;
        return lords;
    }

   private:
    SubsistenceConfig cfg_;
    // True once the commons food path has published in an active regime, so the
    // one-time regime-exit reset (execute()) knows there is a stale value to
    // clear. Mirrors warfare's war_state_dirty_.
    bool commons_state_dirty_ = false;
};

}  // namespace econlife
