#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

#include "core/world_gen/era_catalog.h"
#include "core/world_state/apply_deltas.h"
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
    // R6: knowledge is held per PROVINCE now; the world's figure is the frontier over
    // them. A single-province fixture is the frontier, so the two agree here.
    p.cohort_stats->knowledge_level = knowledge_level;
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

// What the PROVINCES gained this year. R6 made knowledge a per-province stock and the
// world's figure the maximum over them, so the technology delta is now a step toward the
// frontier rather than the production itself — summing the province deltas is what these
// tests have always meant by "produced".
double produced_knowledge(KnowledgeModule& mod, const WorldState& w) {
    DeltaBuffer d{};
    mod.execute(w, d);
    double total = 0.0;
    for (const auto& rd : d.region_deltas)
        if (rd.province_knowledge_delta.has_value())
            total += static_cast<double>(*rd.province_knowledge_delta);
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

// ===========================================================================
// RISE AND FALL.
//
// Civilisations build grand works and then perish: Rome's aqueducts outlived the
// engineers who could maintain them, and the Maya cities outlived the surplus
// that fed their scribes. Advancement is a sawtooth with a ratchet, not a ramp.
// A society holds only what its learned stratum and institutions can carry, so
// when population or surplus collapses it forgets — and can lose the era.
// ===========================================================================

TEST_CASE("knowledge: a society forgets what it can no longer carry",
          "[knowledge][tier1][collapse]") {
    KnowledgeModule mod;

    // A large learned stratum sustains what it holds: nothing is forgotten, and the
    // society still gains.
    WorldState healthy = make_world(/*era=*/1, 200000, 0.15f, /*knowledge=*/2000.0f);
    CHECK(produced_knowledge(mod, healthy) > 0.0);

    // The same knowledge, after a collapse: the people are gone, so the technique
    // goes with them. Net change is NEGATIVE — this is a dark age, not a plateau.
    WorldState collapsed = make_world(/*era=*/1, 2000, 0.02f, /*knowledge=*/2000.0f);
    CHECK(produced_knowledge(mod, collapsed) < 0.0);
}

TEST_CASE("knowledge: writing is the ratchet — literate societies forget less",
          "[knowledge][tier1][collapse]") {
    // Identical collapse, different institutions. Era 1 carries knowledge orally
    // (elders); by era 4 the same people are scholars with records. The literate
    // society keeps far more of what it knew, which is what lets each cycle of rise
    // and fall start higher than the last.
    KnowledgeModule mod;
    WorldState oral = make_world(/*era=*/1, 5000, 0.05f, /*knowledge=*/20000.0f);
    WorldState literate = make_world(/*era=*/4, 5000, 0.05f, /*knowledge=*/20000.0f);
    const double oral_change = produced_knowledge(mod, oral);
    const double literate_change = produced_knowledge(mod, literate);
    CHECK(oral_change < 0.0);                 // the oral society is losing it
    CHECK(literate_change > oral_change);     // the literate one loses less (or gains)
}

TEST_CASE("knowledge: an era is LOST when the society can no longer carry it",
          "[knowledge][tier1][collapse]") {
    // The fall. A society sitting in era 2 whose knowledge has collapsed far below
    // what era 1 demanded to get there drops back: the works stand, but nobody can
    // build or maintain them any more.
    KnowledgeModule mod;
    EraCatalog cat;
    cat.load_builtin_default();
    const float entry_threshold = cat.by_index(1)->knowledge_to_advance;  // to enter era 2

    auto era_change = [&](float knowledge) {
        WorldState w = make_world(/*era=*/2, 5000, 0.02f, knowledge);
        DeltaBuffer d{};
        mod.execute(w, d);
        for (const auto& td : d.technology_deltas)
            if (td.new_era.has_value())
                return static_cast<int>(*td.new_era);
        return 2;
    };

    // Comfortably above what it took to get here: the era holds.
    CHECK(era_change(entry_threshold) == 2);
    // Collapsed well below it: the era is lost.
    CHECK(era_change(entry_threshold * 0.5f) == 1);
}

TEST_CASE("knowledge: a lost era can be climbed again (the ratchet)",
          "[knowledge][tier1][collapse]") {
    // Falling back is not the end of history. A society that recovers its population
    // and surplus rebuilds its learned stratum, accumulates again, and re-enters the
    // era it lost — which is what "slowly creeping forward" through repeated rise and
    // fall actually requires.
    KnowledgeModule mod;
    EraCatalog cat;
    cat.load_builtin_default();
    const float threshold = cat.by_index(1)->knowledge_to_advance;

    // Recovered population, knowledge back above the threshold: it advances again.
    WorldState recovered = make_world(/*era=*/1, 200000, 0.15f, threshold * 1.1f);
    DeltaBuffer d{};
    mod.execute(recovered, d);
    bool advanced = false;
    for (const auto& td : d.technology_deltas)
        if (td.new_era.has_value() && *td.new_era == 2)
            advanced = true;
    CHECK(advanced);
}

// ===========================================================================
// WRITTEN RECORDS — the ratchet across collapses.
//
// Tacit knowledge dies with the learned stratum; records do not. Historically it
// is artifacts that carry a civilisation across its own collapse: monastic
// copying after Rome, the Graeco-Arabic translations, clay tablets outlasting
// Sumer. Without this the simulation is a limit cycle — measured, every
// civilisation rebuilt to the same height and fell the same way, because losing
// its scribes made it forget like a culture that never had writing.
// ===========================================================================

TEST_CASE("knowledge: a literate society writes its knowledge down",
          "[knowledge][tier1][records]") {
    // Era 4 has scholars, so the corpus grows toward what the society knows.
    KnowledgeModule mod;
    WorldState w = make_world(/*era=*/4, 200000, 0.15f, /*knowledge=*/5000.0f);
    DeltaBuffer d{};
    mod.execute(w, d);

    float recorded = 0.0f;
    for (const auto& rd : d.region_deltas)
        if (rd.codified_knowledge_delta.has_value())
            recorded += *rd.codified_knowledge_delta;
    CHECK(recorded > 0.0f);
}

TEST_CASE("knowledge: an oral culture cannot write, and loses inherited records",
          "[knowledge][tier1][records]") {
    // Era 1 keeps an oral tradition (elders). A society at the dawn holding
    // inherited records has nobody able to recopy them, so the corpus decays.
    KnowledgeModule mod;
    WorldState w = make_world(/*era=*/1, 200000, 0.15f, /*knowledge=*/5000.0f);
    w.provinces[0].cohort_stats->codified_knowledge = 1000.0f;
    DeltaBuffer d{};
    mod.execute(w, d);

    float change = 0.0f;
    for (const auto& rd : d.region_deltas)
        if (rd.codified_knowledge_delta.has_value())
            change += *rd.codified_knowledge_delta;
    CHECK(change < 0.0f);
}

TEST_CASE("knowledge: records are a FLOOR under forgetting — the ratchet",
          "[knowledge][tier1][records]") {
    // The same collapsed society, with and without a written corpus. Without
    // records it forgets catastrophically; with them the books survive the
    // scholars and the loss is arrested.
    KnowledgeModule mod;

    WorldState oral = make_world(/*era=*/4, 2000, 0.02f, /*knowledge=*/20000.0f);
    const double without_records = produced_knowledge(mod, oral);

    WorldState archived = make_world(/*era=*/4, 2000, 0.02f, /*knowledge=*/20000.0f);
    archived.provinces[0].cohort_stats->codified_knowledge = 20000.0f;
    const double with_records = produced_knowledge(mod, archived);

    CHECK(without_records < 0.0);          // a dark age
    CHECK(with_records > without_records);  // the corpus arrests it
    // With a corpus covering everything the society knew, nothing is forgotten.
    CHECK(with_records >= 0.0);
}

TEST_CASE("knowledge: a society cannot write down more than it knows",
          "[knowledge][tier1][records]") {
    // Conservation of a sort: the corpus tends toward living knowledge and never
    // past it, so records cannot manufacture knowledge a civilisation never had.
    KnowledgeModule mod;
    WorldState w = make_world(/*era=*/4, 200000, 0.15f, /*knowledge=*/100.0f);
    w.provinces[0].cohort_stats->codified_knowledge = 100.0f;  // already complete
    DeltaBuffer d{};
    mod.execute(w, d);

    float change = 0.0f;
    for (const auto& rd : d.region_deltas)
        if (rd.codified_knowledge_delta.has_value())
            change += *rd.codified_knowledge_delta;
    CHECK(change <= 0.0f);  // nothing left to copy; only decay
}

// ===========================================================================
// THE PRESS — what makes the ratchet permanent.
//
// A scribe copies about one substantial work a year; a press runs off
// hundreds. All of Europe held fewer than 30,000 manuscript books in 1450 and
// somewhere between 8 and 12 million printed ones by 1500 — more than two
// orders of magnitude in fifty years.
//
// This is what ends the possibility of a dark age. Roughly 90% of classical
// Latin literature was lost between 500 and 900 CE because every copy was a
// hand-made object in a named building that could burn. A work in ten
// thousand houses cannot be lost by anything short of the end of the
// civilisation itself.
// ===========================================================================

TEST_CASE("knowledge: a manuscript culture copies by hand", "[knowledge][tier2][printing]") {
    // Below the threshold nothing changes: a society that has not invented movable type
    // copies at exactly the rate a scribe can write.
    const KnowledgeConfig cfg{};
    CHECK_THAT(KnowledgeModule::printing_copy_mult(0.0f, cfg),
               Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK(KnowledgeModule::printing_copy_mult(cfg.printing_knowledge_halfsat * 0.01f, cfg) < 2.0);
}

TEST_CASE("knowledge: the press multiplies copying by orders of magnitude",
          "[knowledge][tier2][printing]") {
    const KnowledgeConfig cfg{};
    const double early = KnowledgeModule::printing_copy_mult(cfg.printing_knowledge_halfsat, cfg);
    const double late =
        KnowledgeModule::printing_copy_mult(50.0f * cfg.printing_knowledge_halfsat, cfg);

    // Half-saturation: half the gain realised.
    CHECK_THAT(early, Catch::Matchers::WithinRel(
                          1.0 + (static_cast<double>(cfg.printing_copy_multiplier) - 1.0) * 0.5,
                          1e-4));
    CHECK(late > 0.9 * static_cast<double>(cfg.printing_copy_multiplier));
    // Saturating: approached, never exceeded. Presses spread; they do not appear
    // everywhere at once, and there is no year in which printing is switched on.
    CHECK(late < static_cast<double>(cfg.printing_copy_multiplier));
}

TEST_CASE("knowledge: printing is gated on what a society knows, not on a date",
          "[knowledge][tier2][printing]") {
    // The gate is accumulated knowledge, so a world that develops faster or slower than
    // Earth still gets the press at the right point in ITS OWN development rather than
    // at a calendar year that means nothing to it.
    const KnowledgeConfig cfg{};
    const double backward = KnowledgeModule::printing_copy_mult(1000.0f, cfg);
    const double advanced = KnowledgeModule::printing_copy_mult(5000000.0f, cfg);
    // The saturating form has a tail, so a Neolithic society at a nine-hundredth of the
    // threshold still reads about 11% faster rather than exactly 1.0 — a hundredfold
    // gain makes even a sliver of adoption visible. Harmless: the corpus is separately
    // bounded by what the society actually knows, so a faster scribe with nothing new to
    // write down copies nothing.
    CHECK(backward < 1.2);
    CHECK(advanced > 10.0);
    CHECK(advanced > 50.0 * backward);
}

// ===========================================================================
// POLYCENTRISM — why a fragmented world keeps what a unified one loses.
//
// Innovation survives if ANY polity in a connected culture-area shelters it.
// Tyndale printed in Antwerp, Galileo circulated in the Netherlands,
// Descartes published in Amsterdam. The reason Europe's scientific revolution
// could not be stopped is that nobody was in a position to stop it everywhere
// at once — and the standing explanation for why China cycled through
// unification and collapse while Europe escaped.
//
// So conquest acquires a real cost here: an empire that absorbs its
// neighbours gains their levies and loses their refuges, and the loss lands
// on the one stock that lets a civilisation start its next cycle above the
// last.
// ===========================================================================

TEST_CASE("knowledge: a single empire preserves no better than a single kingdom",
          "[knowledge][tier2][polycentrism]") {
    const KnowledgeConfig cfg{};
    CHECK_THAT(KnowledgeModule::shelter_loss_divisor(0, cfg),
               Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK_THAT(KnowledgeModule::shelter_loss_divisor(1, cfg),
               Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("knowledge: every extra jurisdiction is another place the books survive",
          "[knowledge][tier2][polycentrism]") {
    const KnowledgeConfig cfg{};
    const double two = KnowledgeModule::shelter_loss_divisor(2, cfg);
    const double six = KnowledgeModule::shelter_loss_divisor(6, cfg);
    CHECK(two > 1.0);
    CHECK(six > two);
    // Six independent shelters lose their records several times more slowly than one.
    CHECK(six > 3.0);
    // Linear in the number of refuges, not saturating: each is a genuinely separate
    // place an idea can outlive its suppression, and there is no point at which one
    // more stops helping.
    CHECK_THAT(six - two, Catch::Matchers::WithinRel(
                              4.0 * static_cast<double>(cfg.record_loss_shelter_weight), 1e-6));
}

TEST_CASE("knowledge: conquest costs a civilisation its refuges",
          "[knowledge][tier2][polycentrism]") {
    // The point of the whole mechanism. A world of six polities that is conquered into
    // one keeps its armies and loses its libraries — and the libraries are what let the
    // next cycle start above the last.
    const KnowledgeConfig cfg{};
    const double fragmented_loss =
        static_cast<double>(cfg.record_loss_per_year) / KnowledgeModule::shelter_loss_divisor(6, cfg);
    const double unified_loss =
        static_cast<double>(cfg.record_loss_per_year) / KnowledgeModule::shelter_loss_divisor(1, cfg);
    CHECK(unified_loss > fragmented_loss);
    CHECK(unified_loss > 3.0 * fragmented_loss);
}

// ===========================================================================
// KNOWLEDGE IS HELD SOMEWHERE (R6).
//
// It was a single global number, and that was the deepest reason no
// civilisation could ever fall. With one figure for the whole world there is no
// such thing as one society collapsing while another rises: there is one
// society with six provinces, and the only trajectory available to it is the
// world's.
//
// Every fall the record actually contains is REGIONAL. Mycenaean Greece lost
// literacy for four centuries while Egypt and Assyria carried on writing; the
// Maya lowlands emptied while the highlands did not; Rome's west fell and its
// east did not.
//
// Knowledge is also NOT CONSERVED, unlike grain: a province learns from its
// neighbour without the neighbour forgetting, because copying a text leaves the
// original. That is what lets a dark region relearn instead of starting from
// nothing — Greek mathematics came back to Europe through Arabic translation.
// ===========================================================================

namespace {

// Two provinces, optionally linked, with whatever each of them knows.
WorldState two_province_world(float knowledge_a, float knowledge_b, bool linked,
                              uint32_t population = 100000) {
    WorldState w{};
    w.current_tick = 365;
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.occupation_catalog.load_builtin_default();
    w.technology.current_era = 4;
    w.technology.knowledge_level = std::max(knowledge_a, knowledge_b);
    for (int i = 0; i < 2; ++i) {
        Province p{};
        p.id = static_cast<uint32_t>(i);
        p.region_id = static_cast<uint32_t>(i);
        p.h3_index = static_cast<H3Index>(i + 1);
        p.cohort_stats = std::make_unique<RegionCohortStats>();
        p.cohort_stats->total_population = population;
        p.cohort_stats->specialist_fraction = 0.05f;
        p.cohort_stats->subsistence_surplus_ratio = 1.2f;
        p.cohort_stats->knowledge_level = i == 0 ? knowledge_a : knowledge_b;
        w.provinces.push_back(std::move(p));
    }
    if (linked) {
        ProvinceLink a{};
        a.neighbor_h3 = static_cast<H3Index>(2);
        w.provinces[0].links.push_back(a);
        ProvinceLink b{};
        b.neighbor_h3 = static_cast<H3Index>(1);
        w.provinces[1].links.push_back(b);
    }
    return w;
}

float province_knowledge_gain(const DeltaBuffer& d, uint32_t region_id) {
    float total = 0.0f;
    for (const auto& rd : d.region_deltas)
        if (rd.region_id == region_id && rd.province_knowledge_delta.has_value())
            total += *rd.province_knowledge_delta;
    return total;
}

}  // namespace

TEST_CASE("knowledge: a province that has lost its scholars does not know what its neighbour does",
          "[knowledge][tier1][regional]") {
    // The point of the whole change. An ignorant province surrounded by learned ones is a
    // possible state of the world now, and it is what a regional dark age IS.
    KnowledgeModule mod;
    WorldState w = two_province_world(/*a=*/0.0f, /*b=*/50000.0f, /*linked=*/false);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    CHECK(w.provinces[0].cohort_stats->knowledge_level <
          w.provinces[1].cohort_stats->knowledge_level);
    // And the world's figure is the FRONTIER — what the best of them knows, which is what
    // an era is dated by. The Bronze Age is dated by whoever had bronze.
    CHECK_THAT(w.technology.knowledge_level,
               Catch::Matchers::WithinRel(w.provinces[1].cohort_stats->knowledge_level, 1e-4f));
}

TEST_CASE("knowledge: a dark region relearns from a neighbour that still knows",
          "[knowledge][tier1][regional]") {
    // Greek mathematics and medicine came back to western Europe through Arabic
    // translation, centuries after the western libraries had gone. A linked province
    // catches up; an unlinked one does not.
    KnowledgeModule mod;
    auto gain_of_ignorant = [&](bool linked) {
        WorldState w = two_province_world(/*a=*/0.0f, /*b=*/100000.0f, linked);
        DeltaBuffer d{};
        mod.execute(w, d);
        return province_knowledge_gain(d, 0);
    };
    const float isolated = gain_of_ignorant(false);
    const float in_contact = gain_of_ignorant(true);

    CHECK(in_contact > isolated);
    // The gap is the diffusion: a hundredth of what its neighbour knows beyond it.
    CHECK(in_contact - isolated > 500.0f);
}

TEST_CASE("knowledge: learning from a neighbour costs the neighbour nothing",
          "[knowledge][tier1][regional]") {
    // Knowledge is NOT conserved, unlike grain. Copying a text leaves the original, so
    // the learned province loses nothing by being learned from — which is why the fall of
    // one civilisation is survivable for the species.
    KnowledgeModule mod;
    WorldState linked = two_province_world(/*a=*/0.0f, /*b=*/100000.0f, /*linked=*/true);
    WorldState alone = two_province_world(/*a=*/0.0f, /*b=*/100000.0f, /*linked=*/false);

    DeltaBuffer dl{};
    mod.execute(linked, dl);
    DeltaBuffer da{};
    mod.execute(alone, da);

    // The teacher gains exactly as much either way.
    CHECK_THAT(province_knowledge_gain(dl, 1),
               Catch::Matchers::WithinRel(province_knowledge_gain(da, 1), 1e-4f));
    // While the student gains more for being in contact.
    CHECK(province_knowledge_gain(dl, 0) > province_knowledge_gain(da, 0));
}

TEST_CASE("knowledge: catching up is easier than leading",
          "[knowledge][tier1][regional]") {
    // Ideas get harder to find against what a place ALREADY knows, so a province at the
    // frontier finds the going harder than one still catching up. This is why late
    // developers converge quickly and why the leader's advantage narrows.
    KnowledgeModule mod;
    WorldState w = two_province_world(/*a=*/1000.0f, /*b=*/2000000.0f, /*linked=*/false);
    DeltaBuffer d{};
    mod.execute(w, d);

    // Identical populations and strata; only what they already know differs.
    CHECK(province_knowledge_gain(d, 0) > province_knowledge_gain(d, 1));
}

// ===========================================================================
// SOMEBODY HAS TO BE ABLE TO READ IT.
//
// A dark age is not a shortage of knowledge in the world. Greek mathematics
// and medicine sat intact in Byzantium and Baghdad the entire time western
// Europe could not read them; what was missing was anyone able to receive it,
// and the recovery came exactly when a literate class existed again to
// translate.
//
// So diffusion needs a receiver. Measured, its absence was why regional falls
// did not happen: with diffusion unconditional, a collapsing province was
// topped straight back up by its neighbours and the deepest drawdown any
// region ever suffered was 8.6% of its own peak. Gating it on the receiving
// province's learned stratum took that to 39.7%.
// ===========================================================================

TEST_CASE("knowledge: a region with no scholars cannot receive what its neighbours know",
          "[knowledge][tier1][absorption]") {
    const KnowledgeConfig cfg{};
    // A province whose learned stratum has scattered absorbs essentially nothing.
    CHECK_THAT(KnowledgeModule::absorptive_capacity(0.0f, cfg),
               Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK(KnowledgeModule::absorptive_capacity(0.001f, cfg) < 0.06);

    // A small literate remnant is enough to transmit — the monasteries that kept copying
    // were never more than a sliver of the population.
    CHECK_THAT(KnowledgeModule::absorptive_capacity(cfg.knowledge_absorption_halfsat, cfg),
               Catch::Matchers::WithinAbs(0.5, 1e-6));
    CHECK(KnowledgeModule::absorptive_capacity(0.10f, cfg) > 0.8);

    // Saturating: more scholars always help, and it never exceeds taking in everything
    // on offer.
    CHECK(KnowledgeModule::absorptive_capacity(0.30f, cfg) >
          KnowledgeModule::absorptive_capacity(0.10f, cfg));
    CHECK(KnowledgeModule::absorptive_capacity(1.0f, cfg) < 1.0);
}

TEST_CASE("knowledge: a dark region beside a learned one stays dark until it can read",
          "[knowledge][tier1][absorption]") {
    // The whole mechanism end to end. Two linked provinces, one knowing a great deal and
    // one knowing nothing; the ignorant one learns only in proportion to the stratum it
    // has to learn WITH.
    auto learned_by = [](float receiver_specialists) {
        KnowledgeModule mod;
        WorldState w = make_world(/*era=*/4, /*population=*/50000, /*spec=*/0.10f,
                                  /*knowledge=*/0.0f);
        // The neighbour: knows a great deal, and is reachable.
        Province p{};
        p.id = 1;
        p.region_id = 1;
        p.h3_index = static_cast<H3Index>(0x2);
        p.cohort_stats = std::make_unique<RegionCohortStats>();
        p.cohort_stats->total_population = 50000;
        p.cohort_stats->specialist_fraction = 0.10f;
        p.cohort_stats->subsistence_surplus_ratio = 1.2f;
        p.cohort_stats->knowledge_level = 100000.0f;
        w.provinces.push_back(std::move(p));
        w.provinces[0].h3_index = static_cast<H3Index>(0x1);
        w.provinces[0].cohort_stats->specialist_fraction = receiver_specialists;
        ProvinceLink to_neighbour{};
        to_neighbour.neighbor_h3 = static_cast<H3Index>(0x2);
        w.provinces[0].links.push_back(to_neighbour);

        DeltaBuffer d{};
        mod.execute(w, d);
        for (const auto& rd : d.region_deltas)
            if (rd.region_id == 0 && rd.province_knowledge_delta.has_value())
                return static_cast<double>(*rd.province_knowledge_delta);
        return 0.0;
    };

    const double literate = learned_by(0.10f);   // has a scholarly class
    const double scattered = learned_by(0.001f);  // its scholars are gone

    CHECK(literate > 0.0);
    CHECK(scattered < literate * 0.2);  // the knowledge is right there and unreachable
}
