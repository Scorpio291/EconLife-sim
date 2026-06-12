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

TEST_CASE("emergence econ diagnostic: regime-dependent wealth concentration", "[.emergence-econ]") {
    // Wealth concentrates by default; only a state with the policy + capacity to
    // redistribute pushes it back. Run the SAME economy under three regimes and
    // watch the divergence: an accountable welfare state (Federation) bounds the
    // top fortune and keeps inequality/grievance lower; a kleptocratic Autocracy
    // taxes the rich only weakly so capital compounds further; a FailedState has
    // no fiscal apparatus at all and lets concentration run unchecked.
    struct Regime {
        const char* name;
        int govt;
    };
    const Regime regimes[] = {{"Federation (accountable welfare state)", 2},
                              {"Autocracy (kleptocratic, elite capture)", 1},
                              {"FailedState (no fiscal apparatus)", 3}};
    for (const auto& r : regimes) {
        auto s = run_world_years(/*seed=*/42, /*npc_count=*/300, /*years=*/5,
                                 /*criminal_baseline=*/0.10f, /*force_government_type=*/r.govt);
        std::printf("\n=== ECON DIAGNOSTIC (seed 42, 300 NPCs) — %s ===\n", r.name);
        std::printf("yr | griev ineq | maxCap richestRole | maxBizCash maxBizRev crimBiz\n");
        for (std::size_t i = 0; i < s.size(); ++i) {
            const auto& x = s[i];
            std::printf("%2zu | %.2f  %.2f | %.2e role=%d%s | %.2e %.2e %s\n", i, x.mean_grievance,
                        x.mean_inequality, x.max_capital, x.richest_role,
                        x.richest_is_owner ? "(own)" : "", x.max_business_cash,
                        x.max_business_revenue, x.richest_biz_criminal ? "CRIM" : "legit");
        }
        REQUIRE(s.back().tick == 5u * 365u);
    }
    std::printf("=== END ECON DIAGNOSTIC ===\n\n");
}

TEST_CASE("emergence shock: depression drives the unrest cascade end-to-end",
          "[.emergence-shock]") {
    // Inject a persistent supply/output collapse at the start of year 2 and watch
    // the cascade run through the REAL orchestrator: output -> revenue collapse ->
    // distress layoffs (employment_negative + wage-theft memories) -> grievance ->
    // community-response escalation -> regime-differentiated response. Nothing here
    // writes grievance/stage/legitimacy directly; the shock only throttles factory
    // output. Years 0-1 are the pre-shock baseline; the shock bites from year 2 on.
    constexpr uint64_t kSeed = 42;
    constexpr uint32_t kNpcs = 200;
    constexpr uint32_t kYears = 5;
    constexpr int kDemocracy = 0, kAutocracy = 1;

    DepressionShock shock{};
    shock.onset_tick = 365;         // start of year 2
    shock.output_retained = 0.05f;  // factories fall to 5% of output

    auto demo = run_world_years(kSeed, kNpcs, kYears, 0.10f, kDemocracy, shock);
    auto auto_ = run_world_years(kSeed, kNpcs, kYears, 0.10f, kAutocracy, shock);

    dump_series("SHOCK: democracy under depression (concession response)", demo);
    dump_series("SHOCK: autocracy under depression (suppression -> possible collapse)", auto_);

    // Economic-substrate trace so the cause is visible alongside the social effect.
    auto trace = [](const char* label, const std::vector<Snapshot>& s) {
        std::printf("\n=== ECON TRACE: %s ===\n", label);
        std::printf("yr | formalEmp unemp | insolvBiz wageTheftMemos | grievance maxStage\n");
        for (std::size_t i = 0; i < s.size(); ++i) {
            const auto& x = s[i];
            std::printf("%2zu |   %.2f    %.2f |  %4d      %5d        |   %.2f      %d\n", i,
                        x.formal_employment, x.mean_unemployment, x.insolvent_businesses,
                        x.npcs_with_wage_theft_memory, x.mean_grievance, x.max_response_stage);
        }
    };
    trace("democracy under depression", demo);
    trace("autocracy under depression", auto_);

    REQUIRE(demo.back().tick == kYears * 365u);
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
