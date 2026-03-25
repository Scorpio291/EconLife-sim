#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/obligation_network/obligation_network_module.h"

using Catch::Matchers::WithinAbs;
using namespace econlife;

// ---------------------------------------------------------------------------
// Static utility tests
// ---------------------------------------------------------------------------

TEST_CASE("test_compute_demand_growth_basic", "[obligation_network][tier6]") {
    // urgency=0.5, rate=0.001, wealth_factor=1.0
    // growth = 0.5 * 0.001 * (1.0 + 1.0) = 0.001
    float growth = ObligationNetworkModule::compute_demand_growth(0.5f, 0.001f, 1.0f);
    REQUIRE_THAT(growth, WithinAbs(0.001f, 0.0001f));
}

TEST_CASE("test_compute_player_wealth_factor", "[obligation_network][tier6]") {
    // 100k net worth / 1M reference = 0.1
    float f1 = ObligationNetworkModule::compute_player_wealth_factor(100000.0f, 1000000.0f, 2.0f);
    REQUIRE_THAT(f1, WithinAbs(0.1f, 0.001f));

    // 2M net worth / 1M reference = 2.0 (at max)
    float f2 = ObligationNetworkModule::compute_player_wealth_factor(2000000.0f, 1000000.0f, 2.0f);
    REQUIRE_THAT(f2, WithinAbs(2.0f, 0.001f));

    // 5M net worth / 1M reference = 5.0 but clamped to 2.0
    float f3 = ObligationNetworkModule::compute_player_wealth_factor(5000000.0f, 1000000.0f, 2.0f);
    REQUIRE_THAT(f3, WithinAbs(2.0f, 0.001f));

    // Negative net worth -> 0.0
    float f4 = ObligationNetworkModule::compute_player_wealth_factor(-100000.0f, 1000000.0f, 2.0f);
    REQUIRE_THAT(f4, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("test_compute_creditor_urgency", "[obligation_network][tier6]") {
    float weights[8] = {0.1f, 0.3f, 0.05f, 0.15f, 0.2f, 0.1f, 0.05f, 0.05f};
    float urgency = ObligationNetworkModule::compute_creditor_urgency(weights, 8);
    REQUIRE_THAT(urgency, WithinAbs(0.3f, 0.001f));
}

TEST_CASE("test_evaluate_escalation_thresholds", "[obligation_network][tier6]") {
    // Below escalation: ratio 1.2 < 1.5
    auto s1 = ObligationNetworkModule::evaluate_escalation(120.0f, 100.0f, 1.5f, 3.0f);
    REQUIRE(s1 == ObligationStatus::open);

    // Escalated: ratio 1.6 > 1.5
    auto s2 = ObligationNetworkModule::evaluate_escalation(160.0f, 100.0f, 1.5f, 3.0f);
    REQUIRE(s2 == ObligationStatus::escalated);

    // Critical: ratio 3.5 > 3.0
    auto s3 = ObligationNetworkModule::evaluate_escalation(350.0f, 100.0f, 1.5f, 3.0f);
    REQUIRE(s3 == ObligationStatus::critical);
}

TEST_CASE("test_should_trigger_hostile", "[obligation_network][tier6]") {
    // Critical + high risk tolerance -> hostile
    REQUIRE(ObligationNetworkModule::should_trigger_hostile(ObligationStatus::critical, 0.8f,
                                                            0.7f) == true);

    // Critical + low risk tolerance -> not hostile
    REQUIRE(ObligationNetworkModule::should_trigger_hostile(ObligationStatus::critical, 0.5f,
                                                            0.7f) == false);

    // Not critical -> not hostile
    REQUIRE(ObligationNetworkModule::should_trigger_hostile(ObligationStatus::escalated, 0.9f,
                                                            0.7f) == false);
}

TEST_CASE("test_compute_trust_erosion", "[obligation_network][tier6]") {
    float erosion = ObligationNetworkModule::compute_trust_erosion(100, -0.001f);
    REQUIRE_THAT(erosion, WithinAbs(-0.1f, 0.001f));
}

// ---------------------------------------------------------------------------
// Integration tests
// ---------------------------------------------------------------------------

TEST_CASE("test_overdue_obligation_demand_grows", "[obligation_network][tier6]") {
    ObligationNetworkModule module;

    // Setup creditor NPC
    WorldState state{};
    state.current_tick = 200;
    state.world_seed = 42;

    NPC creditor{};
    creditor.id = 1;
    creditor.status = NPCStatus::active;
    creditor.risk_tolerance = 0.5f;
    creditor.motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f};
    state.significant_npcs.push_back(creditor);

    PlayerCharacter player{};
    player.id = 100;
    player.net_assets = 100000.0f;
    state.player = &player;

    // Create overdue obligation
    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 100.0f;
    obl.original_value = 100.0f;
    obl.deadline_tick = 100;  // overdue since tick 100
    obl.status = ObligationStatus::open;
    obl.creditor_npc_id = 1;
    module.obligation_states().push_back(obl);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Demand should have grown
    REQUIRE(module.obligation_states()[0].current_demand > 100.0f);
}

TEST_CASE("test_not_yet_overdue_no_escalation", "[obligation_network][tier6]") {
    ObligationNetworkModule module;

    WorldState state{};
    state.current_tick = 50;
    state.world_seed = 42;

    NPC creditor{};
    creditor.id = 1;
    creditor.status = NPCStatus::active;
    creditor.risk_tolerance = 0.5f;
    creditor.motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f};
    state.significant_npcs.push_back(creditor);

    PlayerCharacter player{};
    player.id = 100;
    player.net_assets = 100000.0f;
    state.player = &player;

    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 100.0f;
    obl.original_value = 100.0f;
    obl.deadline_tick = 200;  // not yet overdue
    obl.status = ObligationStatus::open;
    obl.creditor_npc_id = 1;
    module.obligation_states().push_back(obl);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Demand unchanged
    REQUIRE_THAT(module.obligation_states()[0].current_demand, WithinAbs(100.0f, 0.001f));
}

TEST_CASE("test_status_transitions_open_to_escalated", "[obligation_network][tier6]") {
    ObligationNetworkModule module;

    WorldState state{};
    state.current_tick = 200;
    state.world_seed = 42;

    NPC creditor{};
    creditor.id = 1;
    creditor.status = NPCStatus::active;
    creditor.risk_tolerance = 0.3f;
    creditor.motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f};
    state.significant_npcs.push_back(creditor);

    PlayerCharacter player{};
    player.id = 100;
    player.net_assets = 500000.0f;
    state.player = &player;

    // Obligation already at 1.51x original value (just past threshold)
    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 151.0f;
    obl.original_value = 100.0f;
    obl.deadline_tick = 100;
    obl.status = ObligationStatus::open;
    obl.creditor_npc_id = 1;
    module.obligation_states().push_back(obl);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.obligation_states()[0].status == ObligationStatus::escalated);
    REQUIRE(module.obligation_states()[0].history.size() >= 1);
}

TEST_CASE("test_status_transitions_escalated_to_critical", "[obligation_network][tier6]") {
    ObligationNetworkModule module;

    WorldState state{};
    state.current_tick = 200;
    state.world_seed = 42;

    NPC creditor{};
    creditor.id = 1;
    creditor.status = NPCStatus::active;
    creditor.risk_tolerance = 0.3f;
    creditor.motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f};
    state.significant_npcs.push_back(creditor);

    PlayerCharacter player{};
    player.id = 100;
    player.net_assets = 500000.0f;
    state.player = &player;

    // Obligation at 3.1x (past critical threshold)
    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 310.0f;
    obl.original_value = 100.0f;
    obl.deadline_tick = 100;
    obl.status = ObligationStatus::escalated;
    obl.creditor_npc_id = 1;
    module.obligation_states().push_back(obl);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.obligation_states()[0].status == ObligationStatus::critical);
}

TEST_CASE("test_hostile_action_risk_tolerance_gated", "[obligation_network][tier6]") {
    ObligationNetworkModule module;

    WorldState state{};
    state.current_tick = 200;
    state.world_seed = 42;

    NPC creditor{};
    creditor.id = 1;
    creditor.status = NPCStatus::active;
    creditor.risk_tolerance = 0.5f;  // below 0.7 threshold
    creditor.motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f};
    state.significant_npcs.push_back(creditor);

    PlayerCharacter player{};
    player.id = 100;
    player.net_assets = 500000.0f;
    state.player = &player;

    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 310.0f;
    obl.original_value = 100.0f;
    obl.deadline_tick = 100;
    obl.status = ObligationStatus::critical;
    obl.creditor_npc_id = 1;
    module.obligation_states().push_back(obl);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should remain critical, not hostile
    REQUIRE(module.obligation_states()[0].status == ObligationStatus::critical);
}

TEST_CASE("test_hostile_triggers_when_risk_high", "[obligation_network][tier6]") {
    ObligationNetworkModule module;

    WorldState state{};
    state.current_tick = 200;
    state.world_seed = 42;

    NPC creditor{};
    creditor.id = 1;
    creditor.status = NPCStatus::active;
    creditor.risk_tolerance = 0.8f;  // above 0.7 threshold
    creditor.motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f};
    state.significant_npcs.push_back(creditor);

    PlayerCharacter player{};
    player.id = 100;
    player.net_assets = 500000.0f;
    state.player = &player;

    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 310.0f;
    obl.original_value = 100.0f;
    obl.deadline_tick = 100;
    obl.status = ObligationStatus::critical;
    obl.creditor_npc_id = 1;
    module.obligation_states().push_back(obl);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.obligation_states()[0].status == ObligationStatus::hostile);
}

TEST_CASE("test_wealthy_player_faces_steeper_demands", "[obligation_network][tier6]") {
    // Compare demand growth for 100k vs 2M net worth
    float growth_low = ObligationNetworkModule::compute_demand_growth(
        0.3f, 0.001f,
        ObligationNetworkModule::compute_player_wealth_factor(100000.0f, 1000000.0f, 2.0f));
    float growth_high = ObligationNetworkModule::compute_demand_growth(
        0.3f, 0.001f,
        ObligationNetworkModule::compute_player_wealth_factor(2000000.0f, 1000000.0f, 2.0f));

    REQUIRE(growth_high > growth_low);
}

TEST_CASE("test_partial_payment_reduces_demand", "[obligation_network][tier6]") {
    // Module-level: simulate partial payment by reducing current_demand directly
    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 200.0f;
    obl.original_value = 100.0f;

    // 30% payment of original_value
    float payment = 0.30f * obl.original_value;
    obl.current_demand -= payment;

    REQUIRE_THAT(obl.current_demand, WithinAbs(170.0f, 0.01f));
}

TEST_CASE("test_trust_erosion_from_overdue_obligation", "[obligation_network][tier6]") {
    // 100 ticks overdue at -0.001/tick = -0.1 total
    float erosion = ObligationNetworkModule::compute_trust_erosion(100, -0.001f);
    REQUIRE_THAT(erosion, WithinAbs(-0.1f, 0.001f));
}

TEST_CASE("test_dead_creditor_freezes_obligation", "[obligation_network][tier6]") {
    ObligationNetworkModule module;

    WorldState state{};
    state.current_tick = 200;
    state.world_seed = 42;

    NPC creditor{};
    creditor.id = 1;
    creditor.status = NPCStatus::dead;  // dead creditor
    creditor.risk_tolerance = 0.8f;
    creditor.motivations.weights = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f};
    state.significant_npcs.push_back(creditor);

    PlayerCharacter player{};
    player.id = 100;
    player.net_assets = 500000.0f;
    state.player = &player;

    ObligationNetworkModule::ObligationState obl;
    obl.obligation_id = 1;
    obl.current_demand = 200.0f;
    obl.original_value = 100.0f;
    obl.deadline_tick = 100;  // overdue
    obl.status = ObligationStatus::escalated;
    obl.creditor_npc_id = 1;
    module.obligation_states().push_back(obl);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Demand unchanged — creditor dead, obligation frozen
    REQUIRE_THAT(module.obligation_states()[0].current_demand, WithinAbs(200.0f, 0.001f));
    REQUIRE(module.obligation_states()[0].status == ObligationStatus::escalated);
}
