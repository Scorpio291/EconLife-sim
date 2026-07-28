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
            std::printf("  %5u | %6.0f |  %.3f  | %4.0f%% | %4.0f%% | %9.0f | %2d\n", s.year,
                        s.total_population, s.mean_surplus, s.specialist_fraction * 100.0, urban_pct,
                        s.knowledge, s.era);
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
            if (s.era > prev_era) {
                const uint32_t dy = s.year - prev_year;
                const float dk = s.knowledge - prev_k;
                const double urban_pct =
                    s.total_population > 0.0 ? 100.0 * s.urban_population / s.total_population : 0.0;
                std::printf(
                    "  era %2d  @ year %5u  (+%4u yrs, knowledge %.0f, +%.2f/yr, pop %.0f, "
                    "spec %.0f%%, urban %.0f%%, gini %.2f)\n",
                    s.era, s.year, dy, s.knowledge, dy > 0 ? dk / dy : 0.0f, s.total_population,
                    s.specialist_fraction * 100.0, urban_pct, s.capital_gini);
                prev_era = s.era;
                prev_k = s.knowledge;
                prev_year = s.year;
            }
        }
        std::printf("  final: era %d at year %u  (%s)\n", series.back().era, series.back().year,
                    trajectory_name(classify_trajectory(series)));
    }
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
            double keeper_output = 0.0;
            for (const auto& npc : world.significant_npcs) {
                if (npc.occupation == 0)
                    continue;
                ++occupied;
                const OccupationDefinition* o = world.occupation_catalog.by_index(npc.occupation);
                if (o != nullptr && o->knowledge_output > 0.0f) {
                    ++keepers;
                    keeper_output += static_cast<double>(o->knowledge_output);
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
            const float world_hazard = hazard_mortality_from_settings(world.hazard_settings);
            const double scarcity = std::clamp(1.0 - avg_surplus, 0.0, 1.0);
            const double pressure =
                std::min(0.35 + 0.6 * std::max(0.0, static_cast<double>(world_hazard) - 0.45) +
                             1.4 * scarcity,
                         3.0);
            const double specialist_term = keeper_output * 0.4;
            const double pop_term = 1.5e-6 * pop_total;
            const double predicted = (specialist_term + pop_term) * pressure;
            const double actual = static_cast<double>(world.technology.knowledge_level) - last_level;
            std::printf(
                "  %5u |  %.3f  | %8u | %8u (out %.1f) | %9.1f | pop %8.0f | pressure %.2f | "
                "predicted/yr %.4f | actual/yr %.4f\n",
                y, avg_surplus, occupied, keepers, keeper_output,
                static_cast<double>(world.technology.knowledge_level), pop_total, pressure,
                predicted, y == 0 ? 0.0 : actual / 50.0);
            last_level = static_cast<double>(world.technology.knowledge_level);
        }
        for (uint32_t t = 0; t < 365; ++t)
            orch.execute_tick(world, pool);
    }
}
