// Criminal subsystem integration — drives the tick orchestrator over a
// WorldGenerator-created world and verifies the organized-crime spine comes
// alive end-to-end: criminal_operations assembles organizations from world-gen
// criminal NPCs, and those orgs seed protection_rackets and money_laundering
// (same-tick seed-delta + queue) so both downstream subsystems open live work.
//
// This is the orchestrator-level counterpart to the per-module unit tests: it
// proves the four slices cooperate through real topological ordering and
// delta application, not just in isolation.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/criminal_operations/criminal_operations_module.h"
#include "modules/game_bootstrap.h"
#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/legal_process/legal_process_module.h"
#include "modules/money_laundering/money_laundering_module.h"
#include "modules/protection_rackets/protection_rackets_module.h"
#include "modules/register_base_game_modules.h"

using namespace econlife;

namespace {

// The game's session with the one declared override these scenarios need.
GameSetup criminal_setup(uint64_t seed, uint32_t npcs, float criminal_baseline) {
    GameSetup setup{};
    setup.seed = seed;
    setup.npc_count = npcs;
    setup.province_count = 6;
    setup.policy = ContentPolicy::strict;
    setup.override_world_config = [criminal_baseline](WorldGeneratorConfig& g) {
        g.criminal_baseline = criminal_baseline;
    };
    setup.declared_overrides.push_back("criminal_baseline=" + std::to_string(criminal_baseline));
    return setup;
}

template <typename T>
T* find_module(const TickOrchestrator& orch) {
    for (const auto& m : orch.modules()) {
        if (auto* p = dynamic_cast<T*>(m.get()))
            return p;
    }
    return nullptr;
}

bool is_criminal_role(NPCRole r) {
    return r == NPCRole::criminal_operator || r == NPCRole::criminal_enforcer ||
           r == NPCRole::fixer;
}

}  // namespace

TEST_CASE("Criminal subsystem: orgs form and seed rackets + laundering end-to-end",
          "[integration][criminal][orchestrator]") {
    // The game's world (EL-SLICE-01): shipped content and package config, strict
    // policy. Declared override: criminal baseline 0.15 to elevate dominance.
    auto session = create_game_session(criminal_setup(20002, 300, 0.15f));
    WorldState& world = session->world;

    // Sanity: world gen actually seeded criminal NPCs, otherwise the scenario
    // would be vacuous.
    uint32_t criminal_npcs = 0;
    for (const auto& npc : world.significant_npcs) {
        if (is_criminal_role(npc.role))
            criminal_npcs++;
    }
    REQUIRE(criminal_npcs > 0);

    TickOrchestrator& orchestrator = session->orchestrator;
    ThreadPool pool(1);

    // A handful of ticks is enough: formation + seeding happen on the first
    // criminal_operations execute, and the consumers drain the same tick.
    for (uint32_t t = 0; t < 5; ++t)
        orchestrator.execute_tick(world, pool);
    REQUIRE(world.current_tick == 5);

    auto* crimops = find_module<CriminalOperationsModule>(orchestrator);
    auto* rackets = find_module<ProtectionRacketsModule>(orchestrator);
    auto* laundering = find_module<MoneyLaunderingModule>(orchestrator);
    REQUIRE(crimops != nullptr);
    REQUIRE(rackets != nullptr);
    REQUIRE(laundering != nullptr);

    // 1) criminal_operations assembled organizations from world-gen criminals.
    REQUIRE_FALSE(crimops->organizations().empty());
    bool any_income = false;
    for (const auto& org : crimops->organizations()) {
        REQUIRE_FALSE(org.member_npc_ids.empty());         // real members
        REQUIRE_FALSE(org.dominance_by_province.empty());  // homed in a province
        REQUIRE(org.leadership_npc_id != 0);
        if (!org.income_source_ids.empty())
            any_income = true;
    }

    // 2) protection_rackets received racket seeds and opened live rackets
    //    (provinces hosting orgs also host legitimate businesses to extort).
    REQUIRE_FALSE(rackets->rackets().empty());
    for (const auto& r : rackets->rackets()) {
        REQUIRE(r.target_business_id != 0);
        REQUIRE(r.demand_per_tick >= 0.0f);
    }

    // 3) money_laundering opened operations for any org that has criminal-sector
    //    income. Gated on income existing so the assertion can't be vacuous if a
    //    given seed happens to produce no criminal-sector businesses.
    if (any_income) {
        REQUIRE_FALSE(laundering->operations().empty());
        for (const auto& op : laundering->operations()) {
            REQUIRE(op.dirty_amount > 0.0f);
        }
    }
}

// ── Criminal justice loop closes end-to-end ────────────────────────────────
// Drives the full orchestrator over a generated world and verifies the
// detection→prosecution→IMPRISONMENT loop actually closes: investigator meters
// fill from facility signals, raids seed legal cases, cases convict, and at
// least one criminal is imprisoned. Prints a per-year stage breakdown so a
// regression shows exactly where it re-stalls. (Was the open emergence ratchet;
// the break was raid cases seeded at `moderate` severity — below the custodial
// floor — so convictions only ever fined.)
// Ratchet history: tagged [!shouldfail] in EL-SLICE-01 (owner decision D1 = (b),
// 2026-11-15) because ~97% of NPCs went `waiting` on tick 1 in the game's world
// and every `status == active` filter then dropped law enforcement, so the loop
// recorded 0 cases. Removed in EL-SLICE-N2: `waiting` now means "took no
// discretionary daily action" and no longer removes an NPC from its role
// duties (core/world_state/npc.h). Expectations unchanged.
TEST_CASE("Criminal justice loop closes: detection to imprisonment",
          "[integration][criminal][justice]") {
    // The game's world (EL-SLICE-01): shipped content and package config, strict
    // policy. Declared override: criminal baseline 0.10 (the game uses 0.05).
    auto session = create_game_session(criminal_setup(42, 300, 0.10f));
    WorldState& world = session->world;
    TickOrchestrator& orch = session->orchestrator;
    ThreadPool pool(1);

    // Count LE/investigator NPCs and criminal businesses, and co-location.
    int le = 0, regs = 0, crim_biz = 0;
    for (const auto& n : world.significant_npcs)
        if (n.status == NPCStatus::active) {
            if (n.role == NPCRole::law_enforcement)
                le++;
            if (n.role == NPCRole::regulator)
                regs++;
        }
    for (const auto& b : world.npc_businesses)
        if (b.criminal_sector)
            crim_biz++;
    std::printf("\n=== CRIMINAL JUSTICE DIAG (seed 42, 300 NPCs) ===\n");
    std::printf("LE=%d regulators=%d criminal_businesses=%d\n", le, regs, crim_biz);
    REQUIRE(le > 0);
    REQUIRE(crim_biz > 0);

    int peak_imprisoned = 0;      // most NPCs simultaneously imprisoned
    bool ever_custodial = false;  // any case reached the imprisoned stage

    for (uint32_t y = 1; y <= 5; ++y) {
        for (uint32_t t = 0; t < 365; ++t)
            orch.execute_tick(world, pool);

        auto* inv = find_module<InvestigatorEngineModule>(orch);
        auto* legal = find_module<LegalProcessModule>(orch);
        float max_meter = 0.0f;
        int opened = 0, with_target = 0;
        if (inv)
            for (const auto& c : inv->cases()) {
                max_meter = std::max(max_meter, c.current_level);
                if (c.formally_opened)
                    opened++;
                if (c.target_id != 0)
                    with_target++;
            }
        int lc = legal ? static_cast<int>(legal->cases().size()) : -1;
        // Legal case stage distribution (0..9) + evidence range, and for any case
        // at the imprisoned stage, the defendant id and that NPC's actual status.
        int stage_ct[10] = {0};
        float ev_min = 1.0f, ev_max = 0.0f;
        std::string impr_detail;
        if (legal)
            for (const auto& c : legal->cases()) {
                int si = static_cast<int>(c.stage);
                if (si >= 0 && si < 10)
                    stage_ct[si]++;
                ev_min = std::min(ev_min, c.evidence_weight);
                ev_max = std::max(ev_max, c.evidence_weight);
                if (c.stage == LegalCaseStage::imprisoned || c.stage == LegalCaseStage::paroled)
                    ever_custodial = true;  // reached/served custody
                if (c.stage == LegalCaseStage::imprisoned) {
                    const char* st = "??";
                    for (const auto& n : world.significant_npcs)
                        if (n.id == c.defendant_npc_id) {
                            st = (n.status == NPCStatus::imprisoned) ? "IMPRISONED"
                                 : (n.status == NPCStatus::active)   ? "active"
                                 : (n.status == NPCStatus::dead)     ? "dead"
                                 : (n.status == NPCStatus::waiting)  ? "waiting"
                                                                     : "fled";
                        }
                    impr_detail += " def#" + std::to_string(c.defendant_npc_id) + "=" + st +
                                   "(rel@" + std::to_string(c.release_tick) + ")";
                }
            }
        int imprisoned = 0;
        for (const auto& n : world.significant_npcs)
            if (n.status == NPCStatus::imprisoned)
                imprisoned++;
        peak_imprisoned = std::max(peak_imprisoned, imprisoned);
        std::printf(
            "yr %u | legalCases=%d ev=[%.2f,%.2f] inv=%d arr=%d chg=%d trial=%d conv=%d acq=%d "
            "impr=%d fined=%d parol=%d pard=%d | NPC.impr=%d%s\n",
            y, lc, static_cast<double>(ev_min), static_cast<double>(ev_max), stage_ct[0],
            stage_ct[1], stage_ct[2], stage_ct[3], stage_ct[4], stage_ct[5], stage_ct[6],
            stage_ct[7], stage_ct[8], stage_ct[9], imprisoned, impr_detail.c_str());
    }
    std::printf("=== END DIAG ===\n\n");
    REQUIRE(world.current_tick == 5u * 365u);

    // The loop must close: a generated world with criminal businesses and law
    // enforcement must, over five years, put at least one criminal behind bars
    // (and the case lifecycle reaches/serves custody, not just convicts to a fine).
    CHECK(ever_custodial);
    CHECK(peak_imprisoned > 0);
}
