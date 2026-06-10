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

static void dump_series(const char* label, const std::vector<Snapshot>& series) {
    std::printf("\n=== %s ===\n", label);
    std::printf(
        "yr | active wait imp dead | crim crImp | evid consq | crBiz sig | "
        "unemp griev | RESPONSE: stage(mean/max) cohes trust resrc | NATION govt legit\n");
    for (std::size_t i = 0; i < series.size(); ++i) {
        const auto& s = series[i];
        std::printf(
            "%2zu | %6d %4d %3d %4d | %4d %5d | %4zu %5zu | %4zu %3d | "
            "%.2f %.2f | stage %.1f/%d  coh %.2f  trust %.2f  res %.2f | govt %d legit %.2f\n",
            i, s.active, s.waiting, s.imprisoned, s.dead, s.criminals, s.criminals_imprisoned,
            s.evidence_pool, s.consequence_queue, s.criminal_businesses, s.criminal_biz_with_signal,
            s.mean_unemployment, s.mean_grievance, s.mean_response_stage, s.max_response_stage,
            s.mean_cohesion, s.mean_inst_trust, s.mean_resource_access, s.home_government_type,
            s.national_legitimacy);
    }
    std::printf(
        "  govt: 0 Democracy 1 Autocracy 2 Federation 3 FailedState | "
        "stage 0..6 (none..sustained_opposition)\n");
}

TEST_CASE("emergence baseline: 10-year aggregate time series", "[.emergence-observe]") {
    // The unrest-response contrast: same mass-unemployment shock, three regimes.
    constexpr uint64_t kSeed = 42;
    constexpr uint32_t kNpcs = 500;
    constexpr uint32_t kYears = 10;
    constexpr int kDemocracy = 0, kAutocracy = 1, kFailedState = 3;

    auto democracy = run_world_years(kSeed, kNpcs, kYears, 0.10f, kDemocracy);
    auto autocracy = run_world_years(kSeed, kNpcs, kYears, 0.10f, kAutocracy);
    auto failed = run_world_years(kSeed, kNpcs, kYears, 0.10f, kFailedState);

    dump_series("DEMOCRACY (accountability: turnover + concession)", democracy);
    dump_series("AUTOCRACY (suppression: martyr ratchet, possible collapse)", autocracy);
    dump_series("FAILED STATE (fragmentation: criminal dominance rises)", failed);

    REQUIRE(democracy.back().tick == kYears * 365u);
}
