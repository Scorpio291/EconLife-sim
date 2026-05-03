#include "core/tick/drain_deferred_work.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/tick/deferred_work.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// Helper: create a minimal WorldState for DWQ tests
static WorldState make_dwq_world() {
    WorldState w{};
    w.current_tick = 10;
    w.world_seed = 42;
    w.game_mode = GameMode::standard;
    w.lod2_price_index.reset();
    w.player.reset();

    Province p{};
    p.id = 0;
    p.region_id = 0;
    p.conditions = {0.7f, 0.3f, 0.1f, 0.05f, 0.05f, 0.7f, 0.8f, 1.0f, 1.0f};
    p.community = {0.6f, 0.2f, 0.6f, 0.5f, 0};
    w.provinces.push_back(p);

    NPC npc{};
    npc.id = 100;
    npc.status = NPCStatus::active;
    npc.capital = 10000.0f;
    npc.risk_tolerance = 0.5f;
    npc.motivations.weights = {0.2f, 0.15f, 0.15f, 0.05f, 0.05f, 0.1f, 0.2f, 0.1f};
    npc.home_province_id = 0;
    npc.current_province_id = 0;
    npc.travel_status = NPCTravelStatus::resident;
    w.significant_npcs.push_back(npc);

    return w;
}

TEST_CASE("drain: empty queue produces no changes", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    DeltaBuffer delta{};
    drain_deferred_work(w, delta);
    REQUIRE(w.deferred_work_queue.empty());
}

TEST_CASE("drain: items not yet due are left in queue", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 5;
    w.deferred_work_queue.push({10, WorkType::consequence, 1, EmptyPayload{}});
    w.deferred_work_queue.push({15, WorkType::consequence, 2, EmptyPayload{}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);

    REQUIRE(w.deferred_work_queue.size() == 2);
}

TEST_CASE("drain: items due at current tick are popped", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 10;
    w.deferred_work_queue.push({5, WorkType::consequence, 1, EmptyPayload{}});
    w.deferred_work_queue.push({10, WorkType::consequence, 2, EmptyPayload{}});
    w.deferred_work_queue.push({15, WorkType::consequence, 3, EmptyPayload{}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);

    // Only tick 15 should remain
    REQUIRE(w.deferred_work_queue.size() == 1);
    REQUIRE(w.deferred_work_queue.top().due_tick == 15);
}

TEST_CASE("drain: relationship decay reduces trust toward zero", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 30;

    // Give NPC a relationship with positive trust
    Relationship rel{};
    rel.target_npc_id = 200;
    rel.trust = 0.5f;
    rel.fear = 0.3f;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 0;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 1.0f;
    w.significant_npcs[0].relationships.push_back(rel);

    w.deferred_work_queue.push(
        {30, WorkType::npc_relationship_decay, 100, NPCRelationshipDecayPayload{100}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);
    apply_deltas(w, delta);

    // Trust should have decayed
    REQUIRE(w.significant_npcs[0].relationships[0].trust < 0.5f);
    // Fear should have decayed
    REQUIRE(w.significant_npcs[0].relationships[0].fear < 0.3f);
}

TEST_CASE("drain: relationship decay reschedules +30 ticks", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 30;

    w.deferred_work_queue.push(
        {30, WorkType::npc_relationship_decay, 100, NPCRelationshipDecayPayload{100}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);

    // Should have rescheduled
    REQUIRE(w.deferred_work_queue.size() == 1);
    REQUIRE(w.deferred_work_queue.top().due_tick == 60);  // 30 + 30
    REQUIRE(w.deferred_work_queue.top().type == WorkType::npc_relationship_decay);
}

TEST_CASE("drain: evidence decay reduces actionability", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 7;

    EvidenceToken token{};
    token.id = 42;
    token.type = EvidenceType::financial;
    token.actionability = 0.8f;
    token.decay_rate = 0.05f;  // per tick, applied over 7-tick interval
    token.is_active = true;
    w.evidence_pool.push_back(token);

    w.deferred_work_queue.push({7, WorkType::evidence_decay_batch, 42, EvidenceDecayPayload{42}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);
    apply_deltas(w, delta);

    // actionability should decrease: 0.8 - (0.05 * 7) = 0.45
    REQUIRE_THAT(w.evidence_pool[0].actionability, WithinAbs(0.45, 0.01));
    REQUIRE(w.evidence_pool[0].is_active == true);

    // Should have rescheduled
    REQUIRE(w.deferred_work_queue.size() == 1);
    REQUIRE(w.deferred_work_queue.top().due_tick == 14);  // 7 + 7
}

TEST_CASE("drain: evidence decay retires token when actionability near zero",
          "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 7;

    EvidenceToken token{};
    token.id = 42;
    token.type = EvidenceType::financial;
    token.actionability = 0.1f;  // will drop below 0.01
    token.decay_rate = 0.05f;    // 0.1 - (0.05 * 7) = -0.25, clamped to 0
    token.is_active = true;
    w.evidence_pool.push_back(token);

    w.deferred_work_queue.push({7, WorkType::evidence_decay_batch, 42, EvidenceDecayPayload{42}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);
    apply_deltas(w, delta);

    // Token should be retired
    REQUIRE(w.evidence_pool[0].is_active == false);
    REQUIRE(w.evidence_pool[0].actionability == 0.0f);

    // Should NOT reschedule (token is retired)
    REQUIRE(w.deferred_work_queue.empty());
}

TEST_CASE("drain: NPC travel arrival sets status to resident", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 10;
    w.significant_npcs[0].travel_status = NPCTravelStatus::in_transit;

    w.deferred_work_queue.push({10, WorkType::npc_travel_arrival, 100, EmptyPayload{}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);
    apply_deltas(w, delta);

    REQUIRE(w.significant_npcs[0].travel_status == NPCTravelStatus::resident);
    REQUIRE(w.deferred_work_queue.empty());
}

TEST_CASE("drain: invalid subject_id is silently skipped", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 10;

    // NPC 999 doesn't exist
    w.deferred_work_queue.push(
        {10, WorkType::npc_relationship_decay, 999, NPCRelationshipDecayPayload{999}});

    DeltaBuffer delta{};
    // Should not crash
    drain_deferred_work(w, delta);

    // Item was popped but no reschedule since NPC wasn't found
    REQUIRE(w.deferred_work_queue.empty());
}

TEST_CASE("drain: multiple items fire in due_tick order", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 20;

    // Add evidence tokens
    EvidenceToken t1{};
    t1.id = 1;
    t1.type = EvidenceType::physical;
    t1.actionability = 0.9f;
    t1.decay_rate = 0.01f;
    t1.is_active = true;
    w.evidence_pool.push_back(t1);

    EvidenceToken t2{};
    t2.id = 2;
    t2.type = EvidenceType::testimonial;
    t2.actionability = 0.8f;
    t2.decay_rate = 0.01f;
    t2.is_active = true;
    w.evidence_pool.push_back(t2);

    // Schedule both for different ticks, both <= current_tick
    w.deferred_work_queue.push({15, WorkType::evidence_decay_batch, 1, EvidenceDecayPayload{1}});
    w.deferred_work_queue.push({20, WorkType::evidence_decay_batch, 2, EvidenceDecayPayload{2}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);
    apply_deltas(w, delta);

    // Both should have been processed
    REQUIRE(w.evidence_pool[0].actionability < 0.9f);
    REQUIRE(w.evidence_pool[1].actionability < 0.8f);

    // Both rescheduled
    REQUIRE(w.deferred_work_queue.size() == 2);
}

TEST_CASE("drain: negative trust decays toward zero", "[drain_deferred_work][core]") {
    auto w = make_dwq_world();
    w.current_tick = 30;

    Relationship rel{};
    rel.target_npc_id = 200;
    rel.trust = -0.5f;
    rel.fear = 0.0f;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 0;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 1.0f;
    w.significant_npcs[0].relationships.push_back(rel);

    w.deferred_work_queue.push(
        {30, WorkType::npc_relationship_decay, 100, NPCRelationshipDecayPayload{100}});

    DeltaBuffer delta{};
    drain_deferred_work(w, delta);
    apply_deltas(w, delta);

    // Negative trust should move toward zero (increase)
    REQUIRE(w.significant_npcs[0].relationships[0].trust > -0.5f);
}
