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

TEST_CASE("emergence: national legitimacy reflects (healthy) conditions",
          "[emergence][integration]") {
    // The national legitimacy roll-up tracks provincial conditions. With the
    // economy functioning (grievance grounded in material conditions, ~0.15;
    // unemployment ~0; stability recovered), legitimacy must NOT crater — a
    // materially-healthy nation retains the population's consent. (Legitimacy
    // CRATERING under a forced crisis, and the regime-differentiated response to
    // it, are covered by the political_cycle [unrest] unit tests.)
    const auto& s = baseline();
    REQUIRE(s.back().mean_grievance < 0.5);  // economy is healthy on this baseline
    CHECK(s.back().national_legitimacy > 0.25);
}

TEST_CASE("emergence: province stability stays healthy (does not collapse)",
          "[emergence][integration]") {
    // Promoted from a ratchet. Grievance is now grounded in material conditions
    // and relaxes (single owner: community_response), so it no longer pins the
    // community at maximum escalation and drags stability to zero. A healthy
    // baseline province retains real stability.
    const auto& s = baseline();
    CHECK(s.back().mean_stability > 0.20);
}

TEST_CASE("emergence: community grievance stays bounded (restoring force works)",
          "[emergence][integration]") {
    // Promoted from a ratchet. Grievance was a memory-only accumulator (plus a
    // ~300/tick social_consequence firehose and uncoordinated writers) that
    // pinned at 1.0 regardless of the economy. Now it is grounded in material
    // deprivation with a single owner and a restoring force: on a healthy
    // baseline it settles low instead of saturating at the ceiling.
    const auto& s = baseline();
    CHECK(s.back().mean_grievance < 0.5);
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

// NOTE: the former crisis ratchets "sustained mass grievance produces organized
// opposition" and "the state responds to a legitimacy crisis" tested an end-to-end
// crisis cascade that the broken (grievance-pinned) world produced involuntarily.
// With the economy functioning, the baseline world is healthy and does NOT enter
// crisis on its own — crises are player/event-driven. The crisis MECHANISMS are
// validated directly and deterministically by the [political_cycle][unrest] unit
// tests (autocratic suppression→collapse, democratic concession+turnover,
// failed-state fragmentation) and the community_response opposition-formation unit
// test. An end-to-end "deprived/exploited world produces unrest" emergence scenario
// awaits a persistent economic-deprivation source (no JobPosting producer / mass-
// layoff event yet) — tracked in the session log.

TEST_CASE("emergence: unemployment never approaches 100 percent", "[emergence][integration]") {
    // There is always some kind of work for willing bodies. With the decision
    // engine calibrated (inaction gate at the bottom of the EV scale) and the
    // informal wage floor in place, the population stays active and measured
    // unemployment is the inaction margin (~0.0 on this baseline; frictional
    // unemployment >0 will emerge as motivation profiles diversify). Guard:
    // it must never again approach the 0.96-1.0 pathology.
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
