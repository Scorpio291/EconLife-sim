// Index benchmarks — measures cost of rebuild_npc_indices() and the per-call
// lookup helpers (lookup_npc_by_id, lookup_market, markets_in_province).
//
// These run ~27× per tick (one rebuild after every apply_deltas) and are on
// every module's hot path. They are currently hidden inside the full-tick
// `[contract]` measurement; a 5× regression here would show as ~5–10% drift
// in the full-tick number, buried in module noise. Isolating them gives a
// clean signal per-PR.
//
// Not gated in CI today; the `[contract]` benchmark continues to be the
// single gated number. Tag: [benchmark][index].

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "../test_world_factory.h"
#include "core/world_state/apply_deltas.h"

using namespace econlife;
using namespace econlife::test;

TEST_CASE("rebuild_npc_indices at 2000 NPCs / 6 provinces", "[benchmark][index]") {
    // V1 scale: 2000 significant NPCs, 6 provinces, 15 goods/province.
    // rebuild_npc_indices is called by the orchestrator after every
    // apply_deltas() (one per module + the drain). At 27 modules this is
    // 27 rebuilds per tick. Each rebuild is O(N + M).
    auto world = create_test_world(42, 2000, 6, 15);

    BENCHMARK("rebuild_npc_indices") {
        rebuild_npc_indices(world);
        return world.npc_index_by_id.size();
    };
}

TEST_CASE("lookup_npc_by_id O(1) path at 2000 NPCs", "[benchmark][index]") {
    auto world = create_test_world(42, 2000, 6, 15);
    // create_test_world already populates the indices, so this exercises
    // the fast hash-map path (not the fallback linear scan).
    const uint32_t target_id = world.significant_npcs[1500].id;

    BENCHMARK("lookup_npc_by_id (index hit)") {
        const NPC* npc = lookup_npc_by_id(world, target_id);
        return npc ? npc->id : 0u;
    };
}

TEST_CASE("lookup_market O(1) path at 1500 markets", "[benchmark][index]") {
    auto world = create_test_world(42, 2000, 6, 15);
    // Pick a market roughly in the middle of the vector.
    const auto& target = world.regional_markets[750];
    const uint32_t gid = target.good_id;
    const uint32_t pid = target.province_id;

    BENCHMARK("lookup_market (index hit)") {
        const RegionalMarket* m = lookup_market(world, gid, pid);
        return m ? m->good_id : 0u;
    };
}

TEST_CASE("markets_in_province bucket iteration at 6 provinces", "[benchmark][index]") {
    auto world = create_test_world(42, 2000, 6, 15);

    BENCHMARK("markets_in_province (bucket hit)") {
        const auto bucket = markets_in_province(world, 0);
        return bucket.size();
    };
}
