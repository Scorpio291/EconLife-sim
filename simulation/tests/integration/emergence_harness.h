#pragma once

// Shared harness for emergence/behavioral integration tests.
//
// Boots a V1-scale generated world, runs it through the real base-game tick
// orchestrator for N in-game years, and captures an annual time series of
// behavioral aggregates drawn ONLY from observable WorldState (module-private
// state like investigator cases is observed via its effects: NPC status,
// evidence pool, consequence queue, province conditions).
//
// Used by emergence_observe.cpp (dumps the series) and emergence_test.cpp
// (asserts behavioral invariants).
//
// THE WORLD IS THE GAME'S WORLD (EL-SLICE-01). The session is built by
// create_game_session() in modules/game_bootstrap.h — the same call econlife_cli
// makes — under ContentPolicy::strict: every catalogue and every package JSON
// must load, and a world with zero production facilities fails before tick 1.
// Until 2026-11-14 this harness loaded only the goods catalogue and no package
// config, so the suite ran a world with 0 recipes, 0 facilities and header-
// default module parameters (inaction_threshold 0.03 instead of the shipped
// 0.10). Only seed, NPC count and province count may differ from the game, plus
// the overrides a caller DECLARES (criminal baseline, forced regime, tick hook),
// which are recorded in the bootstrap report.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/game_bootstrap.h"
#include "modules/register_base_game_modules.h"
#include "modules/technology/technology_types.h"

namespace econlife::emergence {

struct Snapshot {
    uint32_t tick = 0;
    int active = 0, imprisoned = 0, dead = 0, fled = 0, waiting = 0;
    int criminals = 0, criminals_imprisoned = 0, criminals_dead = 0;
    double total_capital = 0.0, max_capital = 0.0;
    double max_legit_capital = 0.0;     // wealthiest non-criminal NPC (tax-exposed)
    double max_criminal_capital = 0.0;  // wealthiest criminal NPC (enforcement-exposed)
    int richest_role = -1;              // NPCRole of the wealthiest NPC
    bool richest_is_owner = false;      // does the richest NPC own a business?
    std::size_t evidence_pool = 0, consequence_queue = 0;
    std::size_t businesses = 0, criminal_businesses = 0;
    int criminal_biz_with_signal = 0;
    // Economic substrate diagnostics
    int insolvent_businesses = 0;  // cash < 0
    double max_business_cash = 0.0;
    double max_business_revenue = 0.0;
    bool richest_biz_criminal = false;   // is the highest-cash business criminal?
    double deferred_salary_total = 0.0;  // unpaid wages (wage-theft driver)
    int npcs_with_wage_theft_memory = 0;
    double mean_inequality = 0.0;
    int era = 0;
    double mean_stability = 0.0, mean_crime = 0.0, mean_gini = 0.0, mean_grievance = 0.0;
    double mean_dominance = 0.0, mean_unemployment = 0.0, total_population = 0.0;
    double formal_employment = 0.0;  // mean cohort formal_employment_rate
    // Community response side (what grievance is supposed to drive)
    double mean_cohesion = 0.0, mean_inst_trust = 0.0, mean_resource_access = 0.0;
    double mean_response_stage = 0.0;
    int max_response_stage = 0;
    // National governance (player's home nation = nations[0])
    double national_legitimacy = 0.5;
    int home_government_type = 0;
    double price_spread = 0.0;
    // World size as it stands at this tick (EL-SLICE-01 telemetry).
    std::size_t npcs = 0, provinces = 0, facilities = 0, facilities_staffed = 0;
    std::vector<float> province_stability;    // per province, index order
    std::vector<double> province_population;  // cohort total_population per province
};

inline bool is_criminal_role(NPCRole r) {
    return r == NPCRole::criminal_operator || r == NPCRole::criminal_enforcer ||
           r == NPCRole::fixer;
}

inline Snapshot capture(const WorldState& w) {
    Snapshot s;
    s.tick = w.current_tick;
    for (const auto& npc : w.significant_npcs) {
        switch (npc.status) {
            case NPCStatus::active:
                s.active++;
                break;
            case NPCStatus::imprisoned:
                s.imprisoned++;
                break;
            case NPCStatus::dead:
                s.dead++;
                break;
            case NPCStatus::fled:
                s.fled++;
                break;
            case NPCStatus::waiting:
                s.waiting++;
                break;
        }
        if (is_criminal_role(npc.role)) {
            s.criminals++;
            if (npc.status == NPCStatus::imprisoned)
                s.criminals_imprisoned++;
            if (npc.status == NPCStatus::dead)
                s.criminals_dead++;
        }
        s.total_capital += static_cast<double>(npc.capital);
        if (static_cast<double>(npc.capital) >= s.max_capital) {
            s.max_capital = static_cast<double>(npc.capital);
            s.richest_role = static_cast<int>(npc.role);
            s.richest_is_owner = false;
            for (const auto& b : w.npc_businesses)
                if (b.owner_id == npc.id) {
                    s.richest_is_owner = true;
                    break;
                }
        }
        if (is_criminal_role(npc.role))
            s.max_criminal_capital =
                std::max(s.max_criminal_capital, static_cast<double>(npc.capital));
        else
            s.max_legit_capital = std::max(s.max_legit_capital, static_cast<double>(npc.capital));
    }
    s.evidence_pool = w.evidence_pool.size();
    s.consequence_queue = w.consequence_queue.size();
    s.businesses = w.npc_businesses.size();
    for (const auto& b : w.npc_businesses) {
        if (b.criminal_sector) {
            s.criminal_businesses++;
            if (b.net_signal > 0.0f)
                s.criminal_biz_with_signal++;
        }
        if (b.cash < 0.0f)
            s.insolvent_businesses++;
        if (static_cast<double>(b.cash) >= s.max_business_cash) {
            s.max_business_cash = static_cast<double>(b.cash);
            s.richest_biz_criminal = b.criminal_sector;
        }
        s.max_business_revenue =
            std::max(s.max_business_revenue, static_cast<double>(b.revenue_per_tick));
        s.deferred_salary_total += static_cast<double>(b.deferred_salary_liability);
    }
    for (const auto& npc : w.significant_npcs) {
        for (const auto& m : npc.memory_log) {
            if (m.type == MemoryType::witnessed_wage_theft) {
                s.npcs_with_wage_theft_memory++;
                break;
            }
        }
    }
    s.era = static_cast<int>(w.technology.current_era);

    const std::size_t np = w.provinces.size();
    for (const auto& p : w.provinces) {
        s.mean_stability += static_cast<double>(p.conditions.stability_score);
        s.mean_gini += static_cast<double>(p.conditions.inequality_index);
        s.mean_grievance += static_cast<double>(p.community.grievance_level);
        s.mean_inequality += static_cast<double>(p.conditions.inequality_index);
        s.mean_cohesion += static_cast<double>(p.community.cohesion);
        s.mean_inst_trust += static_cast<double>(p.community.institutional_trust);
        s.mean_resource_access += static_cast<double>(p.community.resource_access);
        s.mean_response_stage += static_cast<double>(p.community.response_stage);
        s.max_response_stage =
            std::max(s.max_response_stage, static_cast<int>(p.community.response_stage));
        if (p.cohort_stats) {
            s.mean_crime += static_cast<double>(p.cohort_stats->crime_rate);
            s.mean_dominance += static_cast<double>(p.cohort_stats->criminal_dominance_index);
            s.mean_unemployment += static_cast<double>(p.cohort_stats->unemployment_rate);
            s.formal_employment += static_cast<double>(p.cohort_stats->formal_employment_rate);
            s.total_population += p.cohort_stats->total_population;
        }
    }
    if (np > 0) {
        s.mean_stability /= static_cast<double>(np);
        s.mean_crime /= static_cast<double>(np);
        s.mean_gini /= static_cast<double>(np);
        s.mean_grievance /= static_cast<double>(np);
        s.mean_inequality /= static_cast<double>(np);
        s.mean_dominance /= static_cast<double>(np);
        s.mean_unemployment /= static_cast<double>(np);
        s.formal_employment /= static_cast<double>(np);
        s.mean_cohesion /= static_cast<double>(np);
        s.mean_inst_trust /= static_cast<double>(np);
        s.mean_resource_access /= static_cast<double>(np);
        s.mean_response_stage /= static_cast<double>(np);
    }
    if (!w.nations.empty()) {
        s.national_legitimacy = w.nations[0].political_cycle.national_legitimacy;
        s.home_government_type = static_cast<int>(w.nations[0].government_type);
    }
    // good_id 1 is the first real catalog good (ids are 1-based; 0 is the
    // invalid/unknown sentinel). This samples one representative good's
    // cross-province price spread.
    double pmin = 1e30, pmax = -1e30;
    for (const auto& m : w.regional_markets) {
        if (m.good_id == 1) {
            pmin = std::min(pmin, static_cast<double>(m.spot_price));
            pmax = std::max(pmax, static_cast<double>(m.spot_price));
        }
    }
    s.price_spread = (pmax >= pmin) ? (pmax - pmin) : 0.0;

    s.npcs = w.significant_npcs.size();
    s.provinces = np;
    s.facilities = w.facilities.size();
    for (const auto& f : w.facilities)
        if (f.worker_count > 0)
            ++s.facilities_staffed;
    s.province_stability.reserve(np);
    s.province_population.reserve(np);
    for (const auto& p : w.provinces) {
        s.province_stability.push_back(p.conditions.stability_score);
        s.province_population.push_back(p.cohort_stats ? p.cohort_stats->total_population : 0.0);
    }
    return s;
}

using TickHook = std::function<void(WorldState&, uint32_t)>;

// criminal_baseline sentinel: keep the game's WorldGeneratorConfig value.
inline constexpr float kGameCriminalBaseline = -1.0f;

// What a run is asked to be. Everything not listed comes from the package.
struct RunSpec {
    uint64_t seed = 42;
    uint32_t npc_count = 500;
    uint32_t province_count = 6;
    uint32_t ticks = 365;
    // Snapshot cadence: always tick 0 and the final tick; additionally every
    // `snapshot_every` ticks (0 = off) and at each tick listed in `snapshot_at`.
    uint32_t snapshot_every = 365;
    std::vector<uint32_t> snapshot_at;
    // Declared scenario overrides (recorded in the bootstrap report).
    float criminal_baseline = kGameCriminalBaseline;
    int force_government_type = -1;
    TickHook post_tick{};
    // Stop early once the run has used this much wall time (0 = no limit).
    // The stop is recorded, never hidden.
    double wall_budget_seconds = 0.0;
    // Package root override (fail-fast tests point this at a broken copy).
    std::string package_root;
};

struct EmergenceRun {
    BootstrapReport bootstrap;
    std::vector<Snapshot> series;
    uint32_t ticks_completed = 0;
    double init_seconds = 0.0;
    double run_seconds = 0.0;
    bool stopped_on_budget = false;
};

inline GameSetup make_setup(const RunSpec& spec) {
    GameSetup setup{};
    setup.seed = spec.seed;
    setup.npc_count = spec.npc_count;
    setup.province_count = spec.province_count;
    setup.package_root = spec.package_root;
    setup.policy = ContentPolicy::strict;
    if (spec.criminal_baseline >= 0.0f) {
        const float cb = spec.criminal_baseline;
        setup.override_world_config = [cb](WorldGeneratorConfig& g) { g.criminal_baseline = cb; };
        setup.declared_overrides.push_back("criminal_baseline=" + std::to_string(cb));
    }
    if (spec.force_government_type >= 0) {
        const int gt = spec.force_government_type;
        setup.override_generated_world = [gt](WorldState& w) {
            for (auto& n : w.nations)
                n.government_type = static_cast<GovernmentType>(gt);
        };
        setup.declared_overrides.push_back("force_government_type=" + std::to_string(gt));
    }
    if (spec.post_tick)
        setup.declared_overrides.push_back("post_tick hook (exogenous shock)");
    return setup;
}

inline EmergenceRun run_world(const RunSpec& spec) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    std::unique_ptr<GameSession> session = create_game_session(make_setup(spec));
    const auto t1 = clock::now();

    EmergenceRun out;
    out.bootstrap = session->report;
    out.init_seconds = std::chrono::duration<double>(t1 - t0).count();

    WorldState& world = session->world;
    ThreadPool pool(1);
    const std::set<uint32_t> at(spec.snapshot_at.begin(), spec.snapshot_at.end());
    out.series.push_back(capture(world));
    for (uint32_t t = 1; t <= spec.ticks; ++t) {
        session->orchestrator.execute_tick(world, pool);
        if (spec.post_tick)
            spec.post_tick(world, t);
        out.ticks_completed = t;
        const bool budget_hit =
            spec.wall_budget_seconds > 0.0 &&
            std::chrono::duration<double>(clock::now() - t1).count() > spec.wall_budget_seconds;
        if (t == spec.ticks || at.count(t) != 0 ||
            (spec.snapshot_every != 0 && t % spec.snapshot_every == 0) || budget_hit)
            out.series.push_back(capture(world));
        if (budget_hit && t < spec.ticks) {
            out.stopped_on_budget = true;
            break;
        }
    }
    out.run_seconds = std::chrono::duration<double>(clock::now() - t1).count();
    return out;
}

// Run a freshly generated V1-scale world for `years` in-game years, capturing a
// snapshot at year 0 and after each year. Returns the time series.
// criminal_baseline: kGameCriminalBaseline keeps the game's value; anything else
// is a declared override.
// force_government_type: if >= 0, overrides every nation's government_type after
// generation (cast from GovernmentType) so a regime's unrest-response branch can
// be exercised regardless of what world-gen happened to pick.
// Post-tick hook: invoked after each execute_tick with the (mutable) world and the
// 1-based global tick index. Lets a scenario inject a persistent EXOGENOUS shock
// between ticks (e.g. a demand/revenue collapse) so the unrest cascade runs through
// the REAL modules — layoffs, memories, grievance grounding, escalation, regime
// response — rather than by writing grievance directly. Default: no-op.
inline std::vector<Snapshot> run_world_years(uint64_t seed, uint32_t npc_count, uint32_t years,
                                             float criminal_baseline = kGameCriminalBaseline,
                                             int force_government_type = -1,
                                             const TickHook& post_tick = {}) {
    RunSpec spec;
    spec.seed = seed;
    spec.npc_count = npc_count;
    spec.ticks = years * 365u;
    spec.snapshot_every = 365;
    spec.criminal_baseline = criminal_baseline;
    spec.force_government_type = force_government_type;
    spec.post_tick = post_tick;
    EmergenceRun run = run_world(spec);
    std::printf("%s", format_bootstrap_report(run.bootstrap).c_str());
    return std::move(run.series);
}

// A persistent economic-deprivation shock applied between ticks: during
// [onset_tick, end_tick) it forces every production facility to a fraction of its
// output rate, modelling a depression / supply collapse. This is the right lever
// because production READS facility.output_rate_modifier and never overwrites it
// (unlike revenue_per_tick, which production recomputes from output x price every
// tick), so the shock persists. Output collapse -> revenue collapse -> labor_market
// sheds workers it can no longer afford (distress layoffs -> employment_negative
// memories), which cuts worker_count -> output further (self-reinforcing), and
// businesses that cannot make payroll defer salary (wage-theft memories). The whole
// unrest cascade then runs through the REAL modules — nothing touches grievance,
// the response stage, or legitimacy directly.
struct DepressionShock {
    uint32_t onset_tick = 365;        // start of year 2 by default
    uint32_t end_tick = 0xFFFFFFFFu;  // persistent to end of run by default
    float output_retained = 0.05f;    // facilities run at this fraction of baseline
    void operator()(WorldState& world, uint32_t tick) const {
        if (tick < onset_tick || tick >= end_tick)
            return;
        for (auto& f : world.facilities)
            f.output_rate_modifier = output_retained;
    }
};

}  // namespace econlife::emergence
