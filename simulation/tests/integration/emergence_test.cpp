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

#include "modules/random_events/random_events_module.h"
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

// ─── National legitimacy: a causal contract ─────────────────────────────────
//
// These replace "national legitimacy reflects (healthy) conditions", which checked
// nations[0] > 0.25 after three years. That number was never a property of a
// healthy society in this model: legitimacy rolls up trust, stability, grievance and
// unemployment, and stability also answers to random shocks that have nothing to
// do with material health. The old pass was itself an artefact: before R1 the world
// was forced into several nations and nations[0] happened to be the two stable
// provinces, while the population-weighted target over the whole world sat at 0.16.
// The investigation (handoff R1 Legitimacy, 2026-10-09) also found two defects in
// the shock path, both fixed: every province and seeds 42-45 drew one event stream,
// and the event rate read the market against a reference it never moves toward.
//
// What a healthy society IS owed, and what is asserted instead:
//   1. Legitimacy is what its causes make it: every nation's legitimacy stays within
//      the reach of its own EMA of its population-weighted target. A hidden writer,
//      a wrong weighting or a nation sampled by accident breaks this.
//   2. A society nothing strikes keeps its consent: with the random-event roll off,
//      every nation stays above the crisis line the state responds to.
//   3. When shocks lower it, they lower it through the channels they act on:
//      events strike stability and grievance, and the fall from the counterfactual
//      is carried by those two terms of the target.

namespace {

constexpr uint64_t kLegitimacySeeds[] = {42, 43, 44, 45};

struct LegitimacyRun {
    std::vector<Snapshot> years;
    // Per tick, per nation: the target its provinces set after that tick.
    std::vector<std::vector<double>> targets;
};

LegitimacyRun run_legitimacy(uint64_t seed, bool events_on) {
    LegitimacyRun r;
    r.years = run_world_years(
        seed, kNpcs, kYears, 0.10f, -1,
        [&r](WorldState& w, uint32_t) {
            std::vector<double> t;
            for (const auto& n : w.nations)
                t.push_back(nation_snapshot(w, n).target);
            r.targets.push_back(std::move(t));
        },
        [events_on](TickOrchestrator& orch) {
            if (events_on)
                return;
            for (const auto& m : orch.modules())
                if (auto* e = dynamic_cast<RandomEventsModule*>(m.get()))
                    e->set_base_rate(0.0f);
        });
    return r;
}

// Both runs per seed, shared by the three contracts below.
const std::vector<std::pair<LegitimacyRun, LegitimacyRun>>& legitimacy_runs() {
    static const auto runs = [] {
        std::vector<std::pair<LegitimacyRun, LegitimacyRun>> v;
        for (uint64_t seed : kLegitimacySeeds)
            v.emplace_back(run_legitimacy(seed, true), run_legitimacy(seed, false));
        return v;
    }();
    return runs;
}

}  // namespace

TEST_CASE("emergence: national legitimacy is what its causes make it", "[emergence][integration]") {
    // political_cycle moves legitimacy toward the target at alpha per tick, so at any
    // moment it is a weighted mean of past targets: all but (1 - alpha)^N of the
    // weight lies on the last N ticks. It must therefore sit inside the range the
    // target spanned over those ticks, widened by that remainder. Nothing here is
    // tuned: alpha is the module's, N is a quarter.
    const PoliticalCycleConfig cfg{};
    constexpr size_t kWindow = 90;
    const double tail =
        std::pow(1.0 - static_cast<double>(cfg.legitimacy_ema_alpha), static_cast<double>(kWindow));
    for (size_t i = 0; i < std::size(kLegitimacySeeds); ++i) {
        for (const auto* run : {&legitimacy_runs()[i].first, &legitimacy_runs()[i].second}) {
            const auto& last = run->years.back();
            REQUIRE(run->targets.size() >= kWindow);
            REQUIRE(!last.nations.empty());
            for (size_t n = 0; n < last.nations.size(); ++n) {
                double lo = 1.0, hi = 0.0;
                for (size_t t = run->targets.size() - kWindow; t < run->targets.size(); ++t) {
                    lo = std::min(lo, run->targets[t][n]);
                    hi = std::max(hi, run->targets[t][n]);
                }
                INFO("seed " << kLegitimacySeeds[i] << " nation " << n << " legitimacy "
                             << last.nations[n].legitimacy << " target range [" << lo << ", " << hi
                             << "]");
                CHECK(last.nations[n].legitimacy >= lo - tail);
                CHECK(last.nations[n].legitimacy <= hi + tail);
            }
        }
    }
}

TEST_CASE("emergence: a society nothing strikes keeps its consent", "[emergence][integration]") {
    const PoliticalCycleConfig cfg{};
    for (size_t i = 0; i < std::size(kLegitimacySeeds); ++i) {
        const auto& last = legitimacy_runs()[i].second.years.back();
        REQUIRE(last.mean_grievance < 0.5);  // not a grievance-driven collapse
        for (size_t n = 0; n < last.nations.size(); ++n) {
            INFO("seed " << kLegitimacySeeds[i] << " nation " << n);
            CHECK(last.nations[n].legitimacy >
                  static_cast<double>(cfg.legitimacy_crisis_threshold));
        }
    }
}

TEST_CASE("emergence: shocks lower legitimacy through the channels they act on",
          "[emergence][integration]") {
    // Random events strike stability (and infrastructure, which feeds it) and raise
    // grievance; they leave institutional trust and unemployment alone. So the
    // legitimacy they cost, measured against the same world with the roll off, must
    // show up in the target's stability and grievance terms, and those terms must
    // carry more of the fall than the two the events do not touch.
    for (size_t i = 0; i < std::size(kLegitimacySeeds); ++i) {
        const auto& on = legitimacy_runs()[i].first.years.back();
        const auto& off = legitimacy_runs()[i].second.years.back();
        REQUIRE(on.nations.size() == off.nations.size());
        for (size_t n = 0; n < on.nations.size(); ++n) {
            const auto& a = on.nations[n];
            const auto& b = off.nations[n];
            const double struck =
                (b.stability_term - a.stability_term) + (b.grievance_term - a.grievance_term);
            const double untouched =
                (b.trust_term - a.trust_term) + (b.unemployment_term - a.unemployment_term);
            INFO("seed " << kLegitimacySeeds[i] << " nation " << n << ": legitimacy "
                         << a.legitimacy << " with events, " << b.legitimacy
                         << " without; stability+grievance carry " << struck
                         << ", trust+unemployment " << untouched);
            CHECK(a.legitimacy < b.legitimacy);
            CHECK(struck > 0.0);
            CHECK(struck > std::abs(untouched));
        }
    }
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

// NOTE: the former "criminal justice loop closes" ratchet is now a dedicated
// fast-gate integration test ("Criminal justice loop closes: detection to
// imprisonment" in criminal_subsystem_integration_test.cpp), which proves the
// detection→raid→conviction→IMPRISONMENT→parole lifecycle end-to-end. The break
// was raid cases seeded at `moderate` severity (below the custodial floor), so
// convictions only ever fined. The meter needs ~a year to reach the raid
// threshold, so a longer horizon than this suite's 3y window is the right place
// to assert it.

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
          "[emergence][integration]") {
    // Promoted from a ratchet. crime_rate was criminal-NPC count / full
    // demographic population — off by the sampling ratio (~300 tracked NPCs vs
    // ~500k people), reading ~1e-5 despite a persistent criminal presence. Now
    // it is the criminal fraction of the tracked-actor sample, so it reflects
    // the actual ~10% criminal baseline.
    const auto& s = baseline();
    REQUIRE(s.back().criminals > 0);
    CHECK(s.back().mean_crime > 0.001);
}
