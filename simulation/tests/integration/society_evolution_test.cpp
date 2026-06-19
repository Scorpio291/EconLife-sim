#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "society_evolution_harness.h"

using namespace econlife;
using namespace econlife::society;

namespace {
SocietySnapshot snap(uint32_t year, double pop, double surplus, double spec, double cap,
                     uint32_t biz, int era, bool extinct = false) {
    SocietySnapshot s;
    s.year = year;
    s.total_population = pop;
    s.mean_surplus = surplus;
    s.specialist_fraction = spec;
    s.total_capital = cap;
    s.businesses = biz;
    s.era = era;
    s.extinct = extinct;
    return s;
}
}  // namespace

// ---------------------------------------------------------------------------
// Trajectory classifier (pure — instant)
// ---------------------------------------------------------------------------
TEST_CASE("society: trajectory classifier labels the archetypal runs",
          "[integration][society]") {
    SECTION("extinction") {
        std::vector<SocietySnapshot> s = {snap(0, 1000, 1.0, 0, 0, 0, 1),
                                          snap(1, 200, 0.4, 0, 0, 0, 1),
                                          snap(2, 0, 0.0, 0, 0, 0, 1, /*extinct=*/true)};
        CHECK(classify_trajectory(s) == Trajectory::Extinct);
    }
    SECTION("bare subsistence — survives but flat") {
        std::vector<SocietySnapshot> s = {snap(0, 1000, 1.0, 0, 0, 0, 1),
                                          snap(1, 1010, 1.0, 0, 0, 0, 1),
                                          snap(2, 1005, 1.0, 0, 0, 0, 1)};
        CHECK(classify_trajectory(s) == Trajectory::BareSubsistence);
    }
    SECTION("developing — surplus + some specialization") {
        std::vector<SocietySnapshot> s = {snap(0, 1000, 1.0, 0.0, 0, 0, 1),
                                          snap(1, 1100, 1.3, 0.08, 500, 0, 1)};
        CHECK(classify_trajectory(s) == Trajectory::Developing);
    }
    SECTION("thriving — growth + specialization + advancement") {
        std::vector<SocietySnapshot> s = {snap(0, 1000, 1.0, 0.0, 0, 0, 1),
                                          snap(1, 1400, 1.6, 0.3, 5000, 3, 2)};
        CHECK(classify_trajectory(s) == Trajectory::Thriving);
    }
    SECTION("overshoot then crash") {
        std::vector<SocietySnapshot> s = {snap(0, 1000, 1.2, 0, 0, 0, 1),
                                          snap(1, 1600, 0.8, 0, 0, 0, 1),
                                          snap(2, 600, 0.5, 0, 0, 0, 1)};
        CHECK(classify_trajectory(s) == Trajectory::OvershootCrash);
    }
}

// ---------------------------------------------------------------------------
// A dawn world runs forward and is observable
// ---------------------------------------------------------------------------
TEST_CASE("society: a dawn world runs forward and yields a clean time series",
          "[integration][society]") {
    auto series = run_society_years(/*seed=*/1, /*npc_count=*/50, /*years=*/2);
    REQUIRE(series.size() == 3);

    // Founding world starts populated; every snapshot is finite and sane.
    CHECK(series.front().total_population > 0.0);
    for (const auto& s : series) {
        CHECK(std::isfinite(s.total_population));
        CHECK(std::isfinite(s.mean_surplus));
        CHECK(std::isfinite(s.total_capital));
        CHECK(s.capital_gini >= 0.0);
        CHECK(s.capital_gini <= 1.0);
        CHECK(s.era == 1);  // stays at the dawn over this short horizon
    }

    // The classifier produces one of its labels (smoke).
    Trajectory t = classify_trajectory(series);
    CHECK((t == Trajectory::Extinct || t == Trajectory::OvershootCrash ||
           t == Trajectory::BareSubsistence || t == Trajectory::Developing ||
           t == Trajectory::Thriving));
}

TEST_CASE("society: dawn runs are deterministic", "[integration][society]") {
    auto a = run_society_years(/*seed=*/7, /*npc_count=*/40, /*years=*/2);
    auto b = run_society_years(/*seed=*/7, /*npc_count=*/40, /*years=*/2);
    REQUIRE(a.size() == b.size());
    const auto& la = a.back();
    const auto& lb = b.back();
    CHECK(la.total_population == lb.total_population);
    CHECK(la.total_capital == lb.total_capital);
    CHECK(la.specialist_fraction == lb.specialist_fraction);
    CHECK(la.era == lb.era);
}
