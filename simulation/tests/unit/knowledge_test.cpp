#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/knowledge/knowledge_module.h"

using namespace econlife;

namespace {
// A world at an annual tick whose POPULATION carries a learned stratum. Knowledge is
// produced by a share of the people the food surplus frees from the land
// (cohort_stats->specialist_fraction, published by subsistence), so the fixture sets a
// population and a freed share rather than a handful of tracked individuals.
// `n_tracked` adds tracked NPCs with a scholar occupation, used only to test that
// production does NOT depend on them and that a genius leap gets attributed to one.
WorldState make_world(uint8_t era, uint32_t population, float specialist_fraction,
                      float knowledge_level, uint32_t n_tracked = 0) {
    WorldState w{};
    w.current_tick = 365;  // annual cadence
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.occupation_catalog.load_builtin_default();
    w.technology.current_era = era;
    w.technology.knowledge_level = knowledge_level;

    Province p{};
    p.id = 0;
    p.region_id = 0;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = population;
    p.cohort_stats->specialist_fraction = specialist_fraction;
    p.cohort_stats->subsistence_surplus_ratio = 1.2f;  // fed, with a margin
    w.provinces.push_back(std::move(p));

    const OccupationDefinition* scholar = w.occupation_catalog.find("scholar");
    for (uint32_t i = 0; i < n_tracked; ++i) {
        NPC npc{};
        npc.id = 1 + i;
        npc.occupation = scholar ? scholar->index : 0;
        w.significant_npcs.push_back(npc);
    }
    return w;
}

double produced_knowledge(KnowledgeModule& mod, const WorldState& w) {
    DeltaBuffer d{};
    mod.execute(w, d);
    double total = 0.0;
    for (const auto& td : d.technology_deltas)
        if (td.knowledge_delta.has_value())
            total += static_cast<double>(*td.knowledge_delta);
    return total;
}
}  // namespace

TEST_CASE("knowledge: regime gate is dawn-only", "[knowledge][tier1]") {
    KnowledgeModule mod;
    CHECK(mod.regime_active("subsistence"));
    CHECK(mod.regime_active("barter"));
    CHECK_FALSE(mod.regime_active("modern"));
}


// ===========================================================================
// WHO ADVANCES A SOCIETY: a share of the population, gated on surplus and/or
// pressure — plus rare individuals who make leaps.
//
// These pin the design decision and the defect it replaced. Production used to
// be summed over a fixed ~200-person sample of tracked NPCs, whose members died
// with nothing to replace them: the living corps drained to zero within a
// century and the climb died with it, having only ever "worked" because dead
// NPCs kept producing. A stratum of the LIVING POPULATION cannot die out while
// the population lives, which is the property these tests enforce.
// ===========================================================================

TEST_CASE("knowledge: production scales with the learned population",
          "[knowledge][tier1][population]") {
    KnowledgeModule mod;
    // Same freed share, more people -> strictly more knowledge per year. This is why
    // history accelerates as societies grow.
    double previous = -1.0;
    for (uint32_t pop : {1000u, 10000u, 100000u}) {
        WorldState w = make_world(/*era=*/1, pop, /*specialist_fraction=*/0.12f, 0.0f);
        const double produced = produced_knowledge(mod, w);
        CHECK(produced > previous);
        previous = produced;
    }
}

TEST_CASE("knowledge: a bigger freed stratum advances faster",
          "[knowledge][tier1][population]") {
    KnowledgeModule mod;
    // The food surplus is what frees people from the land; a society that can spare
    // more of them advances faster on the same population.
    WorldState lean = make_world(/*era=*/1, 50000, /*specialist_fraction=*/0.02f, 0.0f);
    WorldState rich = make_world(/*era=*/1, 50000, /*specialist_fraction=*/0.20f, 0.0f);
    CHECK(produced_knowledge(mod, rich) > produced_knowledge(mod, lean));
}

TEST_CASE("knowledge: with no freed stratum, pressure alone still advances a society",
          "[knowledge][tier1][population]") {
    // "Food surplus AND/OR pressure": a society with nobody to spare still
    // innovates under duress (Boserup) — the diffuse population term — just far
    // more slowly than one with a learned class.
    KnowledgeModule mod;
    WorldState pressed = make_world(/*era=*/1, 50000, /*specialist_fraction=*/0.0f, 0.0f);
    const double under_pressure = produced_knowledge(mod, pressed);
    CHECK(under_pressure > 0.0);

    WorldState with_class = make_world(/*era=*/1, 50000, /*specialist_fraction=*/0.12f, 0.0f);
    CHECK(produced_knowledge(mod, with_class) > under_pressure);
}

TEST_CASE("knowledge: production does not depend on the tracked NPC layer",
          "[knowledge][tier1][population]") {
    // The regression this design replaces: production must not be a function of a
    // fixed sample of individuals, or it dies as they die. Identical populations
    // produce identically whether the tracked layer is empty, alive, or all dead.
    KnowledgeModule mod;
    WorldState none = make_world(/*era=*/1, 50000, 0.12f, 0.0f, /*n_tracked=*/0);
    WorldState alive = make_world(/*era=*/1, 50000, 0.12f, 0.0f, /*n_tracked=*/8);
    WorldState dead = make_world(/*era=*/1, 50000, 0.12f, 0.0f, /*n_tracked=*/8);
    for (auto& npc : dead.significant_npcs)
        npc.status = NPCStatus::dead;

    const double a = produced_knowledge(mod, none);
    const double b = produced_knowledge(mod, alive);
    const double c = produced_knowledge(mod, dead);
    CHECK(a > 0.0);
    CHECK(b == a);
    CHECK(c == a);
}

TEST_CASE("knowledge: era gating raises what a knowledge-keeper produces",
          "[knowledge][tier1][population]") {
    // The catalog gates the livelihood: elder (oral tradition) at the dawn, scribe
    // with writing, scholar with formal scholarship — each a stronger source. Same
    // people, better institutions, more knowledge.
    KnowledgeModule mod;
    const double dawn = produced_knowledge(mod, make_world(/*era=*/1, 50000, 0.12f, 0.0f));
    const double writing = produced_knowledge(mod, make_world(/*era=*/2, 50000, 0.12f, 0.0f));
    const double scholarship = produced_knowledge(mod, make_world(/*era=*/4, 50000, 0.12f, 0.0f));
    CHECK(writing > dawn);
    CHECK(scholarship > writing);
}

TEST_CASE("knowledge: a genius leap is discrete, attributed, and deterministic",
          "[knowledge][tier1][genius]") {
    // Rare minds make leaps rather than increments. With a large learned population
    // the per-year arrival probability is high enough to observe, and the leap is
    // worth genius_leap_years of ordinary output — so a leap year produces far more
    // than a quiet one, and the same seed and year always give the same outcome.
    KnowledgeModule mod;
    double quiet = 0.0;
    double leap = 0.0;
    uint32_t leap_year = 0;
    for (uint32_t year = 1; year <= 40 && leap == 0.0; ++year) {
        WorldState w = make_world(/*era=*/4, 2000000, 0.20f, 0.0f, /*n_tracked=*/4);
        w.current_tick = year * 365u;
        DeltaBuffer d{};
        mod.execute(w, d);
        double produced = 0.0;
        for (const auto& td : d.technology_deltas)
            if (td.knowledge_delta.has_value())
                produced += static_cast<double>(*td.knowledge_delta);
        // A leap year also credits a named individual.
        bool attributed = false;
        for (const auto& nd : d.npc_deltas)
            if (nd.new_memory_entry.has_value())
                attributed = true;
        if (attributed) {
            leap = produced;
            leap_year = year;
        } else {
            quiet = produced;
        }
    }
    REQUIRE(quiet > 0.0);
    REQUIRE(leap > 0.0);       // a leap was observed within 40 years at this scale
    CHECK(leap > quiet * 5.0);  // discrete jump, not an increment

    // Deterministic: the same seed and year reproduce it exactly.
    WorldState again = make_world(/*era=*/4, 2000000, 0.20f, 0.0f, /*n_tracked=*/4);
    again.current_tick = leap_year * 365u;
    DeltaBuffer d2{};
    mod.execute(again, d2);
    double repeat = 0.0;
    for (const auto& td : d2.technology_deltas)
        if (td.knowledge_delta.has_value())
            repeat += static_cast<double>(*td.knowledge_delta);
    CHECK(repeat == leap);
}

TEST_CASE("knowledge: a leap is credited to a LIVING person", "[knowledge][tier1][genius]") {
    // The leap belongs to a named individual. If the tracked layer holds only the
    // dead, the discovery still happens (it comes from the learned population) but
    // no corpse is credited with it.
    KnowledgeModule mod;
    bool credited_living = false;
    bool credited_dead = false;
    for (uint32_t year = 1; year <= 40; ++year) {
        WorldState w = make_world(/*era=*/4, 2000000, 0.20f, 0.0f, /*n_tracked=*/4);
        w.current_tick = year * 365u;
        w.significant_npcs[0].status = NPCStatus::dead;  // id 1 is gone
        DeltaBuffer d{};
        mod.execute(w, d);
        for (const auto& nd : d.npc_deltas) {
            if (!nd.new_memory_entry.has_value())
                continue;
            if (nd.npc_id == 1u)
                credited_dead = true;
            else
                credited_living = true;
        }
    }
    CHECK(credited_living);
    CHECK_FALSE(credited_dead);
}
