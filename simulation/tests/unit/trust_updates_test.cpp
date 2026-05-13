#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"  // rebuild_npc_indices
#include "core/world_state/world_state.h"
#include "modules/trust_updates/trust_updates_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("TrustUpdates: positive delta applied", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.50f, 0.10f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(0.60f, 0.01f));
}

TEST_CASE("TrustUpdates: negative delta applied", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.50f, -0.20f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(0.30f, 0.01f));
}

TEST_CASE("TrustUpdates: trust clamped to upper bound", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.80f, 0.60f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("TrustUpdates: trust clamped to lower bound", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(-0.50f, -0.80f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(-1.0f, 0.01f));
}

TEST_CASE("TrustUpdates: trust gain capped by recovery ceiling", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.30f, 0.30f, 0.48f);
    REQUIRE_THAT(result, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("TrustUpdates: default ceiling allows full recovery", "[trust_updates][tier10]") {
    float result = TrustUpdatesModule::apply_trust_delta(0.70f, 0.20f, 1.0f);
    REQUIRE_THAT(result, WithinAbs(0.90f, 0.01f));
}

TEST_CASE("TrustUpdates: catastrophic loss sets ceiling", "[trust_updates][tier10]") {
    // Delta = -0.70, result = 0.10 (at floor)
    REQUIRE(TrustUpdatesModule::is_catastrophic_loss(-0.70f, 0.10f) == true);
    float ceiling = TrustUpdatesModule::compute_recovery_ceiling(0.80f);
    // max(0.80 * 0.60, 0.15) = 0.48
    REQUIRE_THAT(ceiling, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("TrustUpdates: second catastrophic loss lowers ceiling", "[trust_updates][tier10]") {
    float existing = 0.48f;
    float new_ceiling = TrustUpdatesModule::compute_recovery_ceiling(0.40f);
    // max(0.40 * 0.60, 0.15) = 0.24
    float final_ceiling = TrustUpdatesModule::update_recovery_ceiling(existing, new_ceiling);
    // min(0.48, 0.24) = 0.24
    REQUIRE_THAT(final_ceiling, WithinAbs(0.24f, 0.01f));
}

TEST_CASE("TrustUpdates: recovery ceiling minimum enforced", "[trust_updates][tier10]") {
    float ceiling = TrustUpdatesModule::compute_recovery_ceiling(0.10f);
    // max(0.10 * 0.60, 0.15) = max(0.06, 0.15) = 0.15
    REQUIRE_THAT(ceiling, WithinAbs(0.15f, 0.01f));
}

TEST_CASE("TrustUpdates: significant change detection", "[trust_updates][tier10]") {
    REQUIRE(TrustUpdatesModule::is_significant_change(0.15f) == true);
    REQUIRE(TrustUpdatesModule::is_significant_change(-0.12f) == true);
    REQUIRE(TrustUpdatesModule::is_significant_change(0.05f) == false);
    REQUIRE(TrustUpdatesModule::is_significant_change(-0.08f) == false);
}

TEST_CASE("TrustUpdates: non-catastrophic loss no ceiling change", "[trust_updates][tier10]") {
    // Delta = -0.30 (above -0.55 threshold), result = 0.50 (above floor)
    REQUIRE(TrustUpdatesModule::is_catastrophic_loss(-0.30f, 0.50f) == false);
}

TEST_CASE("TrustUpdates: negative delta does not trigger ceiling cap", "[trust_updates][tier10]") {
    // Recovery ceiling only caps gains, not losses
    float result = TrustUpdatesModule::apply_trust_delta(0.40f, -0.30f, 0.48f);
    REQUIRE_THAT(result, WithinAbs(0.10f, 0.01f));
}

TEST_CASE("TrustUpdates: config defaults match spec", "[trust_updates][tier10]") {
    constexpr TrustUpdatesConfig cfg{};
    REQUIRE_THAT(cfg.catastrophic_trust_loss_threshold, WithinAbs(-0.55f, 0.001f));
    REQUIRE_THAT(cfg.catastrophic_trust_floor, WithinAbs(0.10f, 0.001f));
    REQUIRE_THAT(cfg.recovery_ceiling_factor, WithinAbs(0.60f, 0.001f));
    REQUIRE_THAT(cfg.recovery_ceiling_minimum, WithinAbs(0.15f, 0.001f));
    REQUIRE_THAT(cfg.significant_change_threshold, WithinAbs(0.10f, 0.001f));
}

// --- Province dispatch (npc_indices_by_province) ---------------------------
//
// Verifies the migration from Province::significant_npc_ids → bucket index.
// Builds a two-province world, gives one NPC a same-tick memory whose
// emotional weight crosses the significant_change_threshold, runs
// execute_province on each province, and asserts:
//   1. The NPC produces a relationship + memory delta (its province only).
//   2. The other province produces nothing for that NPC.
//   3. NPCs whose current_province_id no longer matches their original
//      province (the migration case) are picked up by their new province
//      and ignored by their old one — proving the index is the source of
//      truth, not the stale Province::significant_npc_ids snapshot.

namespace {

WorldState make_two_province_world(uint32_t current_tick) {
    WorldState w{};
    w.current_tick = current_tick;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;

    Province p0{};
    p0.cohort_stats = std::make_unique<RegionCohortStats>();
    p0.id = 0;
    Province p1{};
    p1.cohort_stats = std::make_unique<RegionCohortStats>();
    p1.id = 1;
    w.provinces.push_back(p0);
    w.provinces.push_back(p1);
    return w;
}

NPC make_npc_with_relationship(uint32_t id, uint32_t province_id, uint32_t target_id,
                               uint32_t current_tick, float emotional_weight) {
    NPC npc{};
    npc.id = id;
    npc.current_province_id = province_id;
    npc.status = NPCStatus::active;

    Relationship rel{};
    rel.target_npc_id = target_id;
    rel.trust = 0.5f;
    rel.recovery_ceiling = 1.0f;
    npc.relationships.push_back(rel);

    MemoryEntry mem{};
    mem.tick_timestamp = current_tick;
    mem.subject_id = target_id;
    mem.emotional_weight = emotional_weight;  // *0.1f scale → must clear 0.10 threshold
    mem.decay = 1.0f;
    mem.type = MemoryType::interaction;
    npc.memory_log.push_back(mem);
    return npc;
}

}  // namespace

TEST_CASE("TrustUpdates: execute_province emits delta for NPC in bucket",
          "[trust_updates][tier10][npc_index]") {
    auto w = make_two_province_world(/*current_tick=*/100);
    w.significant_npcs.push_back(
        make_npc_with_relationship(/*id=*/10, /*province_id=*/0, /*target=*/20,
                                   /*current_tick=*/100, /*emotional_weight=*/2.0f));
    rebuild_npc_indices(w);

    TrustUpdatesModule module;
    DeltaBuffer delta_p0{};
    module.execute_province(0, w, delta_p0);
    REQUIRE(delta_p0.npc_deltas.size() == 1);
    REQUIRE(delta_p0.npc_deltas[0].npc_id == 10);
    REQUIRE(delta_p0.npc_deltas[0].updated_relationship.has_value());

    DeltaBuffer delta_p1{};
    module.execute_province(1, w, delta_p1);
    REQUIRE(delta_p1.npc_deltas.empty());
}

TEST_CASE("TrustUpdates: execute_province uses index, not Province::significant_npc_ids",
          "[trust_updates][tier10][npc_index]") {
    // NPC 10 lives in province 0 in significant_npcs but the (deliberately
    // stale) Province::significant_npc_ids on province 1 still claims it.
    // The migration MUST trust the bucket index — i.e. the live
    // current_province_id — and process the NPC under province 0 only.
    auto w = make_two_province_world(/*current_tick=*/100);
    w.significant_npcs.push_back(
        make_npc_with_relationship(/*id=*/10, /*province_id=*/0, /*target=*/20,
                                   /*current_tick=*/100, /*emotional_weight=*/2.0f));
    w.provinces[1].significant_npc_ids.push_back(10);  // stale snapshot
    rebuild_npc_indices(w);

    TrustUpdatesModule module;
    DeltaBuffer delta_p1{};
    module.execute_province(1, w, delta_p1);
    REQUIRE(delta_p1.npc_deltas.empty());

    DeltaBuffer delta_p0{};
    module.execute_province(0, w, delta_p0);
    REQUIRE(delta_p0.npc_deltas.size() == 1);
    REQUIRE(delta_p0.npc_deltas[0].npc_id == 10);
}

TEST_CASE("TrustUpdates: execute_province skips inactive NPCs",
          "[trust_updates][tier10][npc_index]") {
    auto w = make_two_province_world(/*current_tick=*/100);
    NPC dead = make_npc_with_relationship(/*id=*/10, /*province_id=*/0, /*target=*/20,
                                          /*current_tick=*/100, /*emotional_weight=*/2.0f);
    dead.status = NPCStatus::dead;
    w.significant_npcs.push_back(dead);
    rebuild_npc_indices(w);

    TrustUpdatesModule module;
    DeltaBuffer delta{};
    module.execute_province(0, w, delta);
    REQUIRE(delta.npc_deltas.empty());
}

TEST_CASE("TrustUpdates: execute_province processes NPCs in id ascending order",
          "[trust_updates][tier10][npc_index]") {
    // Insert into significant_npcs in non-monotonic id order; the module
    // must still emit deltas sorted by id.
    auto w = make_two_province_world(/*current_tick=*/100);
    w.significant_npcs.push_back(
        make_npc_with_relationship(/*id=*/30, /*province_id=*/0, /*target=*/40,
                                   /*current_tick=*/100, /*emotional_weight=*/2.0f));
    w.significant_npcs.push_back(
        make_npc_with_relationship(/*id=*/10, /*province_id=*/0, /*target=*/40,
                                   /*current_tick=*/100, /*emotional_weight=*/2.0f));
    w.significant_npcs.push_back(
        make_npc_with_relationship(/*id=*/20, /*province_id=*/0, /*target=*/40,
                                   /*current_tick=*/100, /*emotional_weight=*/2.0f));
    rebuild_npc_indices(w);

    TrustUpdatesModule module;
    DeltaBuffer delta{};
    module.execute_province(0, w, delta);
    REQUIRE(delta.npc_deltas.size() == 3);
    REQUIRE(delta.npc_deltas[0].npc_id == 10);
    REQUIRE(delta.npc_deltas[1].npc_id == 20);
    REQUIRE(delta.npc_deltas[2].npc_id == 30);
}
