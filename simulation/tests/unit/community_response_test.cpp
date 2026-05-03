#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "core/config/package_config.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/community_response/community_response_module.h"

using Catch::Matchers::WithinAbs;
using namespace econlife;

// ---------------------------------------------------------------------------
// Static utility tests
// ---------------------------------------------------------------------------

TEST_CASE("test_compute_cohesion_sample", "[community_response][tier6]") {
    // social_capital=50/100=0.5, stability=0.8 -> 0.5*0.8 = 0.4
    float s = CommunityResponseModule::compute_cohesion_sample(50.0f, 100.0f, 0.8f);
    REQUIRE_THAT(s, WithinAbs(0.4f, 0.001f));
}

TEST_CASE("test_compute_cohesion_sample_clamped", "[community_response][tier6]") {
    // social_capital > max: clamped to 1.0
    float s = CommunityResponseModule::compute_cohesion_sample(200.0f, 100.0f, 0.5f);
    REQUIRE_THAT(s, WithinAbs(0.5f, 0.001f));
}

TEST_CASE("test_compute_grievance_contribution", "[community_response][tier6]") {
    std::vector<MemoryEntry> log;

    // witnessed_illegal_activity: weight 1.0, emotional_weight = -0.8
    MemoryEntry e1;
    e1.type = MemoryType::witnessed_illegal_activity;
    e1.emotional_weight = -0.8f;
    e1.decay = 0.5f;
    log.push_back(e1);

    // employment_negative: weight 0.5, emotional_weight = -0.6
    MemoryEntry e2;
    e2.type = MemoryType::employment_negative;
    e2.emotional_weight = -0.6f;
    e2.decay = 0.5f;
    log.push_back(e2);

    // interaction: weight 0.0, should not contribute
    MemoryEntry e3;
    e3.type = MemoryType::interaction;
    e3.emotional_weight = -1.0f;
    e3.decay = 0.5f;
    log.push_back(e3);

    float g = CommunityResponseModule::compute_grievance_contribution(log, 0.01f);
    // 0.8 * 1.0 + 0.6 * 0.5 + 0 = 0.8 + 0.3 = 1.1
    REQUIRE_THAT(g, WithinAbs(1.1f, 0.01f));
}

TEST_CASE("test_memory_type_grievance_weight", "[community_response][tier6]") {
    // Direct harm = 1.0
    REQUIRE_THAT(CommunityResponseModule::memory_type_grievance_weight(
                     MemoryType::witnessed_illegal_activity),
                 WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(CommunityResponseModule::memory_type_grievance_weight(
                     MemoryType::witnessed_safety_violation),
                 WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(
        CommunityResponseModule::memory_type_grievance_weight(MemoryType::witnessed_wage_theft),
        WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(CommunityResponseModule::memory_type_grievance_weight(MemoryType::physical_hazard),
                 WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(
        CommunityResponseModule::memory_type_grievance_weight(MemoryType::retaliation_experienced),
        WithinAbs(1.0f, 0.001f));

    // Economic harm = 0.5
    REQUIRE_THAT(
        CommunityResponseModule::memory_type_grievance_weight(MemoryType::employment_negative),
        WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(
        CommunityResponseModule::memory_type_grievance_weight(MemoryType::facility_quality),
        WithinAbs(0.5f, 0.001f));

    // Others = 0.0
    REQUIRE_THAT(CommunityResponseModule::memory_type_grievance_weight(MemoryType::interaction),
                 WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(CommunityResponseModule::memory_type_grievance_weight(MemoryType::observation),
                 WithinAbs(0.0f, 0.001f));
}

TEST_CASE("test_compute_resource_access_sample", "[community_response][tier6]") {
    // capital=5000/10000=0.5, social=25/50=0.5 -> 1.0 (clamped)
    float r =
        CommunityResponseModule::compute_resource_access_sample(5000.0f, 10000.0f, 25.0f, 50.0f);
    REQUIRE_THAT(r, WithinAbs(1.0f, 0.001f));

    // capital=1000/10000=0.1, social=10/50=0.2 -> 0.3
    float r2 =
        CommunityResponseModule::compute_resource_access_sample(1000.0f, 10000.0f, 10.0f, 50.0f);
    REQUIRE_THAT(r2, WithinAbs(0.3f, 0.001f));
}

TEST_CASE("test_ema_update", "[community_response][tier6]") {
    // EMA: current * (1-alpha) + sample * alpha
    // 0.5 * 0.95 + 1.0 * 0.05 = 0.475 + 0.05 = 0.525
    float result = CommunityResponseModule::ema_update(0.5f, 1.0f, 0.05f);
    REQUIRE_THAT(result, WithinAbs(0.525f, 0.001f));
}

TEST_CASE("test_evaluate_stage_all_thresholds", "[community_response][tier6]") {
    // Quiescent: low grievance
    auto s0 = CommunityResponseModule::evaluate_stage(0.10f, 0.50f, 0.50f, 0.50f, false);
    REQUIRE(s0 == CommunityResponseStage::quiescent);

    // Informal complaint: grievance >= 0.15, cohesion >= 0.10
    auto s1 = CommunityResponseModule::evaluate_stage(0.16f, 0.15f, 0.50f, 0.50f, false);
    REQUIRE(s1 == CommunityResponseStage::informal_complaint);

    // Organized complaint: grievance >= 0.28, cohesion >= 0.25
    auto s2 = CommunityResponseModule::evaluate_stage(0.30f, 0.30f, 0.50f, 0.50f, false);
    REQUIRE(s2 == CommunityResponseStage::organized_complaint);

    // Political mobilization: grievance >= 0.42, institutional_trust >= 0.20
    auto s3 = CommunityResponseModule::evaluate_stage(0.45f, 0.30f, 0.25f, 0.50f, false);
    REQUIRE(s3 == CommunityResponseStage::political_mobilization);

    // Economic resistance: grievance >= 0.56, resource_access >= 0.25
    auto s4 = CommunityResponseModule::evaluate_stage(0.60f, 0.30f, 0.25f, 0.30f, false);
    REQUIRE(s4 == CommunityResponseStage::economic_resistance);

    // Direct action: grievance >= 0.70, cohesion >= 0.45
    auto s5 = CommunityResponseModule::evaluate_stage(0.75f, 0.50f, 0.25f, 0.30f, false);
    REQUIRE(s5 == CommunityResponseStage::direct_action);

    // Sustained opposition: grievance >= 0.85, resource_access >= 0.35, has_leadership
    auto s6 = CommunityResponseModule::evaluate_stage(0.90f, 0.50f, 0.25f, 0.40f, true);
    REQUIRE(s6 == CommunityResponseStage::sustained_opposition);
}

TEST_CASE("test_apply_stage_transition_up_and_down", "[community_response][tier6]") {
    // Advance from quiescent to organized_complaint target -> only goes to informal
    auto up = CommunityResponseModule::apply_stage_transition(
        CommunityResponseStage::quiescent, CommunityResponseStage::organized_complaint, true,
        false);
    REQUIRE(up == CommunityResponseStage::informal_complaint);

    // Regress from direct_action to quiescent target -> only goes to economic_resistance
    auto down = CommunityResponseModule::apply_stage_transition(
        CommunityResponseStage::direct_action, CommunityResponseStage::quiescent, true, false);
    REQUIRE(down == CommunityResponseStage::economic_resistance);
}

TEST_CASE("test_stage_cannot_skip", "[community_response][tier6]") {
    // Target jumps to sustained_opposition from quiescent
    auto result = CommunityResponseModule::apply_stage_transition(
        CommunityResponseStage::quiescent, CommunityResponseStage::sustained_opposition, true,
        false);
    // Should only advance one step
    REQUIRE(result == CommunityResponseStage::informal_complaint);
}

TEST_CASE("test_stage_regression_rate_limited", "[community_response][tier6]") {
    // When can_regress = false, no regression allowed
    auto result = CommunityResponseModule::apply_stage_transition(
        CommunityResponseStage::direct_action, CommunityResponseStage::quiescent,
        false,  // cannot regress (cooldown not expired)
        false);
    REQUIRE(result == CommunityResponseStage::direct_action);
}

TEST_CASE("test_sustained_opposition_no_auto_regress", "[community_response][tier6]") {
    // With opposition org formed, sustained_opposition cannot regress
    auto result = CommunityResponseModule::apply_stage_transition(
        CommunityResponseStage::sustained_opposition, CommunityResponseStage::quiescent,
        true,   // cooldown expired
        true);  // opposition org exists
    REQUIRE(result == CommunityResponseStage::sustained_opposition);
}

// ---------------------------------------------------------------------------
// Integration tests
// ---------------------------------------------------------------------------

TEST_CASE("test_cohesion_ema_smoothing", "[community_response][tier6]") {
    CommunityResponseModule module;

    WorldState state{};
    state.current_tick = 1;
    state.world_seed = 42;

    Province p{};
    p.id = 0;
    p.community.cohesion = 0.5f;
    p.community.grievance_level = 0.0f;
    p.community.institutional_trust = 0.5f;
    p.community.resource_access = 0.5f;
    p.community.response_stage = 0;
    p.political.corruption_index = 0.0f;
    state.provinces.push_back(p);

    // Add NPCs with high social_capital and stability
    for (uint32_t i = 1; i <= 10; ++i) {
        NPC npc{};
        npc.id = i;
        npc.home_province_id = 0;
        npc.status = NPCStatus::active;
        npc.social_capital = 80.0f;  // high
        npc.capital = 5000.0f;
        npc.motivations.weights = {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.3f, 0.0f};
        // stability weight (index 6) = 0.3
        state.significant_npcs.push_back(npc);
    }

    // Run multiple ticks to see EMA convergence
    float initial_cohesion = 0.5f;
    for (int tick = 1; tick <= 20; ++tick) {
        state.current_tick = tick;
        DeltaBuffer delta{};
        module.execute(state, delta);

        // Apply deltas
        for (const auto& rd : delta.region_deltas) {
            if (rd.region_id == 0 && rd.cohesion_delta.has_value()) {
                state.provinces[0].community.cohesion += rd.cohesion_delta.value();
            }
        }
    }

    // Cohesion should have shifted toward the sample mean.
    // Sample per NPC: (80/100) * 0.3 = 0.24
    // After 20 ticks of EMA(0.05): converges toward 0.24 from 0.5
    float final_cohesion = state.provinces[0].community.cohesion;
    REQUIRE(final_cohesion < initial_cohesion);
    REQUIRE(final_cohesion > 0.20f);  // not yet fully converged
}

TEST_CASE("test_grievance_from_negative_memory", "[community_response][tier6]") {
    CommunityResponseModule module;

    WorldState state{};
    state.current_tick = 1;
    state.world_seed = 42;

    Province p{};
    p.id = 0;
    p.community.cohesion = 0.5f;
    p.community.grievance_level = 0.0f;
    p.community.institutional_trust = 0.5f;
    p.community.resource_access = 0.5f;
    p.community.response_stage = 0;
    p.political.corruption_index = 0.0f;
    state.provinces.push_back(p);

    // Add NPCs with grievance-generating memories
    for (uint32_t i = 1; i <= 5; ++i) {
        NPC npc{};
        npc.id = i;
        npc.home_province_id = 0;
        npc.status = NPCStatus::active;
        npc.social_capital = 30.0f;
        npc.capital = 1000.0f;
        npc.motivations.weights = {0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f};

        // witnessed_illegal_activity memory: weight 1.0
        MemoryEntry entry{};
        entry.type = MemoryType::witnessed_illegal_activity;
        entry.emotional_weight = -0.8f;
        entry.decay = 0.5f;
        npc.memory_log.push_back(entry);

        state.significant_npcs.push_back(npc);
    }

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have generated some grievance delta
    bool found_grievance_delta = false;
    for (const auto& rd : delta.region_deltas) {
        if (rd.region_id == 0 && rd.grievance_delta.has_value()) {
            REQUIRE(rd.grievance_delta.value() > 0.0f);
            found_grievance_delta = true;
        }
    }
    REQUIRE(found_grievance_delta);
}

TEST_CASE("test_grievance_shock_bypasses_ema", "[community_response][tier6]") {
    CommunityResponseModule module;

    WorldState state{};
    state.current_tick = 1;
    state.world_seed = 42;

    Province p{};
    p.id = 0;
    p.community.cohesion = 0.5f;
    p.community.grievance_level = 0.0f;
    p.community.institutional_trust = 0.5f;
    p.community.resource_access = 0.5f;
    p.community.response_stage = 0;
    p.political.corruption_index = 0.0f;
    state.provinces.push_back(p);

    // Add NPCs with massive grievance (causes shock)
    for (uint32_t i = 1; i <= 10; ++i) {
        NPC npc{};
        npc.id = i;
        npc.home_province_id = 0;
        npc.status = NPCStatus::active;
        npc.social_capital = 30.0f;
        npc.capital = 1000.0f;
        npc.motivations.weights = {0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f};

        // Many high-weight negative memories
        for (int m = 0; m < 20; ++m) {
            MemoryEntry entry{};
            entry.type = MemoryType::witnessed_illegal_activity;
            entry.emotional_weight = -1.0f;
            entry.decay = 0.5f;
            npc.memory_log.push_back(entry);
        }

        state.significant_npcs.push_back(npc);
    }

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Grievance should have increased significantly (shock bypasses EMA)
    for (const auto& rd : delta.region_deltas) {
        if (rd.region_id == 0 && rd.grievance_delta.has_value()) {
            // With shock, delta should be much larger than EMA would produce
            REQUIRE(rd.grievance_delta.value() >
                    CommunityResponseConfig{}.grievance_shock_threshold);
        }
    }
}

TEST_CASE("test_stage_transition_informal_to_organized", "[community_response][tier6]") {
    // Test evaluate_stage transition: grievance 0.30, cohesion 0.30 -> organized_complaint
    auto stage = CommunityResponseModule::evaluate_stage(0.30f, 0.30f, 0.50f, 0.50f, false);
    REQUIRE(stage == CommunityResponseStage::organized_complaint);
}

TEST_CASE("test_economic_resistance_revenue_penalty", "[community_response][tier6]") {
    // Verify the constant is defined correctly
    REQUIRE_THAT(CommunityResponseConfig{}.resistance_revenue_penalty, WithinAbs(-0.15f, 0.001f));
}

TEST_CASE("test_historical_trauma_sets_floors", "[community_response][tier6]") {
    // trauma_index = 0.60
    // grievance floor = 0.60 * 0.25 = 0.15
    // trust ceiling = 1.0 - 0.60 * 0.30 = 0.82
    float trauma = 0.60f;
    float grievance_floor = trauma * CommunityResponseConfig{}.trauma_grievance_floor_scale;
    float trust_ceiling = 1.0f - trauma * CommunityResponseConfig{}.trauma_trust_ceiling_scale;

    REQUIRE_THAT(grievance_floor, WithinAbs(0.15f, 0.001f));
    REQUIRE_THAT(trust_ceiling, WithinAbs(0.82f, 0.001f));
}
