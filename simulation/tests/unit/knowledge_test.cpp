#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

#include "core/world_gen/era_catalog.h"
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

TEST_CASE("knowledge: a genius is one person, however large the society",
          "[knowledge][tier1][genius]") {
    // A leap is bounded by what a MIND can do — genius_equivalent_workers ordinary
    // keepers for genius_leap_years at this era's per-worker output — so its absolute
    // size does NOT depend on how many people the civilisation has. That makes a great
    // mind era-defining in a small scholarly community and a modest share of a vast
    // one, which is the historically right shape and stops leaps from dominating the
    // late climb (an earlier form multiplied the society's TOTAL output, so one mind
    // supplied thirty years of all its knowledge work).
    KnowledgeConfig quiet_cfg{};
    quiet_cfg.genius_rate_per_worker_year = 0.0f;  // no leaps: ordinary output only
    KnowledgeConfig leap_cfg{};
    leap_cfg.genius_rate_per_worker_year = 1.0f;  // a leap every year, for measurement
    KnowledgeModule ordinary(quiet_cfg);
    KnowledgeModule with_genius(leap_cfg);

    WorldState small = make_world(/*era=*/4, 20000, 0.15f, 0.0f, /*n_tracked=*/4);
    WorldState vast = make_world(/*era=*/4, 2000000, 0.15f, 0.0f, /*n_tracked=*/4);

    const double small_ordinary = produced_knowledge(ordinary, small);
    const double vast_ordinary = produced_knowledge(ordinary, vast);
    const double small_total = produced_knowledge(with_genius, small);
    const double vast_total = produced_knowledge(with_genius, vast);

    const double small_leap = small_total - small_ordinary;
    const double vast_leap = vast_total - vast_ordinary;
    REQUIRE(small_leap > 0.0);
    REQUIRE(vast_leap > 0.0);
    // The same person's contribution in both societies.
    CHECK_THAT(small_leap, Catch::Matchers::WithinRel(vast_leap, 1e-4));
    // Era-defining among few peers; a modest share among many.
    CHECK(small_leap > small_ordinary * 5.0);
    CHECK(vast_leap < vast_ordinary);
}

TEST_CASE("knowledge: a leap is deterministic and credited to a LIVING person",
          "[knowledge][tier1][genius]") {
    KnowledgeConfig cfg{};
    cfg.genius_rate_per_worker_year = 1.0f;  // certain, so the draw is observable
    KnowledgeModule mod(cfg);

    WorldState w = make_world(/*era=*/4, 50000, 0.15f, 0.0f, /*n_tracked=*/4);
    w.significant_npcs[0].status = NPCStatus::dead;  // id 1 is gone

    DeltaBuffer d{};
    mod.execute(w, d);
    uint32_t credited = 0;
    for (const auto& nd : d.npc_deltas)
        if (nd.new_memory_entry.has_value())
            credited = nd.npc_id;
    REQUIRE(credited != 0);       // a named person is on the record
    CHECK(credited != 1u);        // never a corpse

    // Same seed and year reproduce the same discovery exactly.
    DeltaBuffer d2{};
    KnowledgeModule again(cfg);
    again.execute(w, d2);
    double first = 0.0;
    double second = 0.0;
    for (const auto& td : d.technology_deltas)
        if (td.knowledge_delta.has_value())
            first += static_cast<double>(*td.knowledge_delta);
    for (const auto& td : d2.technology_deltas)
        if (td.knowledge_delta.has_value())
            second += static_cast<double>(*td.knowledge_delta);
    CHECK(first == second);
}

// ===========================================================================
// KNOWING AND BUILDING ARE TWO DIFFERENT THINGS.
//
// A society advances only when it both knows enough AND has built enough to use
// what it knows. Knowledge is information: it can spike, and historically does.
// Productive capital is matter and labour — accumulated out of a real food
// surplus at a physical rate, and it wears out. This is why enormous modern
// data output has not produced flying cars, and it is the natural limiter on
// advancement speed.
// ===========================================================================

// A catalog whose era 1 carries BOTH gates, written by the test so the mechanism is
// proven independently of whatever the shipped eras.csv is calibrated to. The shipped
// capital thresholds are currently 0 (uncalibrated — a per-head gate set from the
// measured curve stalled every world, see the session log), and these tests must keep
// working when they are set.
namespace {
struct GatedEras {
    std::filesystem::path dir;
    float knowledge_gate = 4000.0f;
    float capital_gate = 250.0f;

    GatedEras() {
        dir = std::filesystem::temp_directory_path() /
              ("econlife_eras_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        std::ofstream f(dir / "eras.csv");
        f << "era_index,era_key,display_name,start_year,economic_regime,is_default_entry,"
             "v1_in_scope,knowledge_to_advance,capital_to_advance\n";
        f << "1,neolithic,Neolithic,-10000,subsistence,0,1," << knowledge_gate << ","
          << capital_gate << "\n";
        f << "2,bronze_age,Bronze Age,-3300,barter,0,1,0,0\n";
    }
    ~GatedEras() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
}  // namespace

TEST_CASE("knowledge: knowing is not enough — an era needs the capacity to use it",
          "[knowledge][tier1][capital]") {
    KnowledgeModule mod;
    GatedEras eras;

    auto advanced = [&](float knowledge, float capital_per_head) {
        WorldState w = make_world(/*era=*/1, 50000, 0.12f, knowledge);
        REQUIRE(w.era_catalog.load_from_directory(eras.dir.string()));
        w.provinces[0].cohort_stats->productive_capital = capital_per_head * 50000.0f;
        DeltaBuffer d{};
        mod.execute(w, d);
        for (const auto& td : d.technology_deltas)
            if (td.new_era.has_value())
                return true;
        return false;
    };

    // All the knowledge in the world and nothing built with it: no advance. This is
    // the flying-car case — the data exists, the capacity does not.
    CHECK_FALSE(advanced(eras.knowledge_gate * 100.0f, 0.0f));
    // Plenty built but the society does not yet know what to do with it: no advance.
    CHECK_FALSE(advanced(0.0f, eras.capital_gate * 2.0f));
    // Both: the era turns.
    CHECK(advanced(eras.knowledge_gate * 1.5f, eras.capital_gate * 2.0f));
}

TEST_CASE("knowledge: built capacity is counted per head, not as a heap",
          "[knowledge][tier1][capital]") {
    // A bigger society needs proportionally more built, so population growth alone
    // cannot buy an era: the same absolute stock that carries a small society is
    // spread too thin by a large one.
    KnowledgeModule mod;
    GatedEras eras;

    auto advanced_with = [&](uint32_t population, float total_capital) {
        WorldState w = make_world(/*era=*/1, population, 0.12f, eras.knowledge_gate * 1.5f);
        REQUIRE(w.era_catalog.load_from_directory(eras.dir.string()));
        w.provinces[0].cohort_stats->productive_capital = total_capital;
        DeltaBuffer d{};
        mod.execute(w, d);
        for (const auto& td : d.technology_deltas)
            if (td.new_era.has_value())
                return true;
        return false;
    };

    const float stock = eras.capital_gate * 20000.0f * 1.5f;
    CHECK(advanced_with(20000, stock));
    CHECK_FALSE(advanced_with(400000, stock));
}
