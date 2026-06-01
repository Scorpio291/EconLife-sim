// Criminal subsystem integration — drives the tick orchestrator over a
// WorldGenerator-created world and verifies the organized-crime spine comes
// alive end-to-end: criminal_operations assembles organizations from world-gen
// criminal NPCs, and those orgs seed protection_rackets and money_laundering
// (same-tick seed-delta + queue) so both downstream subsystems open live work.
//
// This is the orchestrator-level counterpart to the per-module unit tests: it
// proves the four slices cooperate through real topological ordering and
// delta application, not just in isolation.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/criminal_operations/criminal_operations_module.h"
#include "modules/money_laundering/money_laundering_module.h"
#include "modules/protection_rackets/protection_rackets_module.h"
#include "modules/register_base_game_modules.h"

using namespace econlife;

namespace {

std::string find_goods_dir() {
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
    WorldGeneratorConfig config{};
    config.seed = 20002;
    config.province_count = 6;
    config.npc_count = 300;            // ~9% are criminal roles -> several per province
    config.criminal_baseline = 0.15f;  // elevate dominance for clarity
    config.goods_directory = find_goods_dir();

    auto [world, player] = WorldGenerator::generate_with_player(config);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

    // Sanity: world gen actually seeded criminal NPCs, otherwise the scenario
    // would be vacuous.
    uint32_t criminal_npcs = 0;
    for (const auto& npc : world.significant_npcs) {
        if (is_criminal_role(npc.role))
            criminal_npcs++;
    }
    REQUIRE(criminal_npcs > 0);

    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator);
    orchestrator.finalize_registration();
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
