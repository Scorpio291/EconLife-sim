// NPC behavior module unit tests.
// All tests tagged [npc_behavior][tier5].
//
// Tests verify the NPC daily decision engine:
//   1.  High-EV action selected over inaction
//   2.  Below-threshold EV produces waiting status
//   3.  Motivation vector drives action preference
//   4.  Memory decay archives old entries
//   5.  Risk discount reduces high-exposure EV
//   6.  Relationship modifier boosts cooperative action
//   7.  compute_expected_value basic calculation
//   8.  compute_risk_discount low risk scenario
//   9.  compute_risk_discount high risk clamped to minimum
//   10. archive_lowest_decay_memory removes min-decay entry
//   11. renormalize_motivation_weights sums to 1.0
//   12. clamp_relationship_trust within [-1.0, 1.0]
//   13. clamp_relationship_fear within [0.0, 1.0]
//   14. recovery_ceiling limits trust
//   15. memory_decay_below_floor archived
//   16. motivation_shift_preserves_sum
//   17. knowledge_confidence_decay
//   18. module_interface_properties
//   19. province_parallel_flag
//   20. execute_processes_all_provinces
//   21. evaluate_action_with_multiple_outcomes
//   22. empty_outcomes_produce_zero_ev
//   23. memory_cap_triggers_archive
//   24. relationship_recovery_ceiling_minimum
//   25. renormalize_all_zero_weights_to_uniform

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "modules/npc_behavior/npc_behavior_module.h"
#include "modules/npc_behavior/npc_behavior_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for NPC behavior tests.
WorldState make_test_world_state(uint32_t tick = 1) {
    WorldState state{};
    state.current_tick = tick;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

// Create a Province with sensible defaults.
Province make_test_province(uint32_t id, float criminal_dominance = 0.0f) {
    Province prov{};
    prov.id = id;
    prov.region_id = 0;
    prov.nation_id = 0;
    prov.conditions.criminal_dominance_index = criminal_dominance;
    prov.conditions.stability_score = 0.5f;
    prov.conditions.inequality_index = 0.3f;
    prov.conditions.crime_rate = 0.1f;
    prov.conditions.formal_employment_rate = 0.7f;
    prov.infrastructure_rating = 0.6f;
    prov.demographics.total_population = 100000;
    prov.demographics.income_high_fraction = 0.15f;
    return prov;
}

// Create an NPC with sensible defaults for behavior testing.
NPC make_test_npc(uint32_t id, uint32_t province_id = 0,
                  float risk_tolerance = 0.5f) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::worker;
    npc.risk_tolerance = risk_tolerance;
    npc.current_province_id = province_id;
    npc.home_province_id = province_id;
    npc.status = NPCStatus::active;
    npc.capital = 1000.0f;
    npc.social_capital = 0.5f;
    npc.movement_follower_count = 0;

    // Uniform motivation weights (sum = 1.0).
    for (size_t i = 0; i < 8; ++i) {
        npc.motivations.weights[i] = 1.0f / 8.0f;
    }

    return npc;
}

// Create a MemoryEntry with specified decay.
MemoryEntry make_test_memory(uint32_t tick, float decay,
                              float emotional_weight = 0.5f,
                              bool actionable = true) {
    MemoryEntry mem{};
    mem.tick_timestamp = tick;
    mem.type = MemoryType::interaction;
    mem.subject_id = 1;
    mem.emotional_weight = emotional_weight;
    mem.decay = decay;
    mem.is_actionable = actionable;
    return mem;
}

// Create a Relationship with specified trust and fear.
Relationship make_test_relationship(uint32_t target_id,
                                     float trust = 0.5f,
                                     float fear = 0.1f,
                                     float recovery_ceiling = 1.0f) {
    Relationship rel{};
    rel.target_npc_id = target_id;
    rel.trust = trust;
    rel.fear = fear;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 0;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = recovery_ceiling;
    return rel;
}

// Create a minimal PlayerCharacter for testing.
PlayerCharacter make_test_player(uint32_t id = 1) {
    PlayerCharacter player{};
    player.id = id;
    player.wealth = 100000.0f;
    return player;
}

}  // anonymous namespace

// ===========================================================================
// Test 1: High EV action selected over inaction
// ===========================================================================

TEST_CASE("test_high_ev_action_selected_over_inaction", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);
    // Heavily weight financial_gain motivation.
    npc.motivations.weights[static_cast<size_t>(OutcomeType::financial_gain)] = 0.9f;
    for (size_t i = 1; i < 8; ++i) {
        npc.motivations.weights[i] = 0.1f / 7.0f;
    }

    // Work action with high financial gain outcome.
    std::vector<ActionOutcome> work_outcomes = {
        {OutcomeType::financial_gain, 0.9f, 0.8f}
    };

    ActionEvaluation eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::work, work_outcomes, 0.0f, -2.0f, 0.3f);

    // EV = 0.9 * 0.9 * 0.8 = 0.648, risk_discount = 1.0 (no risk), net = 0.648
    REQUIRE(eval.net_utility > NpcBehaviorModule::Constants::inaction_threshold);
    REQUIRE(eval.action == DailyAction::work);
}

// ===========================================================================
// Test 2: Below threshold produces waiting status
// ===========================================================================

TEST_CASE("test_below_threshold_produces_waiting_status", "[npc_behavior][tier5]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    // Create NPC with near-zero motivations so all actions score below threshold.
    NPC npc = make_test_npc(100, 0);
    for (size_t i = 0; i < 8; ++i) {
        npc.motivations.weights[i] = 0.001f;
    }
    NpcBehaviorModule::renormalize_motivation_weights(npc.motivations);
    // All weights are now uniform but very small outcomes will produce low EV.
    // Actually, after renormalization they're 1/8 each. To get below threshold,
    // make risk tolerance very low and set high risk NPC.
    npc.risk_tolerance = 0.0f;
    // With low risk tolerance, exposure_risk on criminal_activity (0.7) will
    // heavily discount. But other actions have 0 exposure_risk.
    // Instead, directly test with a tiny outcome.
    std::vector<ActionOutcome> tiny_outcomes = {
        {OutcomeType::financial_gain, 0.01f, 0.01f}
    };

    ActionEvaluation eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::rest, tiny_outcomes, 0.5f, -2.0f, 0.3f);

    // EV = (1/8) * 0.01 * 0.01 = 0.0000125
    // risk_discount = max(0.05, 1.0 - (0.5 - 0.0) * 2.0) = max(0.05, 0.0) = 0.05
    // net = 0.0000125 * 0.05 = very small
    REQUIRE(eval.net_utility < NpcBehaviorModule::Constants::inaction_threshold);
}

// ===========================================================================
// Test 3: Motivation vector drives action preference
// ===========================================================================

TEST_CASE("test_motivation_vector_drives_action_preference", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);

    // Strongly weight self_preservation.
    for (size_t i = 0; i < 8; ++i) {
        npc.motivations.weights[i] = 0.0f;
    }
    npc.motivations.weights[static_cast<size_t>(OutcomeType::self_preservation)] = 1.0f;

    // Rest action with self_preservation outcome should score highest.
    std::vector<ActionOutcome> rest_outcomes = {
        {OutcomeType::self_preservation, 0.9f, 0.5f}
    };
    std::vector<ActionOutcome> work_outcomes = {
        {OutcomeType::financial_gain, 0.9f, 0.5f}
    };

    auto rest_eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::rest, rest_outcomes, 0.0f, -2.0f, 0.3f);
    auto work_eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::work, work_outcomes, 0.0f, -2.0f, 0.3f);

    // rest EV = 1.0 * 0.9 * 0.5 = 0.45
    // work EV = 0.0 * 0.9 * 0.5 = 0.0
    REQUIRE(rest_eval.net_utility > work_eval.net_utility);
}

// ===========================================================================
// Test 4: Memory decay archives old entries
// ===========================================================================

TEST_CASE("test_memory_decay_archives_old_entries", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);

    // Add memory entries with varying decay values.
    npc.memory_log.push_back(make_test_memory(1, 0.5f));   // healthy
    npc.memory_log.push_back(make_test_memory(2, 0.02f));  // near floor
    npc.memory_log.push_back(make_test_memory(3, 0.005f)); // below floor after decay

    REQUIRE(npc.memory_log.size() == 3);

    // Apply a small decay that will push the 0.005 entry below floor (0.01).
    // After decay: 0.005 * (1.0 - 0.002) = 0.00499 < 0.01 -> archived
    // After decay: 0.02 * (1.0 - 0.002) = 0.01996 > 0.01 -> kept
    NpcBehaviorModule::decay_memories(npc, NpcBehaviorModule::Constants::memory_decay_rate);

    // The entry with decay 0.005 should have been archived (removed).
    REQUIRE(npc.memory_log.size() == 2);
}

// ===========================================================================
// Test 5: Risk discount reduces high exposure EV
// ===========================================================================

TEST_CASE("test_risk_discount_reduces_high_exposure_ev", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1, 0, 0.3f);  // risk_tolerance = 0.3

    std::vector<ActionOutcome> outcomes = {
        {OutcomeType::financial_gain, 0.8f, 0.7f}
    };

    // Low exposure_risk action.
    auto low_risk_eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::work, outcomes, 0.1f, -2.0f, 0.3f);

    // High exposure_risk action.
    auto high_risk_eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::criminal_activity, outcomes, 0.8f, -2.0f, 0.3f);

    // High risk should produce lower net_utility.
    REQUIRE(low_risk_eval.net_utility > high_risk_eval.net_utility);
}

// ===========================================================================
// Test 6: Relationship modifier boosts cooperative action
// ===========================================================================

TEST_CASE("test_relationship_modifier_boosts_cooperative_action", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);

    std::vector<ActionOutcome> outcomes = {
        {OutcomeType::relationship_repair, 0.7f, 0.5f}
    };

    // Without trust bonus.
    auto no_trust_eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::socialize, outcomes, 0.0f, -2.0f, 0.3f);

    // With positive trust bonus.
    auto with_trust_eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::socialize, outcomes, 0.0f, 0.8f, 0.3f);

    // Trust modifier: net *= (1.0 + 0.8 * 0.3) = 1.24
    REQUIRE(with_trust_eval.net_utility > no_trust_eval.net_utility);
}

// ===========================================================================
// Test 7: compute_expected_value basic
// ===========================================================================

TEST_CASE("test_compute_expected_value_basic", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);
    // Uniform weights: 1/8 each.

    ActionOutcome outcome{};
    outcome.type = OutcomeType::financial_gain;
    outcome.probability = 0.8f;
    outcome.magnitude = 0.6f;

    float ev = NpcBehaviorModule::compute_expected_value(npc, outcome);

    // EV = (1/8) * 0.8 * 0.6 = 0.06
    REQUIRE_THAT(ev, WithinAbs(0.06f, 0.001f));
}

TEST_CASE("test_compute_expected_value_high_weight", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);
    npc.motivations.weights[static_cast<size_t>(OutcomeType::financial_gain)] = 1.0f;

    ActionOutcome outcome{OutcomeType::financial_gain, 1.0f, 1.0f};

    float ev = NpcBehaviorModule::compute_expected_value(npc, outcome);

    // EV = 1.0 * 1.0 * 1.0 = 1.0
    REQUIRE_THAT(ev, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_compute_expected_value_zero_weight", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);
    npc.motivations.weights[static_cast<size_t>(OutcomeType::revenge)] = 0.0f;

    ActionOutcome outcome{OutcomeType::revenge, 1.0f, 1.0f};

    float ev = NpcBehaviorModule::compute_expected_value(npc, outcome);

    REQUIRE_THAT(ev, WithinAbs(0.0f, 0.0001f));
}

// ===========================================================================
// Test 8: compute_risk_discount low risk
// ===========================================================================

TEST_CASE("test_compute_risk_discount_low_risk", "[npc_behavior][tier5]") {
    // exposure_risk = 0.1, risk_tolerance = 0.5
    // raw = 1.0 - (0.1 - 0.5) * 2.0 = 1.0 + 0.8 = 1.8
    // clamped to min(1.0, 1.8) = 1.0 (risk discount never boosts above 1.0)
    float discount = NpcBehaviorModule::compute_risk_discount(0.1f, 0.5f);
    REQUIRE_THAT(discount, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_compute_risk_discount_equal_risk_and_tolerance", "[npc_behavior][tier5]") {
    // exposure_risk = 0.5, risk_tolerance = 0.5
    // raw = 1.0 - (0.5 - 0.5) * 2.0 = 1.0
    float discount = NpcBehaviorModule::compute_risk_discount(0.5f, 0.5f);
    REQUIRE_THAT(discount, WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Test 9: compute_risk_discount high risk clamped
// ===========================================================================

TEST_CASE("test_compute_risk_discount_high_risk_clamped", "[npc_behavior][tier5]") {
    // exposure_risk = 1.0, risk_tolerance = 0.0
    // raw = 1.0 - (1.0 - 0.0) * 2.0 = 1.0 - 2.0 = -1.0
    // clamped to max(0.05, -1.0) = 0.05
    float discount = NpcBehaviorModule::compute_risk_discount(1.0f, 0.0f);
    REQUIRE_THAT(discount, WithinAbs(0.05f, 0.001f));
}

TEST_CASE("test_compute_risk_discount_extreme_exposure", "[npc_behavior][tier5]") {
    // exposure_risk = 0.9, risk_tolerance = 0.1
    // raw = 1.0 - (0.9 - 0.1) * 2.0 = 1.0 - 1.6 = -0.6
    // clamped to max(0.05, -0.6) = 0.05
    float discount = NpcBehaviorModule::compute_risk_discount(0.9f, 0.1f);
    REQUIRE_THAT(discount, WithinAbs(0.05f, 0.001f));
}

// ===========================================================================
// Test 10: archive_lowest_decay_memory
// ===========================================================================

TEST_CASE("test_archive_lowest_decay_memory", "[npc_behavior][tier5]") {
    std::vector<MemoryEntry> memory_log;
    memory_log.push_back(make_test_memory(1, 0.8f));
    memory_log.push_back(make_test_memory(2, 0.1f));   // lowest
    memory_log.push_back(make_test_memory(3, 0.5f));

    REQUIRE(memory_log.size() == 3);
    NpcBehaviorModule::archive_lowest_decay_memory(memory_log);
    REQUIRE(memory_log.size() == 2);

    // The entry with decay 0.1 (tick 2) should be removed.
    for (const auto& mem : memory_log) {
        REQUIRE(mem.decay > 0.1f);
    }
}

TEST_CASE("test_archive_lowest_decay_memory_empty", "[npc_behavior][tier5]") {
    std::vector<MemoryEntry> memory_log;
    // Should not crash on empty log.
    NpcBehaviorModule::archive_lowest_decay_memory(memory_log);
    REQUIRE(memory_log.empty());
}

// ===========================================================================
// Test 11: renormalize_motivation_weights
// ===========================================================================

TEST_CASE("test_renormalize_motivation_weights", "[npc_behavior][tier5]") {
    MotivationVector motivations{};
    motivations.weights = {0.5f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    NpcBehaviorModule::renormalize_motivation_weights(motivations);

    // Sum should be 1.0.
    float sum = 0.0f;
    for (float w : motivations.weights) {
        sum += w;
    }
    REQUIRE_THAT(sum, WithinAbs(1.0f, 0.001f));

    // Proportions preserved: 0.5/1.0 = 0.5, 0.3/1.0 = 0.3, 0.2/1.0 = 0.2
    REQUIRE_THAT(motivations.weights[0], WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(motivations.weights[1], WithinAbs(0.3f, 0.001f));
    REQUIRE_THAT(motivations.weights[2], WithinAbs(0.2f, 0.001f));
}

TEST_CASE("test_renormalize_motivation_weights_already_normalized", "[npc_behavior][tier5]") {
    MotivationVector motivations{};
    for (size_t i = 0; i < 8; ++i) {
        motivations.weights[i] = 1.0f / 8.0f;
    }

    NpcBehaviorModule::renormalize_motivation_weights(motivations);

    float sum = 0.0f;
    for (float w : motivations.weights) {
        sum += w;
    }
    REQUIRE_THAT(sum, WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Test 12: clamp_relationship_trust
// ===========================================================================

TEST_CASE("test_clamp_relationship_trust", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, 1.5f, 0.5f);

    NpcBehaviorModule::clamp_relationship(rel);

    // Trust clamped to 1.0.
    REQUIRE_THAT(rel.trust, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_clamp_relationship_trust_negative", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, -1.5f, 0.5f);

    NpcBehaviorModule::clamp_relationship(rel);

    // Trust clamped to -1.0.
    REQUIRE_THAT(rel.trust, WithinAbs(-1.0f, 0.001f));
}

TEST_CASE("test_clamp_relationship_trust_within_range", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, 0.5f, 0.3f);

    NpcBehaviorModule::clamp_relationship(rel);

    // Trust should be unchanged.
    REQUIRE_THAT(rel.trust, WithinAbs(0.5f, 0.001f));
}

// ===========================================================================
// Test 13: clamp_relationship_fear
// ===========================================================================

TEST_CASE("test_clamp_relationship_fear", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, 0.5f, 1.5f);

    NpcBehaviorModule::clamp_relationship(rel);

    // Fear clamped to 1.0.
    REQUIRE_THAT(rel.fear, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_clamp_relationship_fear_negative", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, 0.5f, -0.5f);

    NpcBehaviorModule::clamp_relationship(rel);

    // Fear clamped to 0.0.
    REQUIRE_THAT(rel.fear, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 14: recovery_ceiling limits trust
// ===========================================================================

TEST_CASE("test_recovery_ceiling_limits_trust", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, 0.8f, 0.1f, 0.5f);
    // trust = 0.8, recovery_ceiling = 0.5

    NpcBehaviorModule::clamp_relationship(rel);

    // Trust should be clamped to recovery_ceiling.
    REQUIRE_THAT(rel.trust, WithinAbs(0.5f, 0.001f));
}

TEST_CASE("test_recovery_ceiling_at_one_does_not_limit", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, 0.8f, 0.1f, 1.0f);

    NpcBehaviorModule::clamp_relationship(rel);

    // Trust should not be limited.
    REQUIRE_THAT(rel.trust, WithinAbs(0.8f, 0.001f));
}

// ===========================================================================
// Test 15: memory_decay_below_floor archived
// ===========================================================================

TEST_CASE("test_memory_decay_below_floor_archived", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);

    // Create a memory entry exactly at the floor.
    npc.memory_log.push_back(make_test_memory(1, 0.011f));

    // After one decay step: 0.011 * (1.0 - 0.002) = 0.010978 > 0.01 -> kept
    NpcBehaviorModule::decay_memories(npc, NpcBehaviorModule::Constants::memory_decay_rate);
    REQUIRE(npc.memory_log.size() == 1);

    // Create a memory entry just below where decay will push it under floor.
    NPC npc2 = make_test_npc(2);
    npc2.memory_log.push_back(make_test_memory(1, 0.009f));

    // After one decay step: 0.009 * (1.0 - 0.002) = 0.008982 < 0.01 -> archived
    NpcBehaviorModule::decay_memories(npc2, NpcBehaviorModule::Constants::memory_decay_rate);
    REQUIRE(npc2.memory_log.empty());
}

// ===========================================================================
// Test 16: motivation_shift_preserves_sum
// ===========================================================================

TEST_CASE("test_motivation_shift_preserves_sum", "[npc_behavior][tier5]") {
    MotivationVector motivations{};
    motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f, 0.1f};

    // Apply a shift to one weight.
    motivations.weights[0] += 0.05f;

    // After renormalization, sum should be 1.0.
    NpcBehaviorModule::renormalize_motivation_weights(motivations);

    float sum = 0.0f;
    for (float w : motivations.weights) {
        sum += w;
    }
    REQUIRE_THAT(sum, WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Test 17: knowledge_confidence_decay
// ===========================================================================

TEST_CASE("test_knowledge_confidence_decay", "[npc_behavior][tier5]") {
    // Verify the constant exists and is reasonable.
    float rate = NpcBehaviorModule::Constants::knowledge_confidence_decay_rate;
    REQUIRE_THAT(rate, WithinAbs(0.001f, 0.0001f));

    // Simulate confidence decay.
    float confidence = 1.0f;
    confidence -= rate;
    REQUIRE_THAT(confidence, WithinAbs(0.999f, 0.0001f));

    // After 100 ticks.
    confidence = 1.0f;
    for (int i = 0; i < 100; ++i) {
        confidence -= rate;
    }
    confidence = std::max(0.0f, confidence);
    REQUIRE_THAT(confidence, WithinAbs(0.9f, 0.001f));
}

TEST_CASE("test_knowledge_confidence_clamps_to_zero", "[npc_behavior][tier5]") {
    float confidence = 0.0005f;
    confidence -= NpcBehaviorModule::Constants::knowledge_confidence_decay_rate;
    confidence = std::max(0.0f, confidence);
    REQUIRE_THAT(confidence, WithinAbs(0.0f, 0.001f));
}

// ===========================================================================
// Test 18: module_interface_properties
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[npc_behavior][tier5]") {
    NpcBehaviorModule module;

    REQUIRE(module.name() == "npc_behavior");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);

    auto after = module.runs_after();
    REQUIRE(after.size() == 4);
    REQUIRE(after[0] == "financial_distribution");
    REQUIRE(after[1] == "npc_business");
    REQUIRE(after[2] == "commodity_trading");
    REQUIRE(after[3] == "real_estate");

    auto before = module.runs_before();
    REQUIRE(before.empty());
}

// ===========================================================================
// Test 19: province_parallel_flag
// ===========================================================================

TEST_CASE("test_province_parallel_flag", "[npc_behavior][tier5]") {
    NpcBehaviorModule module;
    REQUIRE(module.is_province_parallel() == true);
}

// ===========================================================================
// Test 20: execute_processes_all_provinces
// ===========================================================================

TEST_CASE("test_execute_processes_all_provinces", "[npc_behavior][tier5]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    // Add NPCs to both provinces.
    state.significant_npcs.push_back(make_test_npc(1, 0));
    state.significant_npcs.push_back(make_test_npc(2, 1));

    NpcBehaviorModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    // Both provinces should have generated NPC deltas.
    REQUIRE(delta.npc_deltas.size() == 2);
}

TEST_CASE("test_execute_province_skips_inactive_npcs", "[npc_behavior][tier5]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    NPC active_npc = make_test_npc(1, 0);
    active_npc.status = NPCStatus::active;

    NPC dead_npc = make_test_npc(2, 0);
    dead_npc.status = NPCStatus::dead;

    NPC imprisoned_npc = make_test_npc(3, 0);
    imprisoned_npc.status = NPCStatus::imprisoned;

    state.significant_npcs.push_back(active_npc);
    state.significant_npcs.push_back(dead_npc);
    state.significant_npcs.push_back(imprisoned_npc);

    NpcBehaviorModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Only the active NPC should produce a delta.
    REQUIRE(delta.npc_deltas.size() == 1);
    REQUIRE(delta.npc_deltas[0].npc_id == 1);
}

// ===========================================================================
// Test 21: evaluate_action_with_multiple_outcomes
// ===========================================================================

TEST_CASE("test_evaluate_action_with_multiple_outcomes", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);
    // Uniform weights: 1/8 each.

    std::vector<ActionOutcome> outcomes = {
        {OutcomeType::financial_gain, 0.8f, 0.5f},
        {OutcomeType::security_gain, 0.6f, 0.3f},
        {OutcomeType::career_advance, 0.4f, 0.7f},
    };

    auto eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::work, outcomes, 0.0f, -2.0f, 0.3f);

    // EV = (1/8)(0.8*0.5) + (1/8)(0.6*0.3) + (1/8)(0.4*0.7)
    //    = (1/8)(0.40 + 0.18 + 0.28)
    //    = (1/8)(0.86)
    //    = 0.1075
    // risk_discount = 1.0 (no exposure)
    // net = 0.1075
    REQUIRE_THAT(eval.expected_value, WithinAbs(0.1075f, 0.001f));
    REQUIRE_THAT(eval.risk_discount, WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(eval.net_utility, WithinAbs(0.1075f, 0.001f));
}

// ===========================================================================
// Test 22: empty_outcomes_produce_zero_ev
// ===========================================================================

TEST_CASE("test_empty_outcomes_produce_zero_ev", "[npc_behavior][tier5]") {
    NPC npc = make_test_npc(1);
    std::vector<ActionOutcome> no_outcomes;

    auto eval = NpcBehaviorModule::evaluate_action(
        npc, DailyAction::rest, no_outcomes, 0.0f, -2.0f, 0.3f);

    REQUIRE_THAT(eval.expected_value, WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(eval.net_utility, WithinAbs(0.0f, 0.0001f));
}

// ===========================================================================
// Test 23: memory_cap_triggers_archive
// ===========================================================================

TEST_CASE("test_memory_cap_triggers_archive", "[npc_behavior][tier5]") {
    std::vector<MemoryEntry> memory_log;

    // Fill to MAX_MEMORY_ENTRIES.
    for (uint32_t i = 0; i < MAX_MEMORY_ENTRIES; ++i) {
        float decay = 0.5f + static_cast<float>(i) * 0.001f;
        memory_log.push_back(make_test_memory(i, decay));
    }

    REQUIRE(memory_log.size() == MAX_MEMORY_ENTRIES);

    // Archive lowest to make room.
    NpcBehaviorModule::archive_lowest_decay_memory(memory_log);

    REQUIRE(memory_log.size() == MAX_MEMORY_ENTRIES - 1);

    // The entry with the lowest decay (0.5f, tick 0) should have been removed.
    for (const auto& mem : memory_log) {
        REQUIRE(mem.decay > 0.5f);
    }
}

// ===========================================================================
// Test 24: relationship_recovery_ceiling_minimum
// ===========================================================================

TEST_CASE("test_relationship_recovery_ceiling_minimum", "[npc_behavior][tier5]") {
    Relationship rel = make_test_relationship(1, 0.1f, 0.1f, 0.05f);
    // recovery_ceiling = 0.05, which is below minimum (0.15)

    NpcBehaviorModule::clamp_relationship(rel);

    // recovery_ceiling should be raised to minimum.
    REQUIRE_THAT(rel.recovery_ceiling, WithinAbs(0.15f, 0.001f));
    // trust should be clamped to the (now raised) recovery_ceiling.
    REQUIRE(rel.trust <= rel.recovery_ceiling + 0.001f);
}

// ===========================================================================
// Test 25: renormalize_all_zero_weights_to_uniform
// ===========================================================================

TEST_CASE("test_renormalize_all_zero_weights_to_uniform", "[npc_behavior][tier5]") {
    MotivationVector motivations{};
    for (size_t i = 0; i < 8; ++i) {
        motivations.weights[i] = 0.0f;
    }

    NpcBehaviorModule::renormalize_motivation_weights(motivations);

    float expected_uniform = 1.0f / 8.0f;
    for (size_t i = 0; i < 8; ++i) {
        REQUIRE_THAT(motivations.weights[i], WithinAbs(expected_uniform, 0.001f));
    }

    float sum = 0.0f;
    for (float w : motivations.weights) {
        sum += w;
    }
    REQUIRE_THAT(sum, WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Test 26: apply_relationship_modifier calculations
// ===========================================================================

TEST_CASE("test_apply_relationship_modifier_positive_trust", "[npc_behavior][tier5]") {
    float result = NpcBehaviorModule::apply_relationship_modifier(1.0f, 0.5f, 0.3f);
    // 1.0 * (1.0 + 0.5 * 0.3) = 1.0 * 1.15 = 1.15
    REQUIRE_THAT(result, WithinAbs(1.15f, 0.001f));
}

TEST_CASE("test_apply_relationship_modifier_negative_trust", "[npc_behavior][tier5]") {
    float result = NpcBehaviorModule::apply_relationship_modifier(1.0f, -0.5f, 0.3f);
    // 1.0 * (1.0 + (-0.5) * 0.3) = 1.0 * 0.85 = 0.85
    REQUIRE_THAT(result, WithinAbs(0.85f, 0.001f));
}

TEST_CASE("test_apply_relationship_modifier_zero_trust", "[npc_behavior][tier5]") {
    float result = NpcBehaviorModule::apply_relationship_modifier(1.0f, 0.0f, 0.3f);
    // 1.0 * (1.0 + 0.0) = 1.0
    REQUIRE_THAT(result, WithinAbs(1.0f, 0.001f));
}

// ===========================================================================
// Test 27: NPCs processed in deterministic order within province
// ===========================================================================

TEST_CASE("test_npcs_processed_in_id_ascending_order", "[npc_behavior][tier5]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));

    // Add NPCs in reverse order.
    state.significant_npcs.push_back(make_test_npc(30, 0));
    state.significant_npcs.push_back(make_test_npc(10, 0));
    state.significant_npcs.push_back(make_test_npc(20, 0));

    NpcBehaviorModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    REQUIRE(delta.npc_deltas.size() == 3);
    // Deltas should be in id ascending order.
    REQUIRE(delta.npc_deltas[0].npc_id == 10);
    REQUIRE(delta.npc_deltas[1].npc_id == 20);
    REQUIRE(delta.npc_deltas[2].npc_id == 30);
}

// ===========================================================================
// Test 28: NPCs from other provinces not processed
// ===========================================================================

TEST_CASE("test_npcs_from_other_provinces_not_processed", "[npc_behavior][tier5]") {
    auto state = make_test_world_state(1);
    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    state.significant_npcs.push_back(make_test_npc(1, 0));  // province 0
    state.significant_npcs.push_back(make_test_npc(2, 1));  // province 1
    state.significant_npcs.push_back(make_test_npc(3, 0));  // province 0

    NpcBehaviorModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Only NPCs in province 0 should produce deltas.
    REQUIRE(delta.npc_deltas.size() == 2);
    REQUIRE(delta.npc_deltas[0].npc_id == 1);
    REQUIRE(delta.npc_deltas[1].npc_id == 3);
}

// ===========================================================================
// Constants Verification
// ===========================================================================

TEST_CASE("test_npc_behavior_constants", "[npc_behavior][tier5]") {
    REQUIRE_THAT(NpcBehaviorModule::Constants::inaction_threshold,
                 WithinAbs(0.10f, 0.001f));
    REQUIRE_THAT(NpcBehaviorModule::Constants::min_risk_discount,
                 WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(NpcBehaviorModule::Constants::risk_sensitivity_coeff,
                 WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(NpcBehaviorModule::Constants::trust_ev_bonus,
                 WithinAbs(0.3f, 0.001f));
    REQUIRE_THAT(NpcBehaviorModule::Constants::memory_decay_rate,
                 WithinAbs(0.002f, 0.0001f));
    REQUIRE_THAT(NpcBehaviorModule::Constants::memory_decay_floor,
                 WithinAbs(0.01f, 0.001f));
    REQUIRE_THAT(NpcBehaviorModule::Constants::knowledge_confidence_decay_rate,
                 WithinAbs(0.001f, 0.0001f));
    REQUIRE_THAT(NpcBehaviorModule::Constants::motivation_shift_rate,
                 WithinAbs(0.001f, 0.0001f));
    REQUIRE(MAX_MEMORY_ENTRIES == 500);
}
