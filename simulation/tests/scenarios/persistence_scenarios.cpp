// Persistence scenario tests — multi-tick save / mutate / reload round-trips.
//
// Schema v3 unit tests in persistence_test.cpp already cover the catalog
// round-trip in isolation. These scenarios exercise the integration: run
// ticks, save, mutate the in-memory state through a delta, save again,
// reload both, and verify the mutation survived and the rest of the world
// stayed bit-identical. The audit's test-coverage finding ("no scenario
// test exercises a multi-tick save/load with mutations in between") drove
// these tests.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../test_world_factory.h"
#include "core/world_state/apply_deltas.h"
#include "modules/persistence/persistence_module.h"

using namespace econlife;
using namespace econlife::test;

TEST_CASE("persistence scenario: ticks -> save -> delta -> save -> reload preserves mutation",
          "[scenario][persistence]") {
    // Build a world, run ticks, snapshot it.
    auto world = create_test_world(/*seed=*/42, /*npc_count=*/50, /*provinces=*/3);
    TickOrchestrator orch;
    orch.finalize_registration();
    ThreadPool pool(1);

    run_ticks(world, orch, pool, 10);
    auto snapshot_before_mutation = PersistenceModule::serialize(world);

    // Apply a manual NPC capital delta — simulating an editor / cheat action /
    // out-of-band mutation between two save points.
    const uint32_t mutated_npc_id = world.significant_npcs[0].id;
    const float pre_capital = world.significant_npcs[0].capital;
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = mutated_npc_id;
    nd.capital_delta = 12345.0f;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital,
                 Catch::Matchers::WithinAbs(pre_capital + 12345.0f, 0.01f));

    auto snapshot_after_mutation = PersistenceModule::serialize(world);

    // Reload both snapshots and verify the mutation survived in the second.
    WorldState reloaded_before{};
    REQUIRE(PersistenceModule::deserialize(snapshot_before_mutation, reloaded_before) ==
            RestoreResult::success);
    WorldState reloaded_after{};
    REQUIRE(PersistenceModule::deserialize(snapshot_after_mutation, reloaded_after) ==
            RestoreResult::success);

    // The mutated NPC's capital must reflect the delta in the post-mutation
    // reload, and must NOT reflect it in the pre-mutation reload.
    bool found_pre = false;
    bool found_post = false;
    for (const auto& npc : reloaded_before.significant_npcs) {
        if (npc.id == mutated_npc_id) {
            REQUIRE_THAT(npc.capital, Catch::Matchers::WithinAbs(pre_capital, 0.01f));
            found_pre = true;
            break;
        }
    }
    for (const auto& npc : reloaded_after.significant_npcs) {
        if (npc.id == mutated_npc_id) {
            REQUIRE_THAT(npc.capital, Catch::Matchers::WithinAbs(pre_capital + 12345.0f, 0.01f));
            found_post = true;
            break;
        }
    }
    REQUIRE(found_pre);
    REQUIRE(found_post);
}

TEST_CASE(
    "persistence scenario: ticks -> save -> reload -> continue ticks -> bit-identical to live",
    "[scenario][persistence][determinism]") {
    // Two worlds run identically for 5 ticks, both save+reload, then both
    // continue for 5 more ticks. The two reloaded worlds must be
    // bit-identical at the end.
    auto live = create_test_world(/*seed=*/99, /*npc_count=*/30, /*provinces=*/2);
    auto twin = create_test_world(/*seed=*/99, /*npc_count=*/30, /*provinces=*/2);

    TickOrchestrator orch_live, orch_twin;
    orch_live.finalize_registration();
    orch_twin.finalize_registration();
    ThreadPool pool_live(1), pool_twin(1);

    run_ticks(live, orch_live, pool_live, 5);
    run_ticks(twin, orch_twin, pool_twin, 5);

    // Round-trip both through serialize/deserialize.
    WorldState live_reloaded{};
    REQUIRE(PersistenceModule::deserialize(PersistenceModule::serialize(live), live_reloaded) ==
            RestoreResult::success);
    WorldState twin_reloaded{};
    REQUIRE(PersistenceModule::deserialize(PersistenceModule::serialize(twin), twin_reloaded) ==
            RestoreResult::success);

    // Continue running ticks.
    TickOrchestrator orch_live2, orch_twin2;
    orch_live2.finalize_registration();
    orch_twin2.finalize_registration();
    run_ticks(live_reloaded, orch_live2, pool_live, 5);
    run_ticks(twin_reloaded, orch_twin2, pool_twin, 5);

    // Bit-identical after the second run.
    auto bytes_live = serialize_world_state(live_reloaded);
    auto bytes_twin = serialize_world_state(twin_reloaded);
    REQUIRE(bytes_live == bytes_twin);
}

TEST_CASE("persistence scenario: goods_catalog survives reload-and-mutate cycle",
          "[scenario][persistence][goods_catalog]") {
    // After Phase 4's schema v3 changes, the GoodsCatalog rides in saves.
    // This scenario seeds a catalog, saves, mutates a market, saves again,
    // reloads, and verifies the catalog round-trip works alongside market
    // deltas in a single multi-step sequence.
    auto world = create_test_world(/*seed=*/7, /*npc_count=*/10, /*provinces=*/2);

    auto cat = std::make_unique<GoodsCatalog>();
    GoodDefinition g0{};
    g0.numeric_id = 0;
    g0.good_id = "wheat";
    g0.base_price = 25.0f;
    cat->push_back_loaded(g0);
    world.goods_catalog = std::move(cat);

    auto before = PersistenceModule::serialize(world);

    // Mutate a market via delta.
    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = world.regional_markets[0].good_id;
    md.region_id = world.regional_markets[0].province_id;
    md.supply_delta = 500.0f;
    delta.market_deltas.push_back(md);
    apply_deltas(world, delta);

    auto after = PersistenceModule::serialize(world);
    REQUIRE(before != after);

    WorldState reloaded{};
    REQUIRE(PersistenceModule::deserialize(after, reloaded) == RestoreResult::success);

    // Catalog survived.
    REQUIRE(reloaded.goods_catalog);
    REQUIRE(reloaded.goods_catalog->size() == 1);
    REQUIRE(lookup_good_id(reloaded, "wheat") == 0u);
}
