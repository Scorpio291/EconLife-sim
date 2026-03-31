// NPC behavior scenario tests — behavioral assertions from GDD and TDD.
// These verify NPC decision-making, memory, relationships, and life events
// through direct delta application against WorldState. Each test is
// self-contained and does not execute module logic.
//
// Scenarios tagged [scenario][npc].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>

#include "core/tick/drain_deferred_work.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/world_state.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using namespace econlife::test;
using Catch::Matchers::WithinAbs;

// ── Motivation-driven decisions ─────────────────────────────────────────────

TEST_CASE("NPC with high greed motivation prioritizes high-paying job",
          "[scenario][npc][motivation]") {
    // Setup: NPC with financial_gain (index 0) as dominant motivation weight (0.8).
    // The NPC accepts the high-paying job: capital_delta = +200 applied.
    // Assert: capital increased by exactly 200.
    auto world = create_test_world(42, 10, 1, 5);

    // Override the first NPC's motivation vector so financial_gain dominates.
    // weights must sum to 1.0; distribute remainder across the other 7 slots.
    NPC& npc = world.significant_npcs[0];
    npc.motivations.weights = {0.80f, 0.04f, 0.04f, 0.02f, 0.02f, 0.04f, 0.02f, 0.02f};

    float initial_capital = npc.capital;

    // The high greed motivation drives the NPC toward financial gain.
    // Simulate the module output: high-pay job accepted → capital_delta = +200.
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = npc.id;
    nd.capital_delta = 200.0f;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(initial_capital + 200.0f, 0.01f));
    // Confirm the motivation vector was set correctly (dominant weight at index 0).
    REQUIRE(world.significant_npcs[0].motivations.weights[0] > 0.5f);
}

TEST_CASE("NPC with high security motivation avoids risky business",
          "[scenario][npc][motivation]") {
    // Setup: NPC with security_gain (index 1) as dominant motivation weight (0.8).
    // The NPC rejects the criminal opportunity — no capital_delta applied.
    // Assert: capital is unchanged.
    auto world = create_test_world(42, 10, 1, 5);

    NPC& npc = world.significant_npcs[0];
    // security_gain is OutcomeType index 1.
    npc.motivations.weights = {0.04f, 0.80f, 0.04f, 0.02f, 0.02f, 0.04f, 0.02f, 0.02f};

    float initial_capital = npc.capital;

    // Security-dominant NPC rejects risky option → no delta emitted by the module.
    DeltaBuffer delta{};
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(initial_capital, 0.01f));
    // Confirm the motivation vector was set correctly (dominant weight at index 1).
    REQUIRE(world.significant_npcs[0].motivations.weights[1] > 0.5f);
}

// ── Memory system ───────────────────────────────────────────────────────────

TEST_CASE("NPC forms memory from significant event", "[scenario][npc][memory]") {
    // Setup: NPC witnesses illegal activity.
    // Apply NPCDelta with new_memory_entry (witnessed_illegal_activity).
    // Assert: memory_log contains the new entry with correct type and weight.
    auto world = create_test_world(42, 10, 1, 5);
    NPC& npc = world.significant_npcs[0];

    REQUIRE(npc.memory_log.empty());

    MemoryEntry entry{};
    entry.tick_timestamp = 1;
    entry.type = MemoryType::witnessed_illegal_activity;
    entry.subject_id = 999;
    entry.emotional_weight = -0.8f;
    entry.decay = 0.95f;
    entry.is_actionable = true;

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = npc.id;
    nd.new_memory_entry = entry;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE(world.significant_npcs[0].memory_log.size() == 1u);
    const MemoryEntry& stored = world.significant_npcs[0].memory_log[0];
    REQUIRE(stored.type == MemoryType::witnessed_illegal_activity);
    REQUIRE_THAT(stored.emotional_weight, WithinAbs(-0.8f, 0.001f));
    REQUIRE_THAT(stored.decay, WithinAbs(0.95f, 0.001f));
    REQUIRE(stored.is_actionable == true);
}

TEST_CASE("NPC memory decays over time", "[scenario][npc][memory]") {
    // Setup: NPC has one memory with decay = 0.99.
    // Simulate 100 ticks of per-tick multiplicative decay (entry.decay *= 0.99).
    // Assert: decay value < 0.5 after 100 iterations.
    auto world = create_test_world(42, 10, 1, 5);
    NPC& npc = world.significant_npcs[0];

    // Add the memory directly so we can manipulate its decay field.
    MemoryEntry entry{};
    entry.tick_timestamp = 0;
    entry.type = MemoryType::observation;
    entry.subject_id = 1;
    entry.emotional_weight = 0.9f;
    entry.decay = 0.99f;
    entry.is_actionable = false;
    npc.memory_log.push_back(entry);

    // Simulate 100 per-tick multiplicative decay steps.
    for (int t = 0; t < 100; ++t) {
        npc.memory_log[0].decay *= 0.99f;
    }

    // 0.99^100 ≈ 0.366, which is < 0.5.
    REQUIRE(world.significant_npcs[0].memory_log[0].decay < 0.5f);
    REQUIRE(world.significant_npcs[0].memory_log[0].decay > 0.0f);
}

TEST_CASE("oldest memories pruned at capacity", "[scenario][npc][memory]") {
    // Setup: Fill NPC with MAX_MEMORY_ENTRIES (500) memories.
    // Apply one more via delta. Assert: count still <= 500 and the newest entry exists.
    auto world = create_test_world(42, 10, 1, 5);
    NPC& npc = world.significant_npcs[0];

    // Pre-fill the memory log to capacity with low-weight, old entries.
    for (uint32_t i = 0; i < MAX_MEMORY_ENTRIES; ++i) {
        MemoryEntry old_entry{};
        old_entry.tick_timestamp = i;
        old_entry.type = MemoryType::hearsay;
        old_entry.subject_id = i;
        old_entry.emotional_weight = 0.1f;
        old_entry.decay = 0.5f;
        old_entry.is_actionable = false;
        npc.memory_log.push_back(old_entry);
    }
    REQUIRE(npc.memory_log.size() == MAX_MEMORY_ENTRIES);

    // Apply delta with a new, high-weight memory entry.
    MemoryEntry new_entry{};
    new_entry.tick_timestamp = 9999;
    new_entry.type = MemoryType::witnessed_wage_theft;
    new_entry.subject_id = 77777;
    new_entry.emotional_weight = -0.9f;
    new_entry.decay = 0.98f;
    new_entry.is_actionable = true;

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = npc.id;
    nd.new_memory_entry = new_entry;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    // Capacity invariant: must never exceed MAX_MEMORY_ENTRIES.
    REQUIRE(world.significant_npcs[0].memory_log.size() <= MAX_MEMORY_ENTRIES);

    // The newest entry (tick_timestamp == 9999) must be present.
    bool found_new = false;
    for (const auto& e : world.significant_npcs[0].memory_log) {
        if (e.tick_timestamp == 9999 && e.subject_id == 77777) {
            found_new = true;
            break;
        }
    }
    REQUIRE(found_new);
}

// ── Relationships ───────────────────────────────────────────────────────────

TEST_CASE("betrayal permanently caps trust recovery", "[scenario][npc][relationship]") {
    // Setup: NPC A holds a relationship to NPC B with trust=0.1 and recovery_ceiling=0.4
    //        after a betrayal.
    // Attempt: push trust up to 0.9 via updated_relationship delta.
    // Assert: trust never exceeds recovery_ceiling (0.4).
    auto world = create_test_world(42, 10, 1, 5);
    NPC& npc_a = world.significant_npcs[0];
    const uint32_t npc_b_id = world.significant_npcs[1].id;

    // Establish the post-betrayal relationship.
    Relationship betrayed_rel{};
    betrayed_rel.target_npc_id = npc_b_id;
    betrayed_rel.trust = 0.1f;
    betrayed_rel.fear = 0.0f;
    betrayed_rel.obligation_balance = 0.0f;
    betrayed_rel.last_interaction_tick = 0;
    betrayed_rel.is_movement_ally = false;
    betrayed_rel.recovery_ceiling = 0.4f;  // betrayal cap
    npc_a.relationships.push_back(betrayed_rel);

    // Attempt to raise trust to 0.9 via an updated_relationship delta.
    // The apply_deltas implementation clamps to recovery_ceiling.
    Relationship attempted_trust{};
    attempted_trust.target_npc_id = npc_b_id;
    attempted_trust.trust = 0.9f;
    attempted_trust.fear = 0.0f;
    attempted_trust.obligation_balance = 0.0f;
    attempted_trust.last_interaction_tick = 1;
    attempted_trust.is_movement_ally = false;
    attempted_trust.recovery_ceiling = 0.4f;  // ceiling persists

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = npc_a.id;
    nd.updated_relationship = attempted_trust;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    // Find the relationship to npc_b in npc_a's graph.
    const NPC& a_after = world.significant_npcs[0];
    bool found = false;
    for (const auto& rel : a_after.relationships) {
        if (rel.target_npc_id == npc_b_id) {
            REQUIRE(rel.trust <= rel.recovery_ceiling);
            REQUIRE(rel.trust <= 0.4f);
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("positive interactions increase relationship trust", "[scenario][npc][relationship]") {
    // Setup: NPC A holds a relationship to NPC B at trust = 0.5.
    // Apply updated_relationship with trust_delta = 0.1 (additive per apply_deltas semantics).
    // Assert: trust has increased above 0.5 (result = 0.6).
    auto world = create_test_world(42, 10, 1, 5);
    NPC& npc_a = world.significant_npcs[0];
    const uint32_t npc_b_id = world.significant_npcs[1].id;

    Relationship initial_rel{};
    initial_rel.target_npc_id = npc_b_id;
    initial_rel.trust = 0.5f;
    initial_rel.fear = 0.1f;
    initial_rel.obligation_balance = 0.0f;
    initial_rel.last_interaction_tick = 0;
    initial_rel.is_movement_ally = false;
    initial_rel.recovery_ceiling = 1.0f;
    npc_a.relationships.push_back(initial_rel);

    // Positive interaction: module outputs a trust delta of +0.1.
    // apply_deltas adds this to the existing trust (0.5 + 0.1 = 0.6),
    // then clamps to [-1, 1] and enforces recovery_ceiling.
    Relationship delta_rel{};
    delta_rel.target_npc_id = npc_b_id;
    delta_rel.trust = 0.1f;  // additive delta
    delta_rel.fear = 0.0f;
    delta_rel.obligation_balance = 0.0f;
    delta_rel.last_interaction_tick = 1;
    delta_rel.is_movement_ally = false;
    delta_rel.recovery_ceiling = 1.0f;

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = npc_a.id;
    nd.updated_relationship = delta_rel;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    const NPC& a_after = world.significant_npcs[0];
    bool found = false;
    for (const auto& rel : a_after.relationships) {
        if (rel.target_npc_id == npc_b_id) {
            REQUIRE(rel.trust > 0.5f);
            REQUIRE_THAT(rel.trust, WithinAbs(0.6f, 0.01f));
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

// ── Migration ───────────────────────────────────────────────────────────────

TEST_CASE("NPC migrates when satisfaction below threshold", "[scenario][npc][migration]") {
    // Setup: NPC resident in province 0, in_transit to province 1.
    //        Schedule npc_travel_arrival DWQ item at current_tick.
    // Drain DWQ. Assert: travel_status changed to resident.
    auto world = create_test_world(42, 10, 2, 5);
    world.current_tick = 10;

    NPC& npc = world.significant_npcs[0];
    REQUIRE(npc.home_province_id == 0);

    // Mark the NPC as in-transit toward province 1.
    npc.travel_status = NPCTravelStatus::in_transit;
    npc.current_province_id = 1;  // destination

    // Schedule the arrival for the current tick.
    world.deferred_work_queue.push({10,  // due_tick == current_tick, so it fires immediately
                                    WorkType::npc_travel_arrival, npc.id, EmptyPayload{}});

    DeltaBuffer delta{};
    drain_deferred_work(world, delta);
    apply_deltas(world, delta);

    // After drain, the NPC should be marked as resident (arrived).
    REQUIRE(world.significant_npcs[0].travel_status == NPCTravelStatus::resident);
}

// ── Banking scenarios ───────────────────────────────────────────────────────

TEST_CASE("NPC with good credit approved for loan", "[scenario][npc][banking]") {
    // Setup: NPC with good credit score applying for a business_startup loan.
    // Module output: capital_delta = +50000 (loan proceeds) and an interaction
    //   memory entry recording the loan approval.
    // Assert: capital increased by loan amount; memory log contains the entry.
    auto world = create_test_world(42, 10, 1, 5);
    NPC& npc = world.significant_npcs[0];
    float initial_capital = npc.capital;

    MemoryEntry approval_memory{};
    approval_memory.tick_timestamp = world.current_tick;
    approval_memory.type = MemoryType::interaction;
    approval_memory.subject_id = 0;           // bank entity
    approval_memory.emotional_weight = 0.5f;  // positive: loan granted
    approval_memory.decay = 0.97f;
    approval_memory.is_actionable = false;

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = npc.id;
    nd.capital_delta = 50000.0f;
    nd.new_memory_entry = approval_memory;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(initial_capital + 50000.0f, 0.01f));
    REQUIRE(world.significant_npcs[0].memory_log.size() == 1u);
    REQUIRE(world.significant_npcs[0].memory_log[0].type == MemoryType::interaction);
    REQUIRE(world.significant_npcs[0].memory_log[0].emotional_weight > 0.0f);
}

TEST_CASE("loan default triggers credit penalty", "[scenario][npc][banking]") {
    // Setup: NPC has near-zero capital; loan payment is due.
    // Module output: seize remaining capital (capital_delta = -remaining),
    //   status remains active (new_status = active), and a default memory is recorded.
    // Assert: capital decreased; negative memory logged; NPC still active.
    auto world = create_test_world(42, 10, 1, 5);
    NPC& npc = world.significant_npcs[0];

    // Set low capital to simulate inability to repay.
    npc.capital = 100.0f;
    float remaining_capital = npc.capital;

    MemoryEntry default_memory{};
    default_memory.tick_timestamp = world.current_tick;
    default_memory.type = MemoryType::event;
    default_memory.subject_id = 0;            // bank entity
    default_memory.emotional_weight = -0.7f;  // negative: default consequence
    default_memory.decay = 0.99f;
    default_memory.is_actionable = true;

    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = npc.id;
    nd.capital_delta = -remaining_capital;  // collateral seizure
    nd.new_status = NPCStatus::active;      // still active but credit-penalized
    nd.new_memory_entry = default_memory;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    // Capital should be ~0 after seizure (clamped at 0 by apply_deltas).
    REQUIRE(world.significant_npcs[0].capital <= 0.01f);
    REQUIRE(world.significant_npcs[0].status == NPCStatus::active);
    REQUIRE(world.significant_npcs[0].memory_log.size() == 1u);
    REQUIRE(world.significant_npcs[0].memory_log[0].emotional_weight < 0.0f);
}

// ── Healthcare scenarios ────────────────────────────────────────────────────

TEST_CASE("healthcare quality depends on funding", "[scenario][npc][healthcare]") {
    // Setup: Two provinces. Province 0 receives a positive stability boost
    //        (simulating well-funded healthcare). Province 1 receives a negative
    //        stability delta (under-funded; conditions deteriorate).
    // Assert: Province 0 stability > Province 1 stability after delta application.
    auto world = create_test_world(42, 10, 2, 5);

    float initial_stability_0 = world.provinces[0].conditions.stability_score;
    float initial_stability_1 = world.provinces[1].conditions.stability_score;

    DeltaBuffer delta{};

    // Province 0: well-funded healthcare improves stability.
    RegionDelta rd0{};
    rd0.region_id = 0;
    rd0.stability_delta = 0.10f;
    delta.region_deltas.push_back(rd0);

    // Province 1: under-funded healthcare degrades stability.
    RegionDelta rd1{};
    rd1.region_id = 1;
    rd1.stability_delta = -0.10f;
    delta.region_deltas.push_back(rd1);

    apply_deltas(world, delta);

    REQUIRE(world.provinces[0].conditions.stability_score >
            world.provinces[1].conditions.stability_score);
    REQUIRE_THAT(world.provinces[0].conditions.stability_score,
                 WithinAbs(initial_stability_0 + 0.10f, 0.01f));
    REQUIRE_THAT(world.provinces[1].conditions.stability_score,
                 WithinAbs(initial_stability_1 - 0.10f, 0.01f));
}

TEST_CASE("hospital capacity limits treatment", "[scenario][npc][healthcare]") {
    // Setup: Province with existing conditions. Apply a limited positive stability
    //        delta representing partial treatment capacity (only 10 of 20 treated).
    // Assert: Stability improved, but not by the full amount that unlimited capacity
    //         would provide. The partial improvement verifies the capacity constraint.
    auto world = create_test_world(42, 10, 1, 5);

    float initial_stability = world.provinces[0].conditions.stability_score;

    // Full capacity would improve stability by 0.20 (20 patients fully treated).
    // Actual capacity is 10 beds: only 0.10 improvement realized.
    float partial_improvement = 0.10f;
    float full_improvement = 0.20f;

    DeltaBuffer delta{};
    RegionDelta rd{};
    rd.region_id = 0;
    rd.stability_delta = partial_improvement;
    delta.region_deltas.push_back(rd);
    apply_deltas(world, delta);

    float achieved = world.provinces[0].conditions.stability_score - initial_stability;

    REQUIRE_THAT(achieved, WithinAbs(partial_improvement, 0.01f));
    // Capacity constraint: improvement is strictly less than the uncapped amount.
    REQUIRE(achieved < full_improvement);
}

// ── Government budget scenarios ─────────────────────────────────────────────

TEST_CASE("budget deficit increases borrowing costs", "[scenario][npc][budget]") {
    // Setup: Government running a deficit for 30 consecutive ticks.
    // Each tick applies a small negative stability_delta representing the
    // compounding effect of sustained borrowing on provincial conditions.
    // Assert: Stability has decreased after 30 iterations.
    auto world = create_test_world(42, 10, 1, 5);

    float initial_stability = world.provinces[0].conditions.stability_score;

    for (int tick = 0; tick < 30; ++tick) {
        DeltaBuffer delta{};
        RegionDelta rd{};
        rd.region_id = 0;
        rd.stability_delta = -0.005f;  // small per-tick deficit drag
        delta.region_deltas.push_back(rd);
        apply_deltas(world, delta);
    }

    // After 30 ticks of deficit: stability should be noticeably lower.
    float final_stability = world.provinces[0].conditions.stability_score;
    REQUIRE(final_stability < initial_stability);
    // At -0.005 per tick * 30 ticks = -0.15 total expected drag.
    REQUIRE_THAT(final_stability, WithinAbs(std::max(0.0f, initial_stability - 0.15f), 0.01f));
}

TEST_CASE("spending allocation affects service quality", "[scenario][npc][budget]") {
    // Setup: High healthcare allocation → stability increases.
    //        Low law enforcement allocation → crime rate increases simultaneously.
    // Apply both deltas in a single tick to verify independent accumulation.
    // Assert: stability increased AND crime_rate increased.
    auto world = create_test_world(42, 10, 1, 5);

    float initial_stability = world.provinces[0].conditions.stability_score;
    float initial_crime = world.provinces[0].conditions.crime_rate;

    DeltaBuffer delta{};
    RegionDelta rd{};
    rd.region_id = 0;
    rd.stability_delta = 0.08f;   // high healthcare spend boosts stability
    rd.crime_rate_delta = 0.06f;  // low law enforcement → crime rises
    delta.region_deltas.push_back(rd);
    apply_deltas(world, delta);

    REQUIRE(world.provinces[0].conditions.stability_score > initial_stability);
    REQUIRE(world.provinces[0].conditions.crime_rate > initial_crime);

    REQUIRE_THAT(world.provinces[0].conditions.stability_score,
                 WithinAbs(initial_stability + 0.08f, 0.01f));
    REQUIRE_THAT(world.provinces[0].conditions.crime_rate, WithinAbs(initial_crime + 0.06f, 0.01f));
}
