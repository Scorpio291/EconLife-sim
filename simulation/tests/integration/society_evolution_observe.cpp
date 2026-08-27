// Society-evolution observer (NOT an assertion test).
//
// Dumps an annual time series of a society's life from the DAWN (era 1, founding
// seed) so a human can SEE whether — and how — it evolves: survive, stagnate,
// develop, or crash. This is the lab for the world-spectrum / World-Class
// work (docs/design/EconLife_World_Spectrum_and_Evolution_Plan.md).
//
// Hidden from the default run; invoke explicitly:
//   econlife_emergence_tests "[.society-observe]"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>

#include "core/world_gen/world_class.h"  // hazard_mortality_from_settings
#include "modules/knowledge/knowledge_module.h"
#include "core/world_gen/technology_catalog.h"

#include "tests/integration/society_evolution_harness.h"

using namespace econlife;
using namespace econlife::society;

static void dump_society(const char* label, const std::vector<SocietySnapshot>& series) {
    std::printf("\n=== %s ===\n", label);
    std::printf("yr |   population | surplus | spec%% |   capital | gini | biz | era\n");
    for (const auto& s : series) {
        std::printf("%2u | %12.0f |  %.2f   | %4.0f%% | %9.0f | %.2f | %3u | %2d\n", s.year,
                    s.total_population, s.mean_surplus, s.specialist_fraction * 100.0,
                    s.total_capital, s.capital_gini, s.businesses, s.era);
    }
    std::printf("  -> trajectory: %s\n", trajectory_name(classify_trajectory(series)));
}

TEST_CASE("society observe: dawn trajectories across seeds", "[.society-observe]") {
    // Same dawn settings, several seeds — watch the spread of outcomes the current
    // (un-dialed) world-gen produces. Once P2 lands, re-run per World Class.
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 50;
    for (uint64_t seed : {1ull, 7ull, 42ull, 1000ull}) {
        char label[64];
        std::snprintf(label, sizeof(label), "DAWN society (seed %llu, %u NPCs, %u yrs)",
                      static_cast<unsigned long long>(seed), kNpcs, kYears);
        auto series = run_society_years(seed, kNpcs, kYears);
        dump_society(label, series);
        REQUIRE(series.size() == kYears + 1);
    }
    std::printf(
        "\n  surplus: food produced / needed (>1 = surplus). spec%%: Layer-2 "
        "specialists among livelihoods. capital: total proto-capital. gini: its "
        "inequality.\n");
}

TEST_CASE("society observe: the world spectrum (garden -> earthlike -> deathworld)",
          "[.society-spectrum]") {
    // Same seed, three points on the dial — watch how the society's fate shifts with
    // the world's Bounty + World Class.
    constexpr uint64_t kSeed = 42;
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 50;
    const WorldArchetype archs[] = {archetype_garden(), archetype_earthlike(),
                                    archetype_deathworld()};
    for (const auto& a : archs) {
        char label[96];
        std::snprintf(label, sizeof(label), "%s (Class %.1f, bounty %.2f)", a.name,
                      world_class(a.hazard), a.bounty);
        auto series = run_society_years(kSeed, kNpcs, kYears, a);
        dump_society(label, series);
        REQUIRE(series.size() == kYears + 1);
    }
    std::printf(
        "\n  the Deathworlders test: garden tends to stagnate, deathworld to "
        "kill/stall, earthlike to develop.\n");
}

TEST_CASE("society observe: knowledge/population trace (calibration)",
          "[.society-knowledge-trace]") {
    // Periodic sample of the earthlike dawn so we can read the actual demographic
    // equilibrium (surplus margin), the specialist fraction it sustains, and the
    // resulting knowledge-accumulation rate — the inputs for setting era thresholds.
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 14000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);
    std::printf("\n=== EARTHLIKE knowledge/pop trace ===\n");
    std::printf("  year |    pop | surplus | spec%% | urban%% | knowledge | era\n");
    for (const auto& s : series) {
        if (s.year % 500 == 0) {
            const double urban_pct =
                s.total_population > 0.0 ? 100.0 * s.urban_population / s.total_population : 0.0;
            std::printf(
                "  %5u | %6.0f |  %.3f  | %4.0f%% | %4.0f%% | %9.0f | %2d | soil %.3f | "
                "ghost %.3f | coal %.3g left | PSI %.4f, faction deaths %.4f\n",
                s.year, s.total_population, s.mean_surplus, s.specialist_fraction * 100.0,
                urban_pct, s.knowledge, s.era, s.soil_health, s.ghost_land, s.coal_remaining,
                s.political_stress, s.faction_deaths);
        }
    }
}

TEST_CASE("society observe: the historical climb (year each era is reached)",
          "[.society-history]") {
    // Long-horizon run: print the first year each era is reached, to read the PACE
    // of the climb through the historical eras (calibration tool).
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;  // the historical climb is millennia-long; fast-forward
    struct Run {
        const char* label;
        WorldArchetype arch;
    };
    const Run runs[] = {{"GARDEN", archetype_garden()},
                        {"EARTHLIKE", archetype_earthlike()},
                        {"DEATHWORLD (barren)", archetype_deathworld()},
                        {"FERTILE DEATHWORLD (Earth-like)", archetype_fertile_deathworld()}};
    for (const auto& r : runs) {
        // fast_forward: coarse yearly stride so 1200 years of history runs in seconds.
        auto series = run_society_years(7, kNpcs, kYears, r.arch, /*founding_hardiness=*/0.0f,
                                        /*fast_forward=*/true);
        std::printf("\n=== %s (Class %.1f, bounty %.2f): era reached / year ===\n", r.label,
                    world_class(r.arch.hazard), r.arch.bounty);
        int prev_era = 0;
        float prev_k = 0.0f;
        uint32_t prev_year = 0;
        for (const auto& s : series) {
            // EVERY era change, not just advances: a civilisation that rises, falls
            // back and climbs again is the behaviour under study, and printing only
            // `era > prev_era` made the falls invisible.
            if (s.era != prev_era) {
                const uint32_t dy = s.year - prev_year;
                const float dk = s.knowledge - prev_k;
                const double urban_pct =
                    s.total_population > 0.0 ? 100.0 * s.urban_population / s.total_population : 0.0;
                std::printf(
                    "  %s era %2d  @ year %5u  (+%4u yrs, knowledge %.0f, %+.2f/yr, pop %.0f, "
                    "spec %.0f%%, urban %.0f%%, capital/head %.0f, gini %.2f)\n",
                    s.era > prev_era ? "RISE" : "FALL", s.era, s.year, dy, s.knowledge,
                    dy > 0 ? dk / dy : 0.0f, s.total_population,
                    s.specialist_fraction * 100.0, urban_pct, s.productive_capital_per_head,
                    s.capital_gini);
                prev_era = s.era;
                prev_k = s.knowledge;
                prev_year = s.year;
            }
        }
        std::printf("  final: era %d at year %u  (%s)\n", series.back().era, series.back().year,
                    trajectory_name(classify_trajectory(series)));
    }
}

// ===========================================================================
// THE CLIMB LANDS ON ITS HISTORICAL DATES (F7 gate).
//
// Earthlike is the anchor because it is Earth: measuring from the dawn of
// agriculture, a world with Earth's hazards and Earth's bounty should reach
// each era at roughly the year that era actually began. This is the one place
// the whole grounded stack is checked against the record rather than against
// itself — soil, wage valve, urban graveyard, ghost acres, structural stress
// and the difficulty of new discoveries all have to be right together for the
// dates to come out.
//
// The tolerance is deliberately loose. The calibration lands within a couple of
// years on the calibrated seed, and asserting THAT would be fitting the test to
// one world; what matters is that no mechanism change silently moves an era by
// a millennium.
// ===========================================================================
TEST_CASE("society: the climb is a climb — eras arrive in order, none skipped, none free",
          "[emergence][integration][society][pacing]") {
    // WHAT THIS GATE STOPPED ASSERTING, AND WHY.
    //
    // It used to require earthlike to hit all seven historical era dates within 1,500
    // years. That could only ever pass because seven `knowledge_to_advance` numbers had
    // been fitted to make it pass — so it tested the calibration, not the model, and the
    // fit made the era thresholds (the one thing the grounding doctrine permits as a pure
    // pacing dial) into the definition of the knowledge unit itself.
    //
    // An era is now a SET OF TECHNIQUES: a society moves on when it has worked out enough
    // of that era's main path, which carries both knowing it and having built for it. How
    // long it takes over that is its own business. A world that runs its main line early
    // and one that spends three thousand years deepening its side branches are both
    // playing properly, and neither is a failure — so this asserts the SHAPE of a climb
    // and nothing about its speed.
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);
    REQUIRE(series.size() > 1);

    std::vector<uint32_t> first_year(16, 0);
    std::vector<bool> seen(16, false);
    for (const auto& s : series) {
        const auto e = static_cast<size_t>(s.era);
        if (e < first_year.size() && !seen[e]) {
            seen[e] = true;
            first_year[e] = s.year;
        }
    }
    std::printf("\n=== earthlike: year each era was reached (a prediction, not a target) ===\n");
    for (size_t e = 1; e <= 8; ++e)
        std::printf("  era %zu: %s\n", e,
                    seen[e] ? (std::to_string(first_year[e]) + " yrs").c_str() : "not reached");

    // It gets somewhere. A world that never leaves the Neolithic in thirteen thousand
    // years is not a slow society, it is a broken model. That is the liveness check, and
    // it is deliberately the ONLY one about distance.
    //
    // This used to also require era 4 or better, which is grading the ascent — the one
    // thing this gate exists not to do. How far a society gets in a given span is a
    // property of that society, and a model that fails when a world takes longer is a
    // model that has an opinion about the right answer. (It is also, at the time of
    // writing, hiding a real defect rather than catching one: earthlike currently stalls
    // in eras 3-4 in a Malthusian limit cycle. That is recorded in the roadmap and worked
    // on there; it is not this gate's job to fail for it, because the same reading would
    // fail a world that was simply slow.)
    REQUIRE(seen[2]);
    const int reached = static_cast<int>(series.back().era);
    INFO("final era " << reached);

    // NOTHING IS SKIPPED. Every era up to the one reached was actually lived through —
    // a society cannot arrive at the Iron Age without having been in the Bronze Age.
    for (int e = 2; e <= reached; ++e) {
        INFO("era " << e);
        CHECK(seen[static_cast<size_t>(e)]);
    }

    // AND THEY ARRIVE IN ORDER, each strictly after the last.
    for (int e = 3; e <= reached; ++e)
        CHECK(first_year[static_cast<size_t>(e)] > first_year[static_cast<size_t>(e - 1)]);

    // NO ERA IS FREE. Each takes real time to work through, so nothing advances the
    // moment it is entered — which is what an unearned era gate looks like.
    for (int e = 3; e <= reached; ++e) {
        const int span = static_cast<int>(first_year[static_cast<size_t>(e)]) -
                         static_cast<int>(first_year[static_cast<size_t>(e - 1)]);
        INFO("era " << (e - 1) << " lasted " << span << " years");
        CHECK(span >= 10);
    }

    // And the dawn is long. Working out farming, herding, pottery, weaving, settlement
    // and the ard is the work of millennia however fast a world moves afterwards.
    CHECK(first_year[2] > 300u);
}

TEST_CASE("society: different worlds ascend at different speeds, and that is the point",
          "[emergence][integration][society][pacing]") {
    // The corollary of not grading the speed: worlds must actually DIFFER. If a garden
    // and a deathworld climb on the same schedule then something is still dragging every
    // world through the eras regardless of what it earned, which is what the fitted
    // thresholds did.
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    struct Run {
        const char* name;
        WorldArchetype arch;
    };
    const Run runs[] = {
        {"GARDEN", archetype_garden()},
        {"EARTHLIKE", archetype_earthlike()},
        {"FERTILE DEATHWORLD", archetype_fertile_deathworld()},
    };
    std::printf("\n=== how far each world got in %u years ===\n", kYears);
    std::vector<int> finals;
    for (const auto& r : runs) {
        auto series = run_society_years(7, kNpcs, kYears, r.arch, 0.0f, /*fast_forward=*/true);
        REQUIRE(!series.empty());
        const int era = static_cast<int>(series.back().era);
        finals.push_back(era);
        std::printf("  %-20s era %d, population %.0f\n", r.name, era,
                    series.back().total_population);
    }
    // Not all the same. A model in which the world's bounty and hazards make no
    // difference to how far a society gets is not modelling anything.
    const bool all_equal = finals[0] == finals[1] && finals[1] == finals[2];
    CHECK_FALSE(all_equal);
}

// ===========================================================================
// DO REGIONS ACTUALLY FALL? (R6 measurement)
//
// R6 made knowledge a per-province stock so that one region can lose what it
// knew while its neighbours keep theirs — the shape every real collapse has.
// The era trace showed no regressions afterward, but the era is the FRONTIER
// (a maximum), so it only moves when the LEADING province falls. That leaves an
// open question the era trace cannot answer: do individual regions fall?
//
// This prints the spread. If the laggard tracks the leader, the world is still
// one civilisation with six provinces and R6 bought nothing behaviourally. If
// they diverge — and especially if a province's knowledge goes DOWN in absolute
// terms while the frontier rises — then regional rise and fall is real and the
// only thing missing is that the era, being a maximum, does not show it.
// ===========================================================================
TEST_CASE("society observe: does any REGION lose what it knew? (R6)",
          "[.society-regional-falls]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);

    std::printf("\n=== EARTHLIKE: knowledge spread across provinces ===\n");
    std::printf("  year | leader        | laggard       | mean          | ratio | era\n");
    for (const auto& s : series) {
        if (s.year % 1000 != 0)
            continue;
        const double ratio =
            s.knowledge_leader > 0.0 ? s.knowledge_laggard / s.knowledge_leader : 1.0;
        std::printf("  %5u | %13.0f | %13.0f | %13.0f | %.3f | %2d\n", s.year,
                    s.knowledge_leader, s.knowledge_laggard, s.knowledge_mean, ratio, s.era);
    }

    // The question the era trace cannot answer: did any province's own knowledge fall,
    // in absolute terms, over a sustained stretch — a regional dark age?
    //
    // Reported separately for the DAWN and the CLIMB, because they mean different things.
    // A drawdown in the first centuries is the founding transient settling (the seeded
    // urban composition draining, the stratum forming); one during the climb is a region
    // that had built something and lost it, which is the thing actually under study.
    constexpr uint32_t kDawnEnds = 1000;
    double worst_dawn = 0.0, worst_climb = 0.0;
    uint32_t dawn_year = 0, climb_year = 0;
    int episodes = 0;  // distinct climb-era drawdowns past a tenth
    bool in_episode = false;
    double laggard_peak = 0.0;
    for (const auto& s : series) {
        laggard_peak = std::max(laggard_peak, s.knowledge_laggard);
        if (laggard_peak <= 0.0)
            continue;
        const double drawdown = 1.0 - s.knowledge_laggard / laggard_peak;
        if (s.year < kDawnEnds) {
            if (drawdown > worst_dawn) {
                worst_dawn = drawdown;
                dawn_year = s.year;
            }
            continue;
        }
        if (drawdown > worst_climb) {
            worst_climb = drawdown;
            climb_year = s.year;
        }
        if (drawdown > 0.10 && !in_episode) {
            in_episode = true;
            ++episodes;
        } else if (drawdown <= 0.02) {
            in_episode = false;  // recovered: the next fall is a new episode
        }
    }
    std::printf("\n  dawn transient (before year %u): %.1f%% drawdown at year %u\n", kDawnEnds,
                worst_dawn * 100.0, dawn_year);
    std::printf("  DURING THE CLIMB:  deepest %.1f%% at year %u, in %d distinct episodes "
                "past a tenth\n",
                worst_climb * 100.0, climb_year, episodes);
    const auto& last = series.back();
    std::printf("  final spread: leader %.0f, laggard %.0f (laggard holds %.1f%% of the "
                "frontier)\n",
                last.knowledge_leader, last.knowledge_laggard,
                last.knowledge_leader > 0.0
                    ? 100.0 * last.knowledge_laggard / last.knowledge_leader
                    : 100.0);
    std::printf("  If the ratio stays near 1.00 the world is still ONE civilisation with "
                "six provinces:\n  diffusion is holding them together and nothing regional "
                "can fall.\n");
}

// ===========================================================================
// IS ANY SHOCK BIG ENOUGH TO BREAK ONE REGION? (the open question, measured)
//
// Regional falls do not happen during the climb. The capability is present —
// knowledge is per province, and a province that loses its stratum can no
// longer absorb what its neighbours know — but nothing ever drives a province
// down far enough to scatter that stratum in the first place.
//
// The stabilising couplings are each individually correct: grain diffuses to
// whoever is short, refugees leave for wherever is better, knowledge diffuses
// from whoever knows more. The destabilising ones all exist. So the question
// is a magnitude question, and it is answerable: over a whole climb, how bad
// did it EVER get for the worst-hit single province, on each channel?
//
// World means hide precisely this. A mean surplus of 1.5 says nothing about
// the province sitting at 0.4.
// ===========================================================================
TEST_CASE("society observe: how bad does it ever get for ONE province?",
          "[.society-worst-province]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    struct Run {
        const char* label;
        WorldArchetype arch;
    };
    const Run runs[] = {{"EARTHLIKE", archetype_earthlike()},
                        {"DEATHWORLD (barren)", archetype_deathworld()}};

    for (const auto& r : runs) {
        auto series = run_society_years(7, kNpcs, kYears, r.arch, /*founding_hardiness=*/0.0f,
                                        /*fast_forward=*/true);
        // Skip the founding transient: what matters is what a BUILT society suffers.
        constexpr uint32_t kDawnEnds = 1000;
        double worst_surplus = 1e9, worst_soil = 1e9, worst_spec = 1e9;
        double worst_war = 0.0, worst_faction = 0.0, worst_psi = 0.0;
        uint32_t worst_surplus_year = 0, worst_war_year = 0;
        int hungry_years = 0, war_years = 0;
        for (const auto& s : series) {
            if (s.year < kDawnEnds)
                continue;
            if (s.worst_province_surplus < worst_surplus) {
                worst_surplus = s.worst_province_surplus;
                worst_surplus_year = s.year;
            }
            worst_soil = std::min(worst_soil, s.worst_province_soil);
            worst_spec = std::min(worst_spec, s.min_province_specialists);
            if (s.max_war_deaths > worst_war) {
                worst_war = s.max_war_deaths;
                worst_war_year = s.year;
            }
            worst_faction = std::max(worst_faction, s.max_faction_deaths);
            worst_psi = std::max(worst_psi, s.political_stress);
            if (s.worst_province_surplus < 1.0)
                ++hungry_years;
            if (s.max_war_deaths > 0.0)
                ++war_years;
        }
        std::printf("\n=== %s: the worst any ONE province ever had it (after year %u) ===\n",
                    r.label, kDawnEnds);
        std::printf("  food:      worst surplus %.3f (year %u); %d years with some province "
                    "under 1.0\n",
                    worst_surplus, worst_surplus_year, hungry_years);
        std::printf("  soil:      worst %.3f of pristine\n", worst_soil);
        std::printf("  stratum:   smallest non-farming share %.4f  (scattering is what makes a "
                    "fall stick)\n",
                    worst_spec);
        std::printf("  war:       worst annual death fraction %.4f (year %u); %d years with any "
                    "war at all\n",
                    worst_war, worst_war_year, war_years);
        std::printf("  faction:   worst annual death fraction %.4f\n", worst_faction);
        std::printf("  PSI:       peak %.3f\n", worst_psi);

        // THE POLITICAL MAP OVER TIME. War needs somebody to fight, asabiya needs a
        // frontier to be forged at, and polycentrism needs more than one place for a book
        // to survive in. A world that unifies loses all three at once — and Earth never
        // unified, which is the whole reason its history has more than one civilisation
        // in it.
        std::printf("  polities:  ");
        for (const auto& s2 : series) {
            const bool early = s2.year <= 2000 && s2.year % 250 == 0;
            const bool late = s2.year > 2000 && s2.year % 2000 == 0;
            if (early || late)
                std::printf("y%u:%d ", s2.year, s2.polity_count);
        }
        std::printf("\n");
    }
    std::printf("\n  A region breaks when its stratum scatters. If the smallest stratum any\n"
                "  province ever holds stays comfortably above zero, no shock in this model\n"
                "  is big enough to break one — and that is a magnitude question about\n"
                "  shocks that already exist, not a missing mechanism.\n");
}

// ===========================================================================
// AGAIN AND AGAIN? (the brief, measured)
//
// The design brief asks for "the ability to rise and fail, again and again,
// slowly creeping forward". One fall is not "again and again", so this counts
// them: every episode where a world loses a fifth or more of its population
// from a running peak, with the year, the depth, and how long it took to get
// back.
//
// It also reports what happened to KNOWLEDGE across each one, because that is
// the difference between a cycle and a ratchet. If knowledge is retained
// through a population collapse, the civilisation is creeping forward while
// the society falls — which is the shape asked for. If it is lost with the
// people, each cycle starts over and nothing accumulates.
// ===========================================================================
TEST_CASE("society observe: how many times does it fall? (the brief)",
          "[.society-cycles]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    struct Run {
        const char* label;
        WorldArchetype arch;
    };
    const Run runs[] = {{"GARDEN", archetype_garden()},
                        {"EARTHLIKE", archetype_earthlike()},
                        {"FERTILE DEATHWORLD", archetype_fertile_deathworld()}};

    for (const auto& r : runs) {
        auto series = run_society_years(7, kNpcs, kYears, r.arch, /*founding_hardiness=*/0.0f,
                                        /*fast_forward=*/true);
        std::printf("\n=== %s: population collapses of 20%% or more ===\n", r.label);

        constexpr double kFallThreshold = 0.20;
        double peak = 0.0, trough = 0.0, peak_knowledge = 0.0;
        uint32_t peak_year = 0;
        bool falling = false;
        int episodes = 0;
        for (const auto& s : series) {
            const double pop = s.total_population;
            if (!falling) {
                if (pop >= peak) {
                    peak = pop;
                    peak_year = s.year;
                    peak_knowledge = static_cast<double>(s.knowledge);
                    continue;
                }
                if (peak > 0.0 && (1.0 - pop / peak) >= kFallThreshold) {
                    falling = true;
                    trough = pop;
                }
            } else {
                trough = std::min(trough, pop);
                if (pop >= peak) {
                    // Recovered to the old peak: the episode is over.
                    ++episodes;
                    std::printf("  fall %d: peaked %.0f at year %u, bottomed %.0f (-%.0f%%), "
                                "back by year %u (%u yrs); knowledge %.0f -> %.0f\n",
                                episodes, peak, peak_year, trough, 100.0 * (1.0 - trough / peak),
                                s.year, s.year - peak_year, peak_knowledge,
                                static_cast<double>(s.knowledge));
                    falling = false;
                    peak = pop;
                    peak_year = s.year;
                    peak_knowledge = static_cast<double>(s.knowledge);
                }
            }
        }
        if (falling) {
            ++episodes;
            const auto& last = series.back();
            std::printf("  fall %d: peaked %.0f at year %u, bottomed %.0f (-%.0f%%), STILL DOWN "
                        "at year %u; knowledge %.0f -> %.0f\n",
                        episodes, peak, peak_year, trough, 100.0 * (1.0 - trough / peak),
                        last.year, peak_knowledge, static_cast<double>(last.knowledge));
        }
        if (episodes == 0)
            std::printf("  none — this world only ever rose.\n");
        else
            std::printf("  %d collapse%s over %u years.\n", episodes, episodes == 1 ? "" : "s",
                        kYears);
    }
    std::printf("\n  Knowledge holding up across a population collapse is the ratchet: the\n"
                "  society falls while the civilisation keeps what it learned.\n");
}

// ===========================================================================
// DOES IT ACTUALLY INDUSTRIALISE? (improvement hunt)
//
// Reaching "era 8" on a knowledge threshold is not the same as having an
// industrial economy. The test of that is how many people the land still
// needs: an English farmer fed about 3 people in 1800 and about 7 by 1900,
// and the share of the workforce in agriculture fell from roughly 35% in 1800
// to 22% by 1851 and 9% by 1900.
//
// This prints the COHORT non-farming share (not the tracked-NPC sample the
// other observations use) alongside urbanisation and capital per head, so the
// question "did the economy change shape, or only the era label" is answerable.
// ===========================================================================
TEST_CASE("society observe: does the economy actually industrialise?",
          "[.society-industrialise]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);

    std::printf("\n=== EARTHLIKE: the shape of the economy, by era ===\n");
    std::printf("  era | year  | non-farming | SUPPORTED | urban | capital/head | pop        |"
                " surplus\n");
    int prev_era = 0;
    for (const auto& s : series) {
        if (s.era == prev_era)
            continue;
        prev_era = s.era;
        const double urban_pct =
            s.total_population > 0.0 ? 100.0 * s.urban_population / s.total_population : 0.0;
        std::printf("  %3d | %5u | %10.1f%% | %8.1f%% | %4.1f%% | %12.0f | %10.0f | %.3f\n",
                    s.era, s.year, s.cohort_specialist_share * 100.0, s.supported_share * 100.0,
                    urban_pct, s.productive_capital_per_head, s.total_population, s.mean_surplus);
    }
    std::printf("\n  For reference: England had ~35%% of its workforce in agriculture in 1800,\n"
                "  22%% by 1851 and 9%% by 1900 — so a NON-FARMING share of 65%%, 78%%, 91%%.\n"
                "  Urbanisation went from ~20%% to 54%% to 77%% over the same century.\n");
}

// F7 — RE-ANCHORING THE ERA THRESHOLDS.
//
// The era thresholds are the one calibration surface the grounding doctrine allows:
// they are pure pacing dials and shape nothing mechanical. This prints the knowledge an
// EARTHLIKE world actually holds at the year each era historically began, measuring from
// the dawn of agriculture (era 1 = 10,000 BCE, so `start_year + 10000` is the target).
//
// Earthlike is the anchor on purpose: it is Earth. A garden world should then reach each
// era sooner and a deathworld later or never, which is the spectrum the thresholds exist
// to express — not something they should be tuned per-world to produce.
//
// This is ITERATIVE: the thresholds change the trajectory (era sets the food and
// mortality multipliers and the specialist ceiling), so re-run after editing eras.csv.
TEST_CASE("society observe: knowledge at each era's historical year (F7 calibration)",
          "[.society-threshold-calibration]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);

    struct Target {
        int era;
        const char* name;
        int start_year;  // as in eras.csv
    };
    const Target targets[] = {
        {2, "Bronze Age", -3300}, {3, "Iron Age", -1200},     {4, "Classical", -550},
        {5, "Medieval", 500},     {6, "Early Modern", 1450},  {7, "Industrial", 1750},
        {8, "Turn of Millennium", 2000},
    };

    std::printf("\n=== EARTHLIKE: knowledge at each era's historical year ===\n");
    std::printf("  era | %-20s | target year | knowledge held | era actually at\n", "name");
    for (const auto& t : targets) {
        const auto target_year = static_cast<uint32_t>(t.start_year + 10000);
        const SocietySnapshot* at = nullptr;
        for (const auto& s : series) {
            if (s.year >= target_year) {
                at = &s;
                break;
            }
        }
        if (at == nullptr)
            continue;
        std::printf("  %3d | %-20s | %11u | %14.0f | %d\n", t.era, t.name, target_year,
                    static_cast<double>(at->knowledge), at->era);
    }
    // The peak matters as much as the path: a threshold above what the world ever holds
    // is an era no Earth can reach, which reads as a failed civilisation rather than a
    // calibration error.
    float peak = 0.0f;
    uint32_t peak_year = 0;
    for (const auto& s : series) {
        if (s.knowledge > peak) {
            peak = s.knowledge;
            peak_year = s.year;
        }
    }
    std::printf("  peak knowledge %.0f at year %u (final %.0f at %u)\n",
                static_cast<double>(peak), peak_year,
                static_cast<double>(series.back().knowledge), series.back().year);
}

TEST_CASE("society observe: transplant — soft vs native people on a harsh world",
          "[.society-transplant]") {
    // Same harsh-but-FERTILE world (high hazard, plenty of food, so the difference is
    // adaptation, not starvation). Natives are adapted; a soft garden-bred people
    // (hardiness ~0.2) are not, and pay for it in mortality until they harden.
    constexpr uint64_t kSeed = 42;
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 50;
    WorldArchetype harsh_fertile = archetype_deathworld();
    harsh_fertile.name = "harsh-fertile";
    harsh_fertile.bounty = 1.6f;  // fertile, so food is not the limiter

    auto native = run_society_years(kSeed, kNpcs, kYears, harsh_fertile, /*founding_hardiness=*/0.0f);
    auto soft = run_society_years(kSeed, kNpcs, kYears, harsh_fertile, /*founding_hardiness=*/0.2f);

    dump_society("NATIVE people (adapted to the harsh world)", native);
    dump_society("SOFT transplant (garden-bred, hardiness 0.2)", soft);
    std::printf(
        "\n  a soft people dropped onto a hard world are culled until they harden;\n"
        "  the natives, adapted, cope from the start.\n");
    REQUIRE(native.size() == kYears + 1);
    REQUIRE(soft.size() == kYears + 1);
}

TEST_CASE("society observe: who actually produces knowledge (mechanism audit)",
          "[.society-knowledge-who]") {
    // Calibration/diagnostic companion to the knowledge trace. The trace shows the
    // RATE; this shows WHERE it comes from, so a stall can be attributed to a
    // mechanism instead of guessed at. It exists because a filter regression once
    // zeroed the scholar corps and left every world at era 1 for 13,000 years while
    // all three test gates stayed green — the rate looked like "slow progress"
    // rather than "nobody is doing the work".
    //
    // Prints, over a short dawn run: the surplus, how many significant NPCs hold a
    // knowledge-bearing occupation (elder/scribe/scholar), and the split of the
    // year's knowledge production between the scholar corps and the diffuse
    // population term.
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 300;

    WorldGeneratorConfig config{};
    config.seed = 7;
    config.province_count = 6;
    config.npc_count = kNpcs;
    config.starting_era = 1;
    config.founding_seed_mode = true;
    config.goods_directory = find_goods_dir_society();
    config.technology_directory = find_base_game_subdir("technology");
    config.bounty_scale = archetype_earthlike().bounty;
    config.hazard_settings = archetype_earthlike().hazard;

    WorldState world = WorldGenerator::generate(config);
    TickOrchestrator orch;
    register_base_game_modules(orch);
    orch.finalize_registration();
    ThreadPool pool(1);

    double last_level = 0.0;
    std::printf("\n=== WHO PRODUCES KNOWLEDGE (earthlike dawn) ===\n");
    std::printf("  year | surplus | occupied | knowledge-keepers | knowledge\n");
    for (uint32_t y = 0; y <= kYears; ++y) {
        if (y % 50 == 0) {
            uint32_t occupied = 0;
            uint32_t keepers = 0;
            uint32_t living_keepers = 0;
            double keeper_output = 0.0;
            double living_output = 0.0;
            uint32_t living_npcs = 0;
            for (const auto& npc : world.significant_npcs)
                if (npc.status != NPCStatus::dead && npc.status != NPCStatus::fled)
                    ++living_npcs;
            for (const auto& npc : world.significant_npcs) {
                if (npc.occupation == 0)
                    continue;
                ++occupied;
                const OccupationDefinition* o = world.occupation_catalog.by_index(npc.occupation);
                if (o != nullptr && o->knowledge_output > 0.0f) {
                    ++keepers;
                    keeper_output += static_cast<double>(o->knowledge_output);
                    // The module only counts the LIVING. An occupation persists on a
                    // dead record (significant_npcs is append-only), so a corps that
                    // is dying without replacement looks intact here and produces
                    // nothing there.
                    const bool gone = npc.status == NPCStatus::dead ||
                                      npc.status == NPCStatus::fled ||
                                      npc.status == NPCStatus::imprisoned;
                    if (!gone) {
                        ++living_keepers;
                        living_output += static_cast<double>(o->knowledge_output);
                    }
                }
            }
            double surplus = 0.0;
            int counted = 0;
            for (const auto& p : world.provinces) {
                if (p.cohort_stats) {
                    surplus += static_cast<double>(p.cohort_stats->subsistence_surplus_ratio);
                    ++counted;
                }
            }
            // Recompute the module's own terms here so the PUBLISHED year-over-year
            // change can be compared against what the formula says it should be. A
            // gap between them is a wiring fault; agreement means the rate is a
            // calibration question.
            const double pop_total = [&] {
                double t = 0.0;
                for (const auto& p : world.provinces)
                    if (p.cohort_stats)
                        t += static_cast<double>(p.cohort_stats->total_population);
                return t;
            }();
            const double avg_surplus = counted > 0 ? surplus / counted : 1.0;
            double capital_stock = 0.0;
            for (const auto& p : world.provinces)
                if (p.cohort_stats)
                    capital_stock += static_cast<double>(p.cohort_stats->productive_capital);
            const float world_hazard = hazard_mortality_from_settings(world.hazard_settings);
            const double scarcity = std::clamp(1.0 - avg_surplus, 0.0, 1.0);
            const double pressure =
                std::min(0.35 + 0.6 * std::max(0.0, static_cast<double>(world_hazard) - 0.45) +
                             1.4 * scarcity,
                         3.0);
            const double specialist_term = keeper_output * 0.4;
            const double pop_term = 1.5e-6 * pop_total;
            const double predicted = (specialist_term + pop_term) * pressure;
            // Ask the module itself what it publishes for THIS exact state. If this
            // agrees with `predicted` but the accumulated level does not, the loss is
            // downstream (how often the module runs, or how the delta is applied)
            // rather than in the production formula.
            double module_says = 0.0;
            {
                KnowledgeModule probe;
                DeltaBuffer scratch{};
                probe.execute(world, scratch);
                for (const auto& td : scratch.technology_deltas)
                    if (td.knowledge_delta.has_value())
                        module_says += static_cast<double>(*td.knowledge_delta);
            }
            const double actual = static_cast<double>(world.technology.knowledge_level) - last_level;
            std::printf(
                "  %5u |  %.3f  | %8u | %8u (out %.1f) | %9.1f | pop %8.0f | pressure %.2f | "
                "living %3u | keepers-living %2u (out %.1f) | capital/head %.1f | "
                "predicted/yr %.4f | module/yr %.4f | actual/yr %.4f\n",
                y, avg_surplus, occupied, keepers, keeper_output,
                static_cast<double>(world.technology.knowledge_level), pop_total, pressure,
                living_npcs, living_keepers, living_output,
                pop_total > 0.0 ? capital_stock / pop_total : 0.0, predicted, module_says, y == 0 ? 0.0 : actual / 50.0);
            last_level = static_cast<double>(world.technology.knowledge_level);
        }
        for (uint32_t t = 0; t < 365; ++t)
            orch.execute_tick(world, pool);
    }
}

TEST_CASE("society observe: knowledge mechanism audit (fast, 20y)",
          "[.society-knowledge-quick]") {
    // Calibration/diagnostic companion to the knowledge trace. The trace shows the
    // RATE; this shows WHERE it comes from, so a stall can be attributed to a
    // mechanism instead of guessed at. It exists because a filter regression once
    // zeroed the scholar corps and left every world at era 1 for 13,000 years while
    // all three test gates stayed green — the rate looked like "slow progress"
    // rather than "nobody is doing the work".
    //
    // Prints, over a short dawn run: the surplus, how many significant NPCs hold a
    // knowledge-bearing occupation (elder/scribe/scholar), and the split of the
    // year's knowledge production between the scholar corps and the diffuse
    // population term.
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 20;

    WorldGeneratorConfig config{};
    config.seed = 7;
    config.province_count = 6;
    config.npc_count = kNpcs;
    config.starting_era = 1;
    config.founding_seed_mode = true;
    config.goods_directory = find_goods_dir_society();
    config.technology_directory = find_base_game_subdir("technology");
    config.bounty_scale = archetype_earthlike().bounty;
    config.hazard_settings = archetype_earthlike().hazard;

    WorldState world = WorldGenerator::generate(config);
    TickOrchestrator orch;
    register_base_game_modules(orch);
    orch.finalize_registration();
    ThreadPool pool(1);

    double last_level = 0.0;
    std::printf("\n=== KNOWLEDGE MECHANISM AUDIT (fast, 20y) ===\n");
    std::printf("  year | surplus | occupied | knowledge-keepers | knowledge\n");
    for (uint32_t y = 0; y <= kYears; ++y) {
        if (y % 5 == 0) {
            uint32_t occupied = 0;
            uint32_t keepers = 0;
            uint32_t living_keepers = 0;
            double keeper_output = 0.0;
            double living_output = 0.0;
            uint32_t living_npcs = 0;
            for (const auto& npc : world.significant_npcs)
                if (npc.status != NPCStatus::dead && npc.status != NPCStatus::fled)
                    ++living_npcs;
            for (const auto& npc : world.significant_npcs) {
                if (npc.occupation == 0)
                    continue;
                ++occupied;
                const OccupationDefinition* o = world.occupation_catalog.by_index(npc.occupation);
                if (o != nullptr && o->knowledge_output > 0.0f) {
                    ++keepers;
                    keeper_output += static_cast<double>(o->knowledge_output);
                    // The module only counts the LIVING. An occupation persists on a
                    // dead record (significant_npcs is append-only), so a corps that
                    // is dying without replacement looks intact here and produces
                    // nothing there.
                    const bool gone = npc.status == NPCStatus::dead ||
                                      npc.status == NPCStatus::fled ||
                                      npc.status == NPCStatus::imprisoned;
                    if (!gone) {
                        ++living_keepers;
                        living_output += static_cast<double>(o->knowledge_output);
                    }
                }
            }
            double surplus = 0.0;
            int counted = 0;
            for (const auto& p : world.provinces) {
                if (p.cohort_stats) {
                    surplus += static_cast<double>(p.cohort_stats->subsistence_surplus_ratio);
                    ++counted;
                }
            }
            // Recompute the module's own terms here so the PUBLISHED year-over-year
            // change can be compared against what the formula says it should be. A
            // gap between them is a wiring fault; agreement means the rate is a
            // calibration question.
            const double pop_total = [&] {
                double t = 0.0;
                for (const auto& p : world.provinces)
                    if (p.cohort_stats)
                        t += static_cast<double>(p.cohort_stats->total_population);
                return t;
            }();
            const double avg_surplus = counted > 0 ? surplus / counted : 1.0;
            double capital_stock = 0.0;
            for (const auto& p : world.provinces)
                if (p.cohort_stats)
                    capital_stock += static_cast<double>(p.cohort_stats->productive_capital);
            const float world_hazard = hazard_mortality_from_settings(world.hazard_settings);
            const double scarcity = std::clamp(1.0 - avg_surplus, 0.0, 1.0);
            const double pressure =
                std::min(0.35 + 0.6 * std::max(0.0, static_cast<double>(world_hazard) - 0.45) +
                             1.4 * scarcity,
                         3.0);
            const double specialist_term = keeper_output * 0.4;
            const double pop_term = 1.5e-6 * pop_total;
            const double predicted = (specialist_term + pop_term) * pressure;
            // Ask the module itself what it publishes for THIS exact state. If this
            // agrees with `predicted` but the accumulated level does not, the loss is
            // downstream (how often the module runs, or how the delta is applied)
            // rather than in the production formula.
            double module_says = 0.0;
            {
                KnowledgeModule probe;
                DeltaBuffer scratch{};
                probe.execute(world, scratch);
                for (const auto& td : scratch.technology_deltas)
                    if (td.knowledge_delta.has_value())
                        module_says += static_cast<double>(*td.knowledge_delta);
            }
            const double actual = static_cast<double>(world.technology.knowledge_level) - last_level;
            std::printf(
                "  %5u |  %.3f  | %8u | %8u (out %.1f) | %9.1f | pop %8.0f | pressure %.2f | "
                "living %3u | keepers-living %2u (out %.1f) | capital/head %.1f | "
                "predicted/yr %.4f | module/yr %.4f | actual/yr %.4f\n",
                y, avg_surplus, occupied, keepers, keeper_output,
                static_cast<double>(world.technology.knowledge_level), pop_total, pressure,
                living_npcs, living_keepers, living_output,
                pop_total > 0.0 ? capital_stock / pop_total : 0.0, predicted, module_says, y == 0 ? 0.0 : actual / 5.0);
            last_level = static_cast<double>(world.technology.knowledge_level);
        }
        for (uint32_t t = 0; t < 365; ++t)
            orch.execute_tick(world, pool);
    }
}

TEST_CASE("society observe: why is the dawn population not pressing on its land?",
          "[.society-ecology]") {
    // The chain under test: a surplus should feed more mouths, more mouths should press
    // harder on the land and the wild stocks, and that pressure should pull the surplus
    // back down. If the surplus stays high while the population barely moves, either the
    // demography is not answering the food signal or something else is killing people.
    // This prints both sides so the answer is not a guess.
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);
    std::printf("\n=== EARTHLIKE: the population and the environment it lives off ===\n");
    std::printf("  year |    pop |  growth | surplus | spec%% | urban%% | soil  | topsoil | forest | fish/K       | fishers |"
                " era\n");
    for (size_t i = 0; i < series.size(); ++i) {
        const auto& s = series[i];
        if (s.year % 500 != 0)
            continue;
        double growth = 0.0;
        if (i >= 500 && series[i - 500].total_population > 0.0)
            growth = 100.0 * (std::pow(s.total_population / series[i - 500].total_population,
                                       1.0 / 500.0) - 1.0);
        const double urban_pct =
            s.total_population > 0.0 ? 100.0 * s.urban_population / s.total_population : 0.0;
        std::printf("  %5u | %6.0f | %+6.3f%% |  %.3f  | %4.0f%% | %5.0f%% | %.3f |  %.4f | %.4f |"
                    " %.3f/%.3f | %7.0f | %2d\n",
                    s.year, s.total_population, growth, s.mean_surplus,
                    s.cohort_specialist_share * 100.0, urban_pct, s.soil_health, s.topsoil,
                    s.forest, s.fish_stock, s.fish_capacity, s.fishers, s.era);
    }
}

// Rate calibration probe. The era ladder is CONTENT now (the running total of the tech
// tree's own weights — TechnologyCatalog::derive_era_thresholds), so the only fitted
// quantity left is the RATE knowledge accumulates at, KnowledgeConfig::production_scalar.
// One dial instead of seven, and a rate decides nothing about the shape of the climb —
// which means the dates the other eras land on are a PREDICTION rather than a fit.
//
// tools/calibration/calib_rate.sh bisects on this output.
TEST_CASE("rate calibration: year each era is reached on earthlike", "[.rate-calibration]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);
    std::vector<uint32_t> first(16, 0);
    std::vector<bool> seen(16, false);
    for (const auto& s : series) {
        const auto e = static_cast<size_t>(s.era);
        if (e < first.size() && !seen[e]) {
            seen[e] = true;
            first[e] = s.year;
        }
    }
    for (size_t e = 2; e <= 8; ++e)
        std::printf("ERA %zu YEAR %d\n", e, seen[e] ? static_cast<int>(first[e]) : -1);
}

// Fine-resolution look at the limit cycle: what actually kills the population, and what
// the ceiling is doing while it happens.
TEST_CASE("society observe: inside the cycle", "[.society-cycle-detail]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);
    std::printf("\n  year |    pop |  d%%/yr | surplus | spec%% | soil  | topsl | forest |"
                " store/yr | plague | war    | faction | PSI    | era\n");
    for (size_t i = 1; i < series.size(); ++i) {
        const auto& s = series[i];
        if (s.year < 6400 || s.year > 9200 || s.year % 50 != 0) continue;
        const double prev = series[i - 1].total_population;
        const double g = prev > 0 ? 100.0 * (s.total_population / prev - 1.0) : 0.0;
        std::printf("  %5u | %6.0f | %+6.2f | %7.3f | %4.0f%% | %.3f | %.3f | %.4f |"
                    " %8.2f | %.3f | %.4f | %.4f | %.4f | %2d\n",
                    s.year, s.total_population, g, s.mean_surplus,
                    s.cohort_specialist_share * 100.0, s.soil_health, s.topsoil, s.forest,
                    s.food_store_years, s.plague_susceptible, s.max_war_deaths,
                    s.max_faction_deaths, s.political_stress, s.era);
    }
}

// Is the climb held up by a bootstrap threshold? Capital gates how much of its knowledge a
// society can apply; building capital needs a surplus; the surplus needs applied capital.
// If that is a hump rather than a slope, a society either gets over it or sits below it
// forever — which would explain both the stall and the total absence of secular cycles.
TEST_CASE("society observe: the bootstrap", "[.society-bootstrap]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(),
                                    /*founding_hardiness=*/0.0f, /*fast_forward=*/true);
    std::printf("\n  year |    pop | surplus | spec%% | knowledge |  cap/head | tech-food |"
                " store | era\n");
    for (const auto& s : series) {
        if (s.year % 500 != 0) continue;
        std::printf("  %5u | %6.0f | %7.3f | %4.0f%% | %9.0f | %9.0f | %9.3f | %5.2f | %2d\n",
                    s.year, s.total_population, s.mean_surplus, s.cohort_specialist_share * 100.0,
                    static_cast<double>(s.knowledge), s.productive_capital_per_head, s.tech_food,
                    s.food_store_years, s.era);
    }
}

// IS THE BOOM-AND-BUST PHYSICS OR ARITHMETIC? The dawn arc is integrated at one
// orchestrator step per year — a 365x under-sample — and a strongly nonlinear system
// stepped in one-year jumps overshoots the way a coarse Euler step always does. If the
// swings shrink as the stride gets finer, they were never in the model.
TEST_CASE("society observe: does the cycle survive a finer stride?", "[.society-stride]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 9000;
    std::printf("\n  steps/yr |    peak |  trough | worst fall | end pop | end era | knowledge"
                " | K-drawdown\n");
    for (uint32_t stride : {1u, 4u, 12u, 52u}) {
        auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(), 0.0f, false,
                                        stride);
        double peak = 0, trough = 1e18, worst = 0, run_max = 0;
        double k_max = 0, k_draw = 0;
        for (const auto& s : series) {
            peak = std::max(peak, s.total_population);
            if (s.total_population > 0) trough = std::min(trough, s.total_population);
            run_max = std::max(run_max, s.total_population);
            if (run_max > 0)
                worst = std::max(worst, 1.0 - s.total_population / run_max);
            k_max = std::max(k_max, static_cast<double>(s.knowledge));
            if (k_max > 0)
                k_draw = std::max(k_draw, 1.0 - static_cast<double>(s.knowledge) / k_max);
        }
        const auto& last = series.back();
        std::printf("  %8u | %7.0f | %7.0f | %9.0f%% | %7.0f | %7d | %9.0f | %9.0f%%\n",
                    stride, peak, trough, worst * 100.0, last.total_population, last.era,
                    static_cast<double>(last.knowledge), k_draw * 100.0);
    }
}

// DO DIFFERENT WORLDS HAVE DIFFERENT HISTORIES? A model whose every seed tells the same
// story is not simulating anything; it is replaying one. This reports the spread.
TEST_CASE("society observe: the spread across seeds", "[.society-seeds]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    std::printf("\n   seed | era2 | era3 | era4 | era5 | end era | end pop | knowledge |"
                " peak pop | worst fall | K-drawdown | falls>20%%\n");
    for (uint64_t seed : {1ull, 7ull, 42ull, 1000ull, 2024ull}) {
        auto series = run_society_years(seed, kNpcs, kYears, archetype_earthlike(), 0.0f, true);
        std::vector<int> first(9, -1);
        double peak = 0, run_max = 0, worst = 0, k_max = 0, k_draw = 0;
        int falls = 0;
        bool in_fall = false;
        for (const auto& s : series) {
            const auto e = static_cast<size_t>(s.era);
            if (e < first.size() && first[e] < 0) first[e] = static_cast<int>(s.year);
            peak = std::max(peak, s.total_population);
            run_max = std::max(run_max, s.total_population);
            const double draw = run_max > 0 ? 1.0 - s.total_population / run_max : 0.0;
            worst = std::max(worst, draw);
            if (draw > 0.20 && !in_fall) { falls++; in_fall = true; }
            if (draw < 0.05) in_fall = false;
            k_max = std::max(k_max, static_cast<double>(s.knowledge));
            if (k_max > 0)
                k_draw = std::max(k_draw, 1.0 - static_cast<double>(s.knowledge) / k_max);
        }
        const auto& last = series.back();
        std::printf("  %5llu | %4d | %4d | %4d | %4d | %7d | %7.0f | %9.0f | %8.0f | %9.0f%% |"
                    " %9.0f%% | %8d\n",
                    (unsigned long long)seed, first[2], first[3], first[4], first[5], last.era,
                    last.total_population, static_cast<double>(last.knowledge), peak,
                    worst * 100.0, k_draw * 100.0, falls);
    }
}

// WHAT A PEOPLE IS, over the whole climb. Nutrition, health and schooling are stocks with
// long memories, and the rates that used to be fitted constants now come out of them.
TEST_CASE("society observe: what these people are", "[.society-people]") {
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 13000;
    auto series = run_society_years(7, kNpcs, kYears, archetype_earthlike(), 0.0f, true);
    std::printf("\n  year |    pop | surplus | spec%% | stature | fit-days | schooling |"
                " knowledge | era\n");
    for (const auto& s : series) {
        if (s.year % 500 != 0) continue;
        std::printf("  %5u | %6.0f | %7.3f | %4.0f%% |   %.3f |    %.3f | %9.2f | %9.0f | %2d\n",
                    s.year, s.total_population, s.mean_surplus, s.cohort_specialist_share * 100.0,
                    s.nutrition, s.health, s.schooling, static_cast<double>(s.knowledge), s.era);
    }
}
