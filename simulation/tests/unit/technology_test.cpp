// Smoke tests for TechnologyModule. The module runs sequentially and is
// responsible for era transitions, maturation advance, and domain-knowledge
// decay. The catalog-loading path is tested separately in
// technology_catalog_test.cpp; here we exercise execute().

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/technology/technology_module.h"
#include "modules/technology/technology_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {

WorldState make_world_with_tech(uint8_t era, uint32_t current_tick) {
    WorldState w{};
    w.current_tick = current_tick;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;
    w.era_catalog.load_builtin_default();  // era logic reads the data-driven timeline
    w.technology.current_era = era;
    w.technology.era_started_tick = 0;

    Province p{};
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.id = 0;
    w.provinces.push_back(p);
    rebuild_npc_indices(w);
    return w;
}

}  // namespace

TEST_CASE("Technology: domain_knowledge decay emits TechnologyDelta with negative delta",
          "[technology][tier1]") {
    auto state = make_world_with_tech(/*era=*/5, /*tick=*/1);
    // Seed domain_knowledge high enough that the per-tick decay
    // (current * knowledge_decay_rate; default 0.0001) clears the
    // skip-negligible-decay threshold (|decay| > 0.0001, strictly).
    // 1.5 * 0.0001 = 0.00015 > 0.0001 ✓.
    state.technology.domain_knowledge[0] = 1.5f;
    state.technology.domain_knowledge[1] = 1.5f;

    TechnologyModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    // At least one TechnologyDelta with a negative domain_knowledge_delta.
    bool found_negative_decay = false;
    for (const auto& td : delta.technology_deltas) {
        if (td.domain_index.has_value() && td.domain_knowledge_delta.has_value() &&
            *td.domain_knowledge_delta < 0.0f) {
            found_negative_decay = true;
            break;
        }
    }
    REQUIRE(found_negative_decay);
}

TEST_CASE("Technology: zero domain_knowledge produces no decay delta", "[technology][tier1]") {
    auto state = make_world_with_tech(/*era=*/5, /*tick=*/1);
    // All domain_knowledge entries default to 0.0 — module should emit no
    // decay deltas for them.

    TechnologyModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    for (const auto& td : delta.technology_deltas) {
        if (td.domain_index.has_value() && td.domain_knowledge_delta.has_value()) {
            // The only non-zero deltas allowed at this point are era-related
            // (new_era replacements), not domain decay. Decay deltas for
            // domains we never seeded must not appear.
            INFO("Unexpected decay delta for domain_index " << static_cast<int>(*td.domain_index));
            REQUIRE_FALSE(td.domain_knowledge_delta.value() < 0.0f);
        }
    }
}

TEST_CASE("Technology: module name and dependencies match spec", "[technology][tier1]") {
    TechnologyModule module;
    REQUIRE(module.name() == "technology");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE_FALSE(module.is_province_parallel());

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "calendar");
    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "production");
}

// ===========================================================================
// Era advancement — the calendar gate is DATA-DRIVEN and the two advancement
// paths are cleanly split (F1 repair).
//
// These pin the bugs found in review: a hardcoded year table that was never
// renumbered when the seven pre-modern eras were inserted (it pinned era 9 to
// year 2100 and dead-ended the timeline at era 10), and an unsigned base_year
// that wrapped for every BCE start and made the gate vacuously true.
// ===========================================================================

TEST_CASE("Technology: modern era advances at the catalog's scheduled year",
          "[technology][tier1][era]") {
    // Era 8 (turn_of_millennium, start 2000) -> era 9 (disruption, start 2007).
    // The scheduled year is the trigger; no tech maturation is needed.
    WorldState w = make_world_with_tech(8, 0);
    w.technology.base_year = 2000;

    TechnologyModule mod;
    DeltaBuffer before{};
    mod.execute(w, before);
    // Year 2000: the next era is not due yet.
    bool advanced_early = false;
    for (const auto& td : before.technology_deltas)
        if (td.new_era.has_value())
            advanced_early = true;
    CHECK_FALSE(advanced_early);

    // Year 2007 (7 years of daily ticks): the era is due and opens.
    w.current_tick = 7u * 365u;
    DeltaBuffer after{};
    mod.execute(w, after);
    bool advanced = false;
    for (const auto& td : after.technology_deltas)
        if (td.new_era.has_value() && *td.new_era == 9)
            advanced = true;
    CHECK(advanced);
}

TEST_CASE("Technology: knowledge-gated pre-modern eras are not advanced by the calendar",
          "[technology][tier1][era]") {
    // Era 1 (neolithic) has knowledge_to_advance > 0, so the pre-modern climb is
    // knowledge_module's to pace — each world advances on what it earned. If the
    // calendar could advance it, every world would be dragged through the
    // pre-modern eras on a fixed historical schedule and the World-Class
    // spectrum (garden stalls in the Bronze Age, fertile races ahead) would die.
    WorldState w = make_world_with_tech(1, 0);
    w.technology.base_year = -10000;  // the Neolithic opens at 10000 BCE

    // Run well past era 2's scheduled year (-3300): 7000 years of ticks.
    w.current_tick = 7000u * 365u;
    TechnologyModule mod;
    DeltaBuffer delta{};
    mod.execute(w, delta);

    bool advanced = false;
    for (const auto& td : delta.technology_deltas)
        if (td.new_era.has_value())
            advanced = true;
    CHECK_FALSE(advanced);
}

TEST_CASE("Technology: a BCE start year does not wrap the calendar",
          "[technology][tier1][era]") {
    // base_year is signed: an unsigned field turned -10000 into ~4.29e9, which
    // satisfied every calendar comparison from tick 0 onward.
    GlobalTechnologyState gts;
    gts.base_year = -10000;
    CHECK(gts.base_year < 0);

    WorldState w = make_world_with_tech(1, 0);
    w.technology.base_year = -10000;
    // A world at the dawn must report a BCE calendar year, not a 4-billion-year
    // one. Exercised through the module's public path: era 2 is scheduled at
    // -3300, so at tick 0 the dawn world is nowhere near any modern threshold.
    TechnologyModule mod;
    DeltaBuffer delta{};
    mod.execute(w, delta);
    for (const auto& td : delta.technology_deltas)
        CHECK_FALSE(td.new_era.has_value());
}

// ===========================================================================
// R&D funding comes out of ONE purse (F2c).
//
// Affordability was measured against the same pre-tick cash snapshot for every
// project, so a firm running several maturation projects charged each one its
// full cash and went negative — apply_business_deltas sums the deltas and has no
// zero floor, so R&D was funded from money that never existed.
// ===========================================================================
TEST_CASE("Technology: several maturation projects cannot overdraw one firm's cash",
          "[technology][tier1][rd]") {
    WorldState w = make_world_with_tech(8, 1);

    NPCBusiness biz{};
    biz.id = 7;
    biz.province_id = 0;
    biz.cash = 100.0f;  // far less than three projects would ideally spend
    biz.actor_tech_state.effective_tech_tier = 1.0f;
    for (const char* key : {"node_a", "node_b", "node_c"}) {
        TechHolding h{};
        h.node_key = key;
        h.holder_id = biz.id;
        h.maturation_level = 0.10f;
        h.maturation_ceiling = 1.0f;
        biz.actor_tech_state.holdings[key] = h;
    }
    w.npc_businesses.push_back(std::move(biz));
    rebuild_npc_indices(w);

    for (const char* key : {"node_a", "node_b", "node_c"}) {
        MaturationProject p{};
        p.node_key = key;
        p.business_id = 7;
        p.researchers_assigned = 5;
        w.technology.active_maturation_projects.push_back(p);
    }

    TechnologyModule mod;
    DeltaBuffer d{};
    mod.execute(w, d);

    float charged = 0.0f;
    for (const auto& bd : d.business_deltas)
        if (bd.business_id == 7 && bd.cash_delta.has_value())
            charged += -*bd.cash_delta;

    // The firm may spend everything it has, but never more.
    CHECK(charged <= 100.0f + 0.01f);
    CHECK(charged > 0.0f);

    // And after application the purse is not negative.
    apply_deltas(w, d);
    CHECK(w.npc_businesses[0].cash >= -0.01f);
}
