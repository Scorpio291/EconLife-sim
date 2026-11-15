// EL-SLICE-N2 — diagnostic observer for NPC activity (NOT an assertion test).
//
// Runs the game's world (create_game_session, strict content policy, shipped
// package config) for seeds 42, 43 and 44 at 6 provinces and 500 significant
// NPCs and prints, at fixed sample ticks:
//   - NPC status counts (active / waiting / imprisoned / dead / fled);
//   - the daily-decision picture: how many present NPCs have an affordable
//     action that clears the inaction gate, quantiles of the best action's
//     relative utility (net utility / the NPC's largest motivation weight),
//     and which action is best (recomputed with the module's own candidate
//     generator, so it is the module's decision, not a copy of it);
//   - employment: NPCs holding a formal employment record, mean unemployment
//     rate and mean formal employment rate;
//   - mean grievance, mean / min stability and provinces at stability 0;
//   - criminal justice: investigator cases formally opened, legal cases,
//     cases that reached custody, NPCs imprisoned now.
//
// No targets are asserted. Hidden from every gate. From the repository root:
//   econlife_emergence_tests "[.el-slice-n2-observe]"
// Environment:
//   EL_YEARS   horizon in years (default 1)
//   EL_SEEDS   comma-separated seeds (default 42,43,44)
//   EL_NPCS    significant NPCs (default 500)
//   EL_BUDGET_S  wall-time budget per seed in seconds (default 0 = none); a run
//                that hits it prints the tick it reached and says so

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/labor_market/labor_market_module.h"
#include "modules/legal_process/legal_process_module.h"
#include "modules/npc_behavior/npc_behavior_module.h"
#include "tests/integration/emergence_harness.h"

using namespace econlife;
using namespace econlife::emergence;

namespace {

uint32_t n2_env_u32(const char* name, uint32_t def) {
    const char* v = std::getenv(name);
    return v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : def;
}

std::vector<uint64_t> n2_env_seeds() {
    const char* v = std::getenv("EL_SEEDS");
    std::vector<uint64_t> out;
    std::stringstream ss(v ? v : "42,43,44");
    std::string tok;
    while (std::getline(ss, tok, ','))
        if (!tok.empty())
            out.push_back(std::strtoull(tok.c_str(), nullptr, 10));
    return out;
}

template <typename T>
T* n2_find_module(const TickOrchestrator& orch) {
    for (const auto& m : orch.modules())
        if (auto* p = dynamic_cast<T*>(m.get()))
            return p;
    return nullptr;
}

const char* action_name(DailyAction a) {
    switch (a) {
        case DailyAction::work:
            return "work";
        case DailyAction::shop:
            return "shop";
        case DailyAction::socialize:
            return "soc";
        case DailyAction::rest:
            return "rest";
        case DailyAction::migrate:
            return "migr";
        case DailyAction::seek_employment:
            return "seek";
        case DailyAction::criminal_activity:
            return "crim";
        case DailyAction::whistleblow:
            return "whsl";
    }
    return "?";
}

bool present(NPCStatus s) {
    return s == NPCStatus::active || s == NPCStatus::waiting;
}

void print_row(const WorldState& w, const TickOrchestrator& orch, bool& ever_custodial) {
    int st[5] = {0, 0, 0, 0, 0};
    for (const auto& n : w.significant_npcs)
        st[static_cast<int>(n.status)]++;

    // Decision picture from the module's own generator.
    const auto* beh = n2_find_module<NpcBehaviorModule>(orch);
    std::vector<float> best;
    std::array<int, 8> best_action{};
    int above = 0;
    if (beh) {
        for (uint32_t p = 0; p < w.provinces.size(); ++p) {
            const auto ctx = beh->province_context(w, p);
            if (p >= w.npc_indices_by_province.size())
                continue;
            for (uint32_t idx : w.npc_indices_by_province[p]) {
                const NPC& npc = w.significant_npcs[idx];
                if (!present(npc.status))
                    continue;
                const auto choice = beh->choose_action(npc, beh->generate_candidates(npc, w, ctx));
                best.push_back(NpcBehaviorModule::relative_utility(npc, choice.best.net_utility));
                best_action[static_cast<size_t>(choice.best.action)]++;
                if (beh->chooses_to_act(npc, choice))
                    ++above;
            }
        }
    }
    std::sort(best.begin(), best.end());
    auto q = [&](double f) -> double {
        if (best.empty())
            return 0.0;
        return static_cast<double>(
            best[static_cast<size_t>(f * static_cast<double>(best.size() - 1))]);
    };

    // Employment.
    int formal = 0;
    if (const auto* lab = n2_find_module<LaborMarketModule>(orch)) {
        for (const auto& r : lab->employment_records()) {
            if (r.employer_business_id == 0)
                continue;
            for (const auto& n : w.significant_npcs)
                if (n.id == r.npc_id && present(n.status)) {
                    ++formal;
                    break;
                }
        }
    }
    const Snapshot s = capture(w);
    float smin = 1.0f;
    int at_zero = 0;
    for (float v : s.province_stability) {
        smin = std::min(smin, v);
        if (v <= 0.0f)
            ++at_zero;
    }

    // Criminal justice.
    int opened = 0, legal = 0, custodial_now = 0;
    if (const auto* inv = n2_find_module<InvestigatorEngineModule>(orch))
        for (const auto& c : inv->cases())
            if (c.formally_opened)
                ++opened;
    if (const auto* lp = n2_find_module<LegalProcessModule>(orch))
        for (const auto& c : lp->cases()) {
            ++legal;
            if (c.stage == LegalCaseStage::imprisoned || c.stage == LegalCaseStage::paroled) {
                ++custodial_now;
                ever_custodial = true;
            }
        }

    std::printf("%6u | %4d %4d %3d %3d %3d | %4d  %.3f %.3f %.3f  ", w.current_tick, st[0], st[4],
                st[1], st[2], st[3], above, q(0.1), q(0.5), q(0.9));
    for (size_t a = 0; a < 8; ++a)
        if (best_action[a])
            std::printf("%s:%d ", action_name(static_cast<DailyAction>(a)), best_action[a]);
    std::printf("| %4d %.3f %.3f | %.3f | %.3f/%.3f %d/6 | %4d %4d %3d %s\n", formal,
                s.mean_unemployment, s.formal_employment, s.mean_grievance, s.mean_stability,
                static_cast<double>(smin), at_zero, opened, legal, custodial_now,
                ever_custodial ? "custody-reached" : "");
}

}  // namespace

TEST_CASE("EL-SLICE-N2 NPC activity observer", "[.el-slice-n2-observe]") {
    const uint32_t years = n2_env_u32("EL_YEARS", 1);
    const uint32_t npcs = n2_env_u32("EL_NPCS", 500);
    const std::set<uint32_t> samples = {0, 1, 2, 7, 30, 90, 180};
    for (uint64_t seed : n2_env_seeds()) {
        RunSpec spec;
        spec.seed = seed;
        spec.npc_count = npcs;
        auto session = create_game_session(make_setup(spec));
        WorldState& w = session->world;
        std::printf("\n=== EL-SLICE-N2 observer — GAME WORLD, seed %llu, %u NPCs ===\n",
                    static_cast<unsigned long long>(seed), npcs);
        std::printf("%s", format_bootstrap_report(session->report).c_str());
        std::printf(
            "  tick |  act wait imp dead fled | acts  rel p10   p50   p90  best-action | "
            "formal unemp formal_rate | griev | stab mean/min @0 | invOpen legal custody\n");
        ThreadPool pool(1);
        bool ever_custodial = false;
        const auto t0 = std::chrono::steady_clock::now();
        print_row(w, session->orchestrator, ever_custodial);
        const double budget = static_cast<double>(n2_env_u32("EL_BUDGET_S", 0));
        for (uint32_t t = 1; t <= years * 365u; ++t) {
            session->orchestrator.execute_tick(w, pool);
            const double used =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const bool over = budget > 0.0 && used > budget;
            if (samples.count(t) || t % 365u == 0 || over)
                print_row(w, session->orchestrator, ever_custodial);
            if (over) {
                std::printf(
                    "  STOPPED on wall budget %.0f s at tick %u of %u (evidence pool %zu)\n",
                    budget, t, years * 365u, w.evidence_pool.size());
                break;
            }
        }
        std::printf("  evidence pool %zu\n", w.evidence_pool.size());
        std::printf("  wall %.1f s\n",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    SUCCEED();
}
