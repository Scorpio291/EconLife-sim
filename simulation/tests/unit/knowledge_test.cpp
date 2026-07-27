#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/knowledge/knowledge_module.h"

using namespace econlife;

namespace {
// A world at an annual tick with `n_scholars` scholar-occupation NPCs at the dawn.
WorldState make_world(uint8_t era, uint32_t n_scholars, float knowledge_level) {
    WorldState w{};
    w.current_tick = 365;  // annual cadence
    w.era_catalog.load_builtin_default();
    w.occupation_catalog.load_builtin_default();
    w.technology.current_era = era;
    w.technology.knowledge_level = knowledge_level;
    const OccupationDefinition* scholar = w.occupation_catalog.find("scholar");
    for (uint32_t i = 0; i < n_scholars; ++i) {
        NPC npc{};
        npc.id = 1 + i;
        npc.occupation = scholar ? scholar->index : 0;
        w.significant_npcs.push_back(npc);
    }
    return w;
}
}  // namespace

TEST_CASE("knowledge: regime gate is dawn-only", "[knowledge][tier1]") {
    KnowledgeModule mod;
    CHECK(mod.regime_active("subsistence"));
    CHECK(mod.regime_active("barter"));
    CHECK_FALSE(mod.regime_active("modern"));
}

TEST_CASE("knowledge: scholars produce knowledge at the dawn, none in market eras",
          "[knowledge][tier1]") {
    KnowledgeModule mod;

    SECTION("dawn with scholars accrues knowledge") {
        WorldState w = make_world(/*era=*/1, /*scholars=*/4, /*knowledge=*/0.0f);
        DeltaBuffer d{};
        mod.execute(w, d);
        REQUIRE(d.technology_deltas.size() == 1);
        REQUIRE(d.technology_deltas[0].knowledge_delta.has_value());
        CHECK(*d.technology_deltas[0].knowledge_delta > 0.0f);
    }

    SECTION("modern era is inert (no scholars, knowledge module off)") {
        WorldState w = make_world(/*era=*/8, /*scholars=*/4, /*knowledge=*/0.0f);
        DeltaBuffer d{};
        mod.execute(w, d);
        CHECK(d.technology_deltas.empty());
    }

    SECTION("no scholars -> no knowledge, no progress (contingent)") {
        WorldState w = make_world(/*era=*/1, /*scholars=*/0, /*knowledge=*/0.0f);
        DeltaBuffer d{};
        mod.execute(w, d);
        // No production and nothing to decay -> nothing emitted.
        CHECK(d.technology_deltas.empty());
    }
}

TEST_CASE("knowledge: accumulated knowledge advances the era", "[knowledge][tier1]") {
    KnowledgeModule mod;
    // era 1 (subsistence) advances at its builtin knowledge_to_advance (the
    // historically-grounded era thresholds — see era_catalog).
    WorldState w = make_world(/*era=*/1, /*scholars=*/2, /*knowledge=*/4300.0f);
    DeltaBuffer d{};
    mod.execute(w, d);
    REQUIRE(d.technology_deltas.size() == 1);
    REQUIRE(d.technology_deltas[0].new_era.has_value());
    CHECK(*d.technology_deltas[0].new_era == 2);
}

// ===========================================================================
// Liveness: who counts as a working scholar.
//
// These guard a regression that all three test gates missed. A filter of
// `status != NPCStatus::active` looks right but excludes NPCStatus::waiting —
// documented in npc.h as "Active but chose inaction this tick" — which is where
// most of a dawn population sits. Knowledge production fell to the diffuse
// population term alone (~0.01/yr against a first-era threshold of 3,830), so
// every world in the spectrum sat at era 1 for 13,000 years: a stall with no
// cause in the world itself. Nothing outside the hidden observe runs noticed.
// ===========================================================================

TEST_CASE("knowledge: an idle (waiting) scholar still does a year's work",
          "[knowledge][tier1][liveness]") {
    KnowledgeModule mod;

    WorldState active_world = make_world(/*era=*/4, /*n_scholars=*/8, /*knowledge=*/0.0f);
    DeltaBuffer active_delta{};
    mod.execute(active_world, active_delta);
    REQUIRE(active_delta.technology_deltas.size() == 1);
    REQUIRE(active_delta.technology_deltas[0].knowledge_delta.has_value());
    const float active_production = *active_delta.technology_deltas[0].knowledge_delta;
    CHECK(active_production > 0.0f);

    // Same corps, every one of them idle this tick. The sum is an ANNUAL
    // aggregate, so the year's work is identical.
    WorldState waiting_world = make_world(/*era=*/4, /*n_scholars=*/8, /*knowledge=*/0.0f);
    for (auto& npc : waiting_world.significant_npcs)
        npc.status = NPCStatus::waiting;
    DeltaBuffer waiting_delta{};
    mod.execute(waiting_world, waiting_delta);
    REQUIRE(waiting_delta.technology_deltas.size() == 1);
    REQUIRE(waiting_delta.technology_deltas[0].knowledge_delta.has_value());
    CHECK(*waiting_delta.technology_deltas[0].knowledge_delta == active_production);
}

TEST_CASE("knowledge: the dead, fled and imprisoned produce nothing",
          "[knowledge][tier1][liveness]") {
    KnowledgeModule mod;

    for (NPCStatus gone : {NPCStatus::dead, NPCStatus::fled, NPCStatus::imprisoned}) {
        WorldState w = make_world(/*era=*/4, /*n_scholars=*/8, /*knowledge=*/0.0f);
        for (auto& npc : w.significant_npcs)
            npc.status = gone;
        DeltaBuffer d{};
        mod.execute(w, d);
        // No provinces in this fixture, so the population term is zero: with no
        // working scholars there is nothing to publish at all.
        bool produced = false;
        for (const auto& td : d.technology_deltas)
            if (td.knowledge_delta.has_value() && *td.knowledge_delta > 0.0f)
                produced = true;
        CHECK_FALSE(produced);
    }
}

TEST_CASE("knowledge: the scholar corps is what drives the climb, not a constant",
          "[knowledge][tier1][liveness]") {
    // The mechanical link the era clock depends on: more knowledge-keepers must
    // mean strictly more knowledge per year. If this ever holds flat, the climb is
    // being paced by something other than the people doing the work.
    KnowledgeModule mod;
    float previous = -1.0f;
    for (uint32_t corps : {0u, 1u, 4u, 16u}) {
        WorldState w = make_world(/*era=*/4, corps, /*knowledge=*/0.0f);
        DeltaBuffer d{};
        mod.execute(w, d);
        float produced = 0.0f;
        for (const auto& td : d.technology_deltas)
            if (td.knowledge_delta.has_value())
                produced += *td.knowledge_delta;
        CHECK(produced > previous);
        previous = produced;
    }
}
