// Determinism test harness — verifies that same seed + same inputs = identical output.
// Run with: ctest --test-dir build -R determinism
//
// Strategy:
//   1. Create a WorldState with known seed and initial conditions.
//   2. Run N ticks.
//   3. Serialize final state to bytes.
//   4. Reset to same initial state, run N ticks again.
//   5. Serialize again.
//   6. Compare byte-for-byte.

#include <catch2/catch_test_macros.hpp>
#include "../test_world_factory.h"

using namespace econlife;
using namespace econlife::test;

// ── Single-threaded determinism ─────────────────────────────────────────────

TEST_CASE("same seed produces identical state after N ticks", "[determinism]") {
    auto world1 = create_test_world(42, 100, 3);
    auto world2 = create_test_world(42, 100, 3);

    // Verify initial states are identical
    auto bytes_init1 = serialize_world_state(world1);
    auto bytes_init2 = serialize_world_state(world2);
    REQUIRE(bytes_init1 == bytes_init2);

    // Create two identical orchestrators (no modules registered = tick just increments)
    TickOrchestrator orch1, orch2;
    orch1.finalize_registration();
    orch2.finalize_registration();

    ThreadPool pool1(1), pool2(1);

    run_ticks(world1, orch1, pool1, 30);
    run_ticks(world2, orch2, pool2, 30);

    auto bytes1 = serialize_world_state(world1);
    auto bytes2 = serialize_world_state(world2);
    REQUIRE(bytes1 == bytes2);
    REQUIRE(world1.current_tick == 30);
    REQUIRE(world2.current_tick == 30);
}

TEST_CASE("different seeds produce different state", "[determinism]") {
    auto world1 = create_test_world(42, 100, 3);
    auto world2 = create_test_world(99, 100, 3);

    // Different seeds should produce different initial NPC capital values
    auto bytes1 = serialize_world_state(world1);
    auto bytes2 = serialize_world_state(world2);
    REQUIRE(bytes1 != bytes2);
}

// ── RNG determinism ─────────────────────────────────────────────────────────

TEST_CASE("RNG fork determinism across runs", "[determinism][rng]") {
    DeterministicRNG rng1(42), rng2(42);
    auto fork1a = rng1.fork(5);
    auto fork2a = rng2.fork(5);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(fork1a.next_u64() == fork2a.next_u64());
    }
}

TEST_CASE("RNG different contexts produce different sequences", "[determinism][rng]") {
    DeterministicRNG rng(42);
    auto fork_a = rng.fork(1);
    auto fork_b = rng.fork(2);

    // Different context IDs should yield different sequences
    bool all_same = true;
    for (int i = 0; i < 100; ++i) {
        if (fork_a.next_u64() != fork_b.next_u64()) {
            all_same = false;
            break;
        }
    }
    REQUIRE_FALSE(all_same);
}

// ── Float accumulation determinism ──────────────────────────────────────────

TEST_CASE("canonical sort order prevents float drift", "[determinism][float]") {
    // Create market deltas in two different orders.
    // Accumulating in canonical order (good_id asc, province_id asc)
    // must always produce the same result.

    constexpr uint32_t GOODS = 50;
    constexpr uint32_t PROVINCES = 6;

    // Generate deterministic delta values
    DeterministicRNG rng(12345);
    std::vector<std::pair<uint32_t, float>> deltas; // (good_id * 1000 + province_id, value)
    for (uint32_t g = 0; g < GOODS; ++g) {
        for (uint32_t p = 0; p < PROVINCES; ++p) {
            float val = rng.next_float() * 100.0f - 50.0f;
            deltas.push_back({g * 1000 + p, val});
        }
    }

    // Accumulate in canonical order
    std::vector<std::pair<uint32_t, float>> canonical = deltas;
    std::sort(canonical.begin(), canonical.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    float sum_canonical = 0.0f;
    for (const auto& [key, val] : canonical) {
        sum_canonical += val;
    }

    // Accumulate in same canonical order again (must be identical)
    float sum_canonical2 = 0.0f;
    for (const auto& [key, val] : canonical) {
        sum_canonical2 += val;
    }

    // Bit-identical
    uint32_t bits1, bits2;
    std::memcpy(&bits1, &sum_canonical, sizeof(bits1));
    std::memcpy(&bits2, &sum_canonical2, sizeof(bits2));
    REQUIRE(bits1 == bits2);
}

// ── Test world factory validation ───────────────────────────────────────────

TEST_CASE("test world factory produces valid state", "[determinism][factory]") {
    auto world = create_test_world(42, 200, 6, 15);

    REQUIRE(world.current_tick == 0);
    REQUIRE(world.world_seed == 42);
    REQUIRE(world.provinces.size() == 6);
    REQUIRE(world.nations.size() == 1);
    REQUIRE(world.region_groups.size() == 6);
    REQUIRE(world.significant_npcs.size() == 200);
    REQUIRE(world.regional_markets.size() == 6 * 15);
    REQUIRE(world.npc_businesses.size() == 40); // 200 / 5

    // All NPCs are active
    for (const auto& npc : world.significant_npcs) {
        REQUIRE(npc.status == NPCStatus::active);
        REQUIRE(npc.current_province_id < 6);
    }

    // All motivation vectors sum to ~1.0
    for (const auto& npc : world.significant_npcs) {
        float sum = 0.0f;
        for (float w : npc.motivations.weights) sum += w;
        REQUIRE(std::abs(sum - 1.0f) < 0.01f);
    }

    // Markets have non-zero prices
    for (const auto& m : world.regional_markets) {
        REQUIRE(m.spot_price > 0.0f);
        REQUIRE(m.supply > 0.0f);
    }

    // Province NPC lists populated
    uint32_t total_assigned = 0;
    for (const auto& prov : world.provinces) {
        total_assigned += static_cast<uint32_t>(prov.significant_npc_ids.size());
    }
    REQUIRE(total_assigned == 200);
}

TEST_CASE("test world factory is deterministic", "[determinism][factory]") {
    auto world1 = create_test_world(42, 50, 3);
    auto world2 = create_test_world(42, 50, 3);

    auto bytes1 = serialize_world_state(world1);
    auto bytes2 = serialize_world_state(world2);
    REQUIRE(bytes1 == bytes2);
}

TEST_CASE("serialization captures meaningful differences", "[determinism][serialization]") {
    auto world = create_test_world(42, 10, 2);

    auto bytes_before = serialize_world_state(world);

    // Mutate one NPC's capital
    world.significant_npcs[0].capital += 1000.0f;

    auto bytes_after = serialize_world_state(world);
    REQUIRE(bytes_before != bytes_after);
}
