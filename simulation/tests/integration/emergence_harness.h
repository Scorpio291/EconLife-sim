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

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/register_base_game_modules.h"
#include "modules/technology/technology_types.h"

namespace econlife::emergence {

inline std::string find_goods_dir() {
    namespace fs = std::filesystem;
    static const char* candidates[] = {
        "packages/base_game/goods",
        "../packages/base_game/goods",
        "../../packages/base_game/goods",
        "../../../packages/base_game/goods",
    };
    for (const auto* c : candidates) {
        if (fs::exists(c) && fs::is_directory(c))
            return fs::canonical(c).string();
    }
    return "";
}

struct Snapshot {
    uint32_t tick = 0;
    int active = 0, imprisoned = 0, dead = 0, fled = 0, waiting = 0;
    int criminals = 0, criminals_imprisoned = 0, criminals_dead = 0;
    double total_capital = 0.0, max_capital = 0.0;
    int richest_role = -1;          // NPCRole of the wealthiest NPC
    bool richest_is_owner = false;  // does the richest NPC own a business?
    std::size_t evidence_pool = 0, consequence_queue = 0;
    std::size_t businesses = 0, criminal_businesses = 0;
    int criminal_biz_with_signal = 0;
    // Economic substrate diagnostics
    int insolvent_businesses = 0;        // cash < 0
    double max_business_cash = 0.0;
    double max_business_revenue = 0.0;
    bool richest_biz_criminal = false;  // is the highest-cash business criminal?
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
        s.total_capital += npc.capital;
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
        s.deferred_salary_total += b.deferred_salary_liability;
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
        s.mean_stability += p.conditions.stability_score;
        s.mean_gini += p.conditions.inequality_index;
        s.mean_grievance += p.community.grievance_level;
        s.mean_inequality += p.conditions.inequality_index;
        s.mean_cohesion += p.community.cohesion;
        s.mean_inst_trust += p.community.institutional_trust;
        s.mean_resource_access += p.community.resource_access;
        s.mean_response_stage += p.community.response_stage;
        s.max_response_stage =
            std::max(s.max_response_stage, static_cast<int>(p.community.response_stage));
        if (p.cohort_stats) {
            s.mean_crime += p.cohort_stats->crime_rate;
            s.mean_dominance += p.cohort_stats->criminal_dominance_index;
            s.mean_unemployment += p.cohort_stats->unemployment_rate;
            s.formal_employment += p.cohort_stats->formal_employment_rate;
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
    double pmin = 1e30, pmax = -1e30;
    for (const auto& m : w.regional_markets) {
        if (m.good_id == 0) {
            pmin = std::min(pmin, static_cast<double>(m.spot_price));
            pmax = std::max(pmax, static_cast<double>(m.spot_price));
        }
    }
    s.price_spread = (pmax >= pmin) ? (pmax - pmin) : 0.0;
    return s;
}

// Run a freshly generated V1-scale world for `years` in-game years, capturing a
// snapshot at year 0 and after each year. Returns the time series.
// force_government_type: if >= 0, overrides every nation's government_type after
// generation (cast from GovernmentType) so a regime's unrest-response branch can
// be exercised regardless of what world-gen happened to pick.
inline std::vector<Snapshot> run_world_years(uint64_t seed, uint32_t npc_count, uint32_t years,
                                             float criminal_baseline = 0.10f,
                                             int force_government_type = -1) {
    WorldGeneratorConfig config{};
    config.seed = seed;
    config.province_count = 6;
    config.npc_count = npc_count;
    config.criminal_baseline = criminal_baseline;
    config.goods_directory = find_goods_dir();

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));
    if (force_government_type >= 0) {
        for (auto& n : world.nations)
            n.government_type = static_cast<GovernmentType>(force_government_type);
    }

    TickOrchestrator orch;
    register_base_game_modules(orch);
    orch.finalize_registration();
    ThreadPool pool(1);

    std::vector<Snapshot> series;
    series.reserve(years + 1);
    series.push_back(capture(world));
    for (uint32_t y = 0; y < years; ++y) {
        for (uint32_t t = 0; t < 365; ++t) {
            orch.execute_tick(world, pool);
        }
        series.push_back(capture(world));
    }
    return series;
}

}  // namespace econlife::emergence
