// EL-SLICE-N2 — integration checks for the `waiting` contract in the game's
// world (create_game_session, strict policy, shipped content and config).
//
// Module-level contract tests are in tests/unit/npc_waiting_contract_test.cpp.
// The criminal justice loop (contract item 5) is asserted by
// "Criminal justice loop closes: detection to imprisonment" in
// criminal_subsystem_integration_test.cpp. This file adds:
//   - determinism: the same seed and configuration give the same NPC status
//     history and world fingerprint through a run with status transitions;
//   - every present law-enforcement NPC is a registered investigator in the
//     game world, whatever it chose to do today.
// No activity percentage is asserted anywhere.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "core/tick/thread_pool.h"
#include "modules/game_bootstrap.h"
#include "modules/investigator_engine/investigator_engine_module.h"
#include "tests/integration/world_fingerprint.h"

using namespace econlife;

namespace {

std::unique_ptr<GameSession> n2_session(uint64_t seed, uint32_t npcs) {
    GameSetup setup{};
    setup.seed = seed;
    setup.npc_count = npcs;
    setup.province_count = 6;
    setup.policy = ContentPolicy::strict;
    return create_game_session(setup);
}

// FNV-1a over (tick, id, status) for every NPC: one number per tick that
// changes if any NPC's status differs.
uint64_t status_hash(const WorldState& w) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xffu;
            h *= 1099511628211ull;
        }
    };
    mix(w.current_tick);
    for (const auto& n : w.significant_npcs) {
        mix(n.id);
        mix(static_cast<uint64_t>(n.status));
    }
    return h;
}

}  // namespace

TEST_CASE("EL-SLICE-N2: same seed and config give the same status history and world",
          "[integration][n2]") {
    constexpr uint32_t kTicks = 60;
    auto a = n2_session(42, 200);
    auto b = n2_session(42, 200);
    ThreadPool pool(1);

    std::vector<uint8_t> prev;
    for (const auto& n : a->world.significant_npcs)
        prev.push_back(static_cast<uint8_t>(n.status));
    int transitions = 0;

    for (uint32_t t = 1; t <= kTicks; ++t) {
        a->orchestrator.execute_tick(a->world, pool);
        b->orchestrator.execute_tick(b->world, pool);
        REQUIRE(status_hash(a->world) == status_hash(b->world));
        for (std::size_t i = 0; i < a->world.significant_npcs.size(); ++i) {
            const auto s = static_cast<uint8_t>(a->world.significant_npcs[i].status);
            if (i < prev.size() && s != prev[i])
                ++transitions;
            if (i < prev.size())
                prev[i] = s;
        }
    }
    REQUIRE(fingerprint::take(a->world) == fingerprint::take(b->world));
    // The run must exercise status changes, or it says nothing about their
    // determinism. (Count reported, not targeted.)
    INFO("status transitions over " << kTicks << " ticks: " << transitions);
    CHECK(transitions > 0);

    // A different seed is a different world (the fingerprint can tell).
    auto c = n2_session(43, 200);
    for (uint32_t t = 1; t <= 5; ++t)
        c->orchestrator.execute_tick(c->world, pool);
    CHECK(fingerprint::take(c->world) != fingerprint::take(a->world));
}

TEST_CASE("EL-SLICE-N2: every present law-enforcement NPC is a registered investigator",
          "[integration][n2]") {
    auto s = n2_session(42, 300);
    ThreadPool pool(1);
    for (uint32_t t = 1; t <= 3; ++t)
        s->orchestrator.execute_tick(s->world, pool);

    const InvestigatorEngineModule* inv = nullptr;
    for (const auto& m : s->orchestrator.modules())
        if (auto* p = dynamic_cast<const InvestigatorEngineModule*>(m.get()))
            inv = p;
    REQUIRE(inv != nullptr);
    std::set<uint32_t> registered;
    for (const auto& c : inv->cases())
        registered.insert(c.investigator_npc_id);

    int le_present = 0;
    for (const auto& n : s->world.significant_npcs) {
        if (n.role != NPCRole::law_enforcement || !is_present(n.status))
            continue;
        ++le_present;
        INFO("LE NPC " << n.id << " status " << static_cast<int>(n.status));
        CHECK(registered.count(n.id) == 1);
    }
    REQUIRE(le_present > 0);
}
