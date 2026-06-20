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
    // era 1 (subsistence) advances at knowledge_to_advance = 150 (builtin).
    WorldState w = make_world(/*era=*/1, /*scholars=*/2, /*knowledge=*/160.0f);
    DeltaBuffer d{};
    mod.execute(w, d);
    REQUIRE(d.technology_deltas.size() == 1);
    REQUIRE(d.technology_deltas[0].new_era.has_value());
    CHECK(*d.technology_deltas[0].new_era == 2);
}
