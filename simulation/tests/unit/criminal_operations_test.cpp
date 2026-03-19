#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/criminal_operations/criminal_operations_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Static utility tests
// =============================================================================

TEST_CASE("Territory pressure from competing orgs", "[criminal_operations][tier7]") {
    CriminalOrganization org_a{};
    org_a.id = 1;
    org_a.dominance_by_province[0] = 0.30f;

    CriminalOrganization org_b{};
    org_b.id = 2;
    org_b.dominance_by_province[0] = 0.40f;

    std::vector<CriminalOrganization> all_orgs = {org_a, org_b};

    float pressure = CriminalOperationsModule::compute_territory_pressure(org_a, all_orgs);
    CHECK_THAT(pressure, WithinAbs(0.40f, 0.01f));
}

TEST_CASE("Territory pressure with no competitors", "[criminal_operations][tier7]") {
    CriminalOrganization org{};
    org.id = 1;
    org.dominance_by_province[0] = 0.50f;

    std::vector<CriminalOrganization> all_orgs = {org};

    float pressure = CriminalOperationsModule::compute_territory_pressure(org, all_orgs);
    CHECK_THAT(pressure, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Cash level computation", "[criminal_operations][tier7]") {
    // cash=15000, monthly_cost=5000, comfortable_months=3 => target=15000 => level=1.0
    float level = CriminalOperationsModule::compute_cash_level(15000.0f, 5000.0f, 3.0f);
    CHECK_THAT(level, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("Cash level low", "[criminal_operations][tier7]") {
    // cash=3000, monthly_cost=5000, comfortable_months=3 => target=15000 => level=0.2
    float level = CriminalOperationsModule::compute_cash_level(3000.0f, 5000.0f, 3.0f);
    CHECK_THAT(level, WithinAbs(0.2f, 0.01f));
}

TEST_CASE("Cash level with zero cost", "[criminal_operations][tier7]") {
    float level = CriminalOperationsModule::compute_cash_level(5000.0f, 0.0f, 3.0f);
    CHECK_THAT(level, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("Decision: reduce activity on high heat", "[criminal_operations][tier7]") {
    auto decision = CriminalOperationsModule::evaluate_decision(0.70f, 0.20f, 1.0f);
    CHECK(decision == CriminalStrategicDecision::reduce_activity);
}

TEST_CASE("Decision: initiate conflict on high pressure with cash", "[criminal_operations][tier7]") {
    auto decision = CriminalOperationsModule::evaluate_decision(0.20f, 0.65f, 1.5f);
    CHECK(decision == CriminalStrategicDecision::initiate_conflict);
}

TEST_CASE("Decision: reduce headcount on low cash", "[criminal_operations][tier7]") {
    auto decision = CriminalOperationsModule::evaluate_decision(0.20f, 0.20f, 0.30f);
    CHECK(decision == CriminalStrategicDecision::reduce_headcount);
}

TEST_CASE("Decision: expand territory when safe", "[criminal_operations][tier7]") {
    auto decision = CriminalOperationsModule::evaluate_decision(0.10f, 0.15f, 2.0f);
    CHECK(decision == CriminalStrategicDecision::expand_territory);
}

TEST_CASE("Decision: maintain as default", "[criminal_operations][tier7]") {
    // Moderate pressure, moderate heat, sufficient cash
    auto decision = CriminalOperationsModule::evaluate_decision(0.40f, 0.40f, 0.80f);
    CHECK(decision == CriminalStrategicDecision::maintain);
}

TEST_CASE("Decision offset spreads load", "[criminal_operations][tier7]") {
    // Verify different org IDs get different offsets (mod 90)
    uint8_t offset_1 = CriminalOperationsModule::compute_decision_offset(1);
    uint8_t offset_2 = CriminalOperationsModule::compute_decision_offset(91);
    CHECK(offset_1 == offset_2);  // 1 % 90 == 91 % 90

    uint8_t offset_3 = CriminalOperationsModule::compute_decision_offset(5);
    uint8_t offset_4 = CriminalOperationsModule::compute_decision_offset(10);
    CHECK(offset_3 != offset_4);
}

TEST_CASE("Conflict stage escalation chain", "[criminal_operations][tier7]") {
    auto s = TerritorialConflictStage::none;
    s = CriminalOperationsModule::advance_conflict_stage(s);
    CHECK(s == TerritorialConflictStage::economic);
    s = CriminalOperationsModule::advance_conflict_stage(s);
    CHECK(s == TerritorialConflictStage::intelligence_harassment);
    s = CriminalOperationsModule::advance_conflict_stage(s);
    CHECK(s == TerritorialConflictStage::property_violence);
    s = CriminalOperationsModule::advance_conflict_stage(s);
    CHECK(s == TerritorialConflictStage::personnel_violence);
    s = CriminalOperationsModule::advance_conflict_stage(s);
    CHECK(s == TerritorialConflictStage::open_warfare);
    s = CriminalOperationsModule::advance_conflict_stage(s);
    CHECK(s == TerritorialConflictStage::resolution);
    s = CriminalOperationsModule::advance_conflict_stage(s);
    CHECK(s == TerritorialConflictStage::none);
}

TEST_CASE("Initial dominance seed value", "[criminal_operations][tier7]") {
    CHECK_THAT(CriminalOperationsModule::initial_dominance_seed(),
               WithinAbs(0.05f, 0.001f));
}

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("Dormant org dominance decays", "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    CriminalOperationsModule module;

    CriminalOrganization org{};
    org.id = 1;
    org.member_npc_ids = {};  // dormant
    org.dominance_by_province[0] = 0.10f;
    org.strategic_decision_tick = 200;
    org.conflict_state = TerritorialConflictStage::none;
    org.conflict_rival_org_id = 0;
    module.organizations().push_back(org);

    DeltaBuffer delta{};
    module.execute(state, delta);

    CHECK(module.organizations()[0].dominance_by_province[0] <
          0.10f);
}

TEST_CASE("Personnel violence generates evidence", "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    Province prov{};
    prov.id = 0;
    state.provinces.push_back(prov);

    PlayerCharacter player{};
    player.id = 999;
    state.player = &player;

    CriminalOperationsModule module;

    CriminalOrganization org{};
    org.id = 1;
    org.leadership_npc_id = 10;
    org.member_npc_ids = {10, 11};
    org.dominance_by_province[0] = 0.30f;
    org.conflict_state = TerritorialConflictStage::personnel_violence;
    org.conflict_rival_org_id = 2;
    org.strategic_decision_tick = 200;
    module.organizations().push_back(org);

    // Rival org must exist
    CriminalOrganization rival{};
    rival.id = 2;
    rival.leadership_npc_id = 20;
    rival.member_npc_ids = {20, 21};
    rival.dominance_by_province[0] = 0.20f;
    rival.conflict_state = TerritorialConflictStage::none;
    rival.conflict_rival_org_id = 0;
    rival.strategic_decision_tick = 200;
    module.organizations().push_back(rival);

    DeltaBuffer delta{};
    module.execute(state, delta);

    // Should have generated evidence
    CHECK(!delta.evidence_deltas.empty());
    if (!delta.evidence_deltas.empty()) {
        CHECK(delta.evidence_deltas[0].new_token.has_value());
        CHECK(delta.evidence_deltas[0].new_token->type == EvidenceType::physical);
    }
}
