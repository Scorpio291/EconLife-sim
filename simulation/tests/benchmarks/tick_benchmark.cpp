// Tick benchmark harness — measures per-tick execution time.
// Run with: ctest --test-dir build -R benchmark
//
// Performance contracts from INTERFACE specs:
//   - Full tick (27 steps): < 500ms at 2,000 NPCs (acceptable), < 200ms (target) on 6 cores
//   - DeltaBuffer merge: < 5ms at 2,000 NPCs
//   - Single DeferredWorkItem pop + execute: < 0.1ms average
//   - Deferred queue drain: < 20ms typical, < 50ms worst case
//   - RNG single call: < 10ns; fork: < 50ns
//   - Persistence serialize: < 2s; deserialize: < 5s

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>

#include "../test_world_factory.h"
#include "core/tick/drain_deferred_work.h"
#include "core/world_state/apply_deltas.h"
#include "modules/persistence/persistence_module.h"

using namespace econlife;
using namespace econlife::test;

// ── Full tick benchmark ─────────────────────────────────────────────────────

TEST_CASE("full tick performance at 2000 NPCs", "[benchmark][tick]") {
    auto world = create_test_world(42, 2000, 6, 15);
    TickOrchestrator orch;
    orch.finalize_registration();
    ThreadPool pool(6);

    BENCHMARK("full tick 2000 NPCs 6 provinces (no modules)") {
        orch.execute_tick(world, pool);
        return world.current_tick;
    };
}

// ── DeltaBuffer merge benchmark ─────────────────────────────────────────────

TEST_CASE("delta buffer merge performance at 2000 NPCs", "[benchmark][delta]") {
    auto world = create_test_world(42, 2000, 6, 15);

    BENCHMARK("apply_deltas with 2000 NPC deltas") {
        DeltaBuffer delta{};
        for (uint32_t i = 0; i < 2000; ++i) {
            NPCDelta nd{};
            nd.npc_id = 100 + i;
            nd.capital_delta = 1.0f;
            delta.npc_deltas.push_back(nd);
        }
        // Add market deltas for all 90 markets
        for (uint32_t g = 0; g < 15; ++g) {
            for (uint32_t p = 0; p < 6; ++p) {
                MarketDelta md{};
                md.good_id = g;
                md.region_id = p;
                md.supply_delta = 0.5f;
                delta.market_deltas.push_back(md);
            }
        }
        // Add region deltas
        for (uint32_t p = 0; p < 6; ++p) {
            RegionDelta rd{};
            rd.region_id = p;
            rd.stability_delta = -0.001f;
            rd.crime_rate_delta = 0.0005f;
            delta.region_deltas.push_back(rd);
        }
        apply_deltas(world, delta);
        return world.current_tick;
    };
}

// ── DeferredWorkQueue benchmark ─────────────────────────────────────────────

TEST_CASE("deferred work queue drain performance", "[benchmark][queue]") {
    BENCHMARK("push and drain 500 deferred items") {
        DeferredWorkQueue queue;
        for (uint32_t i = 0; i < 500; ++i) {
            DeferredWorkItem item{};
            item.due_tick = 1;
            item.type = WorkType::consequence;
            item.subject_id = i;
            item.payload = ConsequencePayload{i};
            queue.push(item);
        }
        // Drain all due at tick 1
        uint32_t count = 0;
        while (!queue.empty() && queue.top().due_tick <= 1) {
            queue.pop();
            ++count;
        }
        return count;
    };
}

// ── RNG benchmark ───────────────────────────────────────────────────────────

TEST_CASE("RNG call performance", "[benchmark][rng]") {
    BENCHMARK("1M next_u64 calls") {
        DeterministicRNG rng(42);
        uint64_t sum = 0;
        for (int i = 0; i < 1'000'000; ++i) {
            sum += rng.next_u64();
        }
        return sum;
    };
}

TEST_CASE("RNG fork performance", "[benchmark][rng]") {
    BENCHMARK("10000 forks") {
        DeterministicRNG rng(42);
        uint64_t sum = 0;
        for (uint32_t i = 0; i < 10'000; ++i) {
            auto fork = rng.fork(i);
            sum += fork.next_u64();
        }
        return sum;
    };
}

// ── Persistence serialization benchmark ──────────────────────────────────────

TEST_CASE("persistence serialize performance", "[benchmark][persistence]") {
    auto world = create_test_world(42, 2000, 6, 15);

    BENCHMARK("serialize 2000 NPC world") {
        return PersistenceModule::serialize(world);
    };
}

TEST_CASE("persistence deserialize performance", "[benchmark][persistence]") {
    auto world = create_test_world(42, 2000, 6, 15);
    auto bytes = PersistenceModule::serialize(world);

    BENCHMARK("deserialize 2000 NPC world") {
        WorldState restored{};
        auto result = PersistenceModule::deserialize(bytes, restored);
        return result;
    };
}

TEST_CASE("persistence round-trip performance", "[benchmark][persistence]") {
    auto world = create_test_world(42, 2000, 6, 15);

    BENCHMARK("serialize + deserialize 2000 NPC world") {
        auto bytes = PersistenceModule::serialize(world);
        WorldState restored{};
        PersistenceModule::deserialize(bytes, restored);
        return restored.current_tick;
    };
}

// ── Province-parallel scaling benchmark ─────────────────────────────────────

TEST_CASE("province parallel scaling 1 vs 6 cores", "[benchmark][parallel]") {
    auto world1 = create_test_world(42, 2000, 6, 15);
    auto world6 = create_test_world(42, 2000, 6, 15);

    TickOrchestrator orch1, orch6;
    orch1.finalize_registration();
    orch6.finalize_registration();
    ThreadPool pool1(1), pool6(6);

    BENCHMARK("30 ticks 1 thread (no modules)") {
        for (int i = 0; i < 30; ++i) {
            orch1.execute_tick(world1, pool1);
        }
        return world1.current_tick;
    };

    BENCHMARK("30 ticks 6 threads (no modules)") {
        for (int i = 0; i < 30; ++i) {
            orch6.execute_tick(world6, pool6);
        }
        return world6.current_tick;
    };
}
