// Emergence baseline observer (NOT an assertion test).
//
// Dumps an annual time series of behavioral aggregates over 10 in-game years so
// a human can SEE what the simulation actually does over a long horizon: which
// feedback loops are alive (markets, evidence generation, capital) and which are
// frozen or pathological (criminal justice, province conditions). Findings from
// this observer drive the assertions in emergence_test.cpp.
//
// Hidden from the default run; invoke explicitly:
//   econlife_integration_tests "[.emergence-observe]"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

#include "tests/integration/emergence_harness.h"

using namespace econlife;
using namespace econlife::emergence;

TEST_CASE("emergence baseline: 10-year aggregate time series", "[.emergence-observe]") {
    auto series = run_world_years(/*seed=*/42, /*npc_count=*/500, /*years=*/10);

    std::printf("\n=== EMERGENCE BASELINE (seed=42, 500 NPCs, 6 provinces) ===\n");
    std::printf(
        "yr | active wait imp dead | crim crImp crDead | evid consq | biz crBiz sig | era | "
        "stab crime gini griev domin unemp | pop | totCap maxCap | pSpread\n");
    for (std::size_t i = 0; i < series.size(); ++i) {
        const auto& s = series[i];
        std::printf(
            "%2zu | %6d %4d %3d %4d | %4d %5d %6d | %4zu %5zu | %3zu %4zu %3d | %3d | "
            "%.2f %.3f %.2f %.2f %.3f %.2f | %.0f | %.2e %.2e | %.2f\n",
            i, s.active, s.waiting, s.imprisoned, s.dead, s.criminals, s.criminals_imprisoned,
            s.criminals_dead, s.evidence_pool, s.consequence_queue, s.businesses,
            s.criminal_businesses, s.criminal_biz_with_signal, s.era, s.mean_stability,
            s.mean_crime, s.mean_gini, s.mean_grievance, s.mean_dominance, s.mean_unemployment,
            s.total_population, s.total_capital, s.max_capital, s.price_spread);
    }
    std::printf("=== END BASELINE ===\n\n");

    REQUIRE(series.back().tick == 10u * 365u);
}
