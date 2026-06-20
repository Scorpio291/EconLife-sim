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
#include <cstdio>

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
