// Emergence / behavioral integration tests.
//
// These assert that the simulation, run through the real base-game orchestrator
// over multiple in-game years, produces the EMERGENT behavior the GDD promises
// — not just that values stay non-NaN and bounded (which world_gen_integration
// already covers). They are the executable form of "does the thing actually
// behave?".
//
// Two groups:
//   [emergence]               — loops that are alive today; locked in as
//                               regression guards.
//   [emergence][!shouldfail]  — loops the 10-year baseline (emergence_observe)
//                               showed are broken/frozen. Each is the intended
//                               invariant, expected to FAIL today. Catch2 keeps
//                               the suite green while they fail; the moment a
//                               loop is fixed the test PASSES, which Catch flags
//                               as an unexpected pass — the signal to drop the
//                               [!shouldfail] tag. One test per loop so they
//                               flip independently.
//
// Baseline (seed 42, 500 NPCs, 10y) that motivated these — see emergence_observe:
//   - active NPCs collapse 500→~10 (rest fall into `waiting`) by year 1;
//   - province stability 0.80→0.00 (pinned), grievance 0.20→1.00 (pinned),
//     unemployment →1.00 (pinned), crime_rate →0.00 despite 36 criminals;
//   - evidence_pool grows to ~2000 and consequence_queue to ~565, yet ZERO
//     imprisonments ever occur — the detection→prosecution→imprisonment loop
//     never closes;
//   - markets move and capital accumulates (these are alive).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "tests/integration/emergence_harness.h"

using namespace econlife;
using namespace econlife::emergence;
using Catch::Matchers::WithinAbs;

namespace {
constexpr uint64_t kSeed = 42;
constexpr uint32_t kNpcs = 200;
constexpr uint32_t kYears = 3;  // scale/horizon where the pathologies manifest robustly.
                                // emergence_observe covers the fuller 10-year narrative. This is
                                // an opt-in behavioral suite (ctest label "emergence"), not the
                                // fast per-commit gate — a multi-year orchestrated run is ~tens of
                                // seconds and not unit-gate material.

// Memoized shared run: every assertion below (except the determinism test, which
// needs two independent runs) reads the same time series, so a single process
// invocation of [emergence] pays for one multi-year world run, not one per test.
const std::vector<Snapshot>& baseline() {
    static const std::vector<Snapshot> s = run_world_years(kSeed, kNpcs, kYears);
    return s;
}

template <typename Pred>
bool any_year(const std::vector<Snapshot>& s, Pred p) {
    for (const auto& snap : s)
        if (p(snap))
            return true;
    return false;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Group 1: loops that are alive — regression guards
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("emergence: simulation runs a multi-year horizon without NaN/crash",
          "[emergence][integration]") {
    const auto& s = baseline();
    REQUIRE(s.back().tick == kYears * 365u);
    for (const auto& snap : s) {
        REQUIRE_FALSE(std::isnan(snap.total_capital));
        REQUIRE(std::isfinite(snap.max_capital));
    }
}

TEST_CASE("emergence: markets are alive (prices move over time)", "[emergence][integration]") {
    const auto& s = baseline();
    double lo = 1e30, hi = -1e30;
    for (const auto& snap : s) {
        lo = std::min(lo, snap.price_spread);
        hi = std::max(hi, snap.price_spread);
    }
    // Cross-province price spread for a good must change over the run — a static
    // spread would mean the price engine / supply chain isn't responding.
    CHECK(hi - lo > 0.5);
}

TEST_CASE("emergence: criminal activity generates evidence", "[emergence][integration]") {
    const auto& s = baseline();
    // With a criminal population, the evidence pool must become non-empty —
    // confirms facility_signals→evidence→pool is producing observable signal.
    CHECK(any_year(s, [](const Snapshot& x) { return x.evidence_pool > 0; }));
    // And the net_signal pipeline this work wired must reach criminal businesses.
    CHECK(any_year(s, [](const Snapshot& x) { return x.criminal_biz_with_signal > 0; }));
}

TEST_CASE("emergence: capital economy is active (wealth accumulates, stays bounded)",
          "[emergence][integration]") {
    const auto& s = baseline();
    REQUIRE(s.front().total_capital > 0.0);
    // The wealthiest actor's capital should move (economy isn't frozen) and stay
    // finite (no runaway to the safety ceiling).
    CHECK(s.back().max_capital != s.front().max_capital);
    CHECK(std::isfinite(s.back().max_capital));
}

TEST_CASE("emergence: identical seed reproduces identical behavior", "[emergence][integration]") {
    const auto& a = baseline();                      // reuse the shared run as run #1
    auto b = run_world_years(kSeed, kNpcs, kYears);  // independent run #2
    const auto& fa = a.back();
    const auto& fb = b.back();
    CHECK(fa.evidence_pool == fb.evidence_pool);
    CHECK(fa.consequence_queue == fb.consequence_queue);
    CHECK(fa.imprisoned == fb.imprisoned);
    CHECK(fa.active + fa.waiting == fb.active + fb.waiting);
    CHECK_THAT(fa.total_capital, WithinAbs(fb.total_capital, 1.0));
}

TEST_CASE("emergence: national legitimacy reacts to provincial conditions",
          "[emergence][integration]") {
    // Slice 1 of the unrest-response design: the national roll-up must respond —
    // under the mass-grievance baseline, national legitimacy craters from its 0.5
    // start. (That the response then has no effect is the ratchet below.)
    const auto& s = baseline();
    REQUIRE(s.back().mean_grievance > 0.7);  // there IS a national crisis
    CHECK(s.back().national_legitimacy < 0.40);
}

TEST_CASE("emergence: province stability does not fully collapse", "[emergence][integration]") {
    // Was a [!shouldfail] ratchet (stability 0.80→0.00 and pinned). The
    // democratic concession branch of the unrest response (institutional-trust
    // restoration + grievance relief in the worst provinces) now keeps stability
    // off the floor on the Federation baseline — the loop is (partially) closed,
    // so this is a regression guard, not a known gap.
    const auto& s = baseline();
    CHECK(s.back().mean_stability > 0.05);
}

// ─────────────────────────────────────────────────────────────────────────────
// Group 2: known-broken loops — intended invariants, expected to fail today.
// Drop the [!shouldfail] tag when the loop is fixed (the test will start passing).
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("emergence: criminal justice loop closes (some prosecution lands)",
          "[emergence][integration][!shouldfail]") {
    // Evidence accrues and consequences queue, but the baseline shows ZERO
    // imprisonments in 10 years. Detection→case→conviction→imprisonment must
    // close at least once given a criminal population over multiple years.
    const auto& s = baseline();
    REQUIRE(s.back().criminals > 0);
    CHECK(any_year(
        s, [](const Snapshot& x) { return x.imprisoned > 0 || x.criminals_imprisoned > 0; }));
}

TEST_CASE("emergence: community grievance relaxes from its peak (has a restoring force)",
          "[emergence][integration][!shouldfail]") {
    // Baseline: grievance rises monotonically toward 1.00 and stays pinned,
    // which holds community response at maximum escalation forever. A healthy
    // world relaxes grievance once conditions stop being worst-case. Stated
    // horizon-independently: the final grievance should sit below its peak.
    // Today grievance only ever climbs, so final == peak and this fails.
    const auto& s = baseline();
    double peak = 0.0;
    for (const auto& snap : s)
        peak = std::max(peak, snap.mean_grievance);
    CHECK(s.back().mean_grievance < peak - 0.02);
}

TEST_CASE("emergence: sustained mass grievance produces organized opposition",
          "[emergence][integration][!shouldfail]") {
    // GDD 14.4: when grievance is high (>=0.85) with leadership and resources,
    // the community escalates to stage 6 (sustained_opposition) and an
    // OppositionOrganization forms. Baseline: grievance pins at 1.0 and
    // resource_access at ~1.0, but the escalation JAMS at stage 4
    // (economic_resistance) and never reaches 6 — because cohesion collapses to
    // 0.00 (gating direct_action at >=0.45) once NPCs fall inactive. The
    // population is maximally aggrieved but structurally unable to organize. A
    // society under sustained mass unemployment/grievance should produce
    // organized opposition, not sit frozen at boycotts for a decade.
    const auto& s = baseline();
    REQUIRE(s.back().mean_grievance > 0.7);         // precondition: mass grievance exists
    REQUIRE(s.back().mean_resource_access > 0.35);  // and the resource gate is satisfied
    CHECK(any_year(s, [](const Snapshot& x) {
        return x.max_response_stage >= 6;  // sustained_opposition reached somewhere
    }));
}

TEST_CASE("emergence: the state responds to a legitimacy crisis (it does not just crater)",
          "[emergence][integration][!shouldfail]") {
    // Slice 2 target: a legitimacy crisis must provoke a STATE response that
    // changes the trajectory — democratic turnover + concession, autocratic
    // suppression, or failed-state fragmentation — so legitimacy recovers from
    // its trough instead of flooring forever. Today legitimacy only craters and
    // pins (no regime-differentiated response exists), so final == trough.
    const auto& s = baseline();
    double trough = 1.0;
    for (const auto& snap : s)
        trough = std::min(trough, snap.national_legitimacy);
    CHECK(s.back().national_legitimacy > trough + 0.02);
}

TEST_CASE("emergence: unemployment never approaches 100 percent",
          "[emergence][integration][!shouldfail]") {
    // There is always some kind of work for willing bodies — even the worst
    // real collapses top out far below total unemployment (Great Depression
    // ~25%; informal/subsistence work absorbs the rest). The informal wage
    // floor + spec-correct metric (active-without-employer = informal worker)
    // are in, but unemployment still reads ~0.96 because the NPC decision
    // engine is mis-calibrated: work's EV (weight 0.25 × prob ~0.78 × magnitude
    // 0.5 ≈ 0.0975) sits below the inaction threshold (0.10) from day one, so
    // ~96% of NPCs fall to `waiting` and count as unemployed. Flips when the
    // GDD §3 utility/threshold calibration pass lands.
    const auto& s = baseline();
    CHECK(s.back().mean_unemployment < 0.60);
}

TEST_CASE("emergence: regional crime rate reflects the criminal population",
          "[emergence][integration][!shouldfail]") {
    // Baseline: crime_rate →0.00 even though 36 criminals and 2 criminal
    // businesses persist. The regional crime metric is disconnected from the
    // actual criminal presence it is meant to aggregate.
    const auto& s = baseline();
    REQUIRE(s.back().criminals > 0);
    CHECK(s.back().mean_crime > 0.001);
}
