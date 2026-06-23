#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/config/package_config.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/criminal_operations/criminal_operations_module.h"

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
    auto decision =
        CriminalOperationsModule::evaluate_decision(0.70f, 0.20f, 1.0f, CriminalOperationsConfig{});
    CHECK(decision == CriminalStrategicDecision::reduce_activity);
}

TEST_CASE("LE heat reflects investigation pressure on org members, not local police",
          "[criminal_operations][tier7]") {
    CriminalOrganization org{};
    org.id = 1;
    org.leadership_npc_id = 100;
    org.member_npc_ids = {101, 102};
    org.dominance_by_province[0] = 0.5f;

    std::vector<NPC> npcs(4);
    npcs[0].id = 100;  // leader, light investigation
    npcs[0].status = NPCStatus::active;
    npcs[0].investigator_meter.current_level = 0.30f;
    npcs[1].id = 101;  // member under heavy investigation -> drives heat
    npcs[1].status = NPCStatus::active;
    npcs[1].investigator_meter.current_level = 0.80f;
    npcs[2].id = 102;
    npcs[2].status = NPCStatus::active;
    npcs[2].investigator_meter.current_level = 0.10f;
    npcs[3].id = 999;  // a non-member (e.g. police) with high social capital -> ignored
    npcs[3].status = NPCStatus::active;
    npcs[3].social_capital = 100.0f;
    npcs[3].investigator_meter.current_level = 0.0f;

    // Heat = peak investigation pressure across the org's people (0.80), NOT the
    // old local-police social_capital proxy (which would have read npc 999).
    CHECK_THAT(CriminalOperationsModule::compute_le_heat(org, npcs), WithinAbs(0.80f, 0.001f));
}

TEST_CASE("Decision: initiate conflict on high pressure with cash",
          "[criminal_operations][tier7]") {
    auto decision =
        CriminalOperationsModule::evaluate_decision(0.20f, 0.65f, 1.5f, CriminalOperationsConfig{});
    CHECK(decision == CriminalStrategicDecision::initiate_conflict);
}

TEST_CASE("Decision: reduce headcount on low cash", "[criminal_operations][tier7]") {
    auto decision = CriminalOperationsModule::evaluate_decision(0.20f, 0.20f, 0.30f,
                                                                CriminalOperationsConfig{});
    CHECK(decision == CriminalStrategicDecision::reduce_headcount);
}

TEST_CASE("Decision: expand territory when safe", "[criminal_operations][tier7]") {
    auto decision =
        CriminalOperationsModule::evaluate_decision(0.10f, 0.15f, 2.0f, CriminalOperationsConfig{});
    CHECK(decision == CriminalStrategicDecision::expand_territory);
}

TEST_CASE("Decision: maintain as default", "[criminal_operations][tier7]") {
    // Moderate pressure, moderate heat, sufficient cash
    auto decision = CriminalOperationsModule::evaluate_decision(0.40f, 0.40f, 0.80f,
                                                                CriminalOperationsConfig{});
    CHECK(decision == CriminalStrategicDecision::maintain);
}

TEST_CASE("Decision offset spreads load", "[criminal_operations][tier7]") {
    // Verify different org IDs get different offsets (mod 90)
    constexpr uint32_t qi = CriminalOperationsConfig{}.quarterly_interval;
    uint8_t offset_1 = CriminalOperationsModule::compute_decision_offset(1, qi);
    uint8_t offset_2 = CriminalOperationsModule::compute_decision_offset(91, qi);
    CHECK(offset_1 == offset_2);  // 1 % 90 == 91 % 90

    uint8_t offset_3 = CriminalOperationsModule::compute_decision_offset(5, qi);
    uint8_t offset_4 = CriminalOperationsModule::compute_decision_offset(10, qi);
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
    CHECK_THAT(CriminalOperationsModule::initial_dominance_seed(
                   CriminalOperationsConfig{}.expansion_initial_dominance),
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
    state.player = std::make_unique<PlayerCharacter>(player);

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

    CHECK(module.organizations()[0].dominance_by_province[0] < 0.10f);
}

TEST_CASE("Personnel violence generates evidence", "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    Province prov{};
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = 0;
    state.provinces.push_back(prov);

    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

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

// =============================================================================
// Organization formation bootstrap
// =============================================================================
// criminal_operations only ever loaded orgs from a save; world-gen creates the
// raw materials (criminal NPCs + criminal-sector businesses) but nothing
// assembled them. These tests cover the formation bootstrap that makes the
// subsystem live in a fresh game: one org per province with criminal NPCs.

namespace {
NPC make_npc(uint32_t id, NPCRole role, uint32_t province) {
    NPC n{};
    n.id = id;
    n.role = role;
    n.status = NPCStatus::active;
    n.current_province_id = province;
    n.home_province_id = province;
    return n;
}
}  // namespace

TEST_CASE("CriminalOps: formation assembles one org per criminal province",
          "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 0;

    Province prov{};
    prov.id = 0;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.cohort_stats->criminal_dominance_index = 0.05f;  // world-gen baseline
    state.provinces.push_back(std::move(prov));

    // Criminal NPCs (operator + enforcer) plus a non-criminal that must be
    // excluded; an enforcer with the lower id must NOT become leader.
    state.significant_npcs.push_back(make_npc(11, NPCRole::criminal_enforcer, 0));
    state.significant_npcs.push_back(make_npc(12, NPCRole::criminal_operator, 0));
    state.significant_npcs.push_back(make_npc(13, NPCRole::worker, 0));

    NPCBusiness front{};
    front.id = 5;
    front.province_id = 0;
    front.criminal_sector = true;
    state.npc_businesses.push_back(front);
    NPCBusiness legit{};
    legit.id = 6;
    legit.province_id = 0;
    legit.criminal_sector = false;
    state.npc_businesses.push_back(legit);

    CriminalOperationsModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.organizations().size() == 1);
    const CriminalOrganization& org = module.organizations()[0];
    REQUIRE(org.id == 1);                  // province_id + 1
    REQUIRE(org.leadership_npc_id == 12);  // the criminal_operator, not the lower-id enforcer
    REQUIRE(org.member_npc_ids == std::vector<uint32_t>{11, 12});
    REQUIRE(org.income_source_ids == std::vector<uint32_t>{5});  // only the criminal-sector front
    REQUIRE_THAT(org.dominance_by_province.at(0), Catch::Matchers::WithinAbs(0.05f, 1e-5f));

    // Idempotent: a second tick must not duplicate the org.
    DeltaBuffer delta2{};
    module.execute(state, delta2);
    REQUIRE(module.organizations().size() == 1);
}

TEST_CASE("CriminalOps: province with no criminal NPCs forms no org",
          "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 0;
    Province prov{};
    prov.id = 0;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    state.provinces.push_back(std::move(prov));
    state.significant_npcs.push_back(make_npc(10, NPCRole::worker, 0));

    CriminalOperationsModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.organizations().empty());
}

TEST_CASE("CriminalOps: formation emits a racket seed targeting a legit business",
          "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 0;
    Province prov{};
    prov.id = 0;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    state.provinces.push_back(std::move(prov));
    state.significant_npcs.push_back(make_npc(12, NPCRole::criminal_operator, 0));

    NPCBusiness front{};  // criminal -> income source, not a target
    front.id = 5;
    front.province_id = 0;
    front.criminal_sector = true;
    state.npc_businesses.push_back(front);
    NPCBusiness legit_hi{};
    legit_hi.id = 9;
    legit_hi.province_id = 0;
    legit_hi.criminal_sector = false;
    state.npc_businesses.push_back(legit_hi);
    NPCBusiness legit_lo{};  // lowest-id legit -> the racket target
    legit_lo.id = 7;
    legit_lo.province_id = 0;
    legit_lo.criminal_sector = false;
    state.npc_businesses.push_back(legit_lo);

    CriminalOperationsModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.new_racket_seeds.size() == 1);
    REQUIRE(delta.new_racket_seeds[0].criminal_org_id == 1);
    REQUIRE(delta.new_racket_seeds[0].target_business_id == 7);  // lowest-id legit
    REQUIRE(delta.new_racket_seeds[0].province_id == 0);
}

TEST_CASE("CriminalOps: formation emits a laundering seed for illicit income",
          "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 0;
    Province prov{};
    prov.id = 0;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    state.provinces.push_back(std::move(prov));
    state.significant_npcs.push_back(make_npc(12, NPCRole::criminal_operator, 0));

    NPCBusiness front{};
    front.id = 5;
    front.province_id = 0;
    front.criminal_sector = true;
    front.revenue_per_tick = 100.0f;
    state.npc_businesses.push_back(front);

    CriminalOperationsModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.new_laundering_seeds.size() == 1);
    REQUIRE(delta.new_laundering_seeds[0].actor_id == 12);  // org leadership
    REQUIRE(delta.new_laundering_seeds[0].destination_business_id == 5);
    // dirty = revenue(100) * launder_seed_income_ticks(30) = 3000.
    REQUIRE_THAT(delta.new_laundering_seeds[0].dirty_amount,
                 Catch::Matchers::WithinAbs(3000.0f, 1e-2f));
}

TEST_CASE("CriminalOps: no criminal businesses -> no laundering seed",
          "[criminal_operations][tier7]") {
    WorldState state{};
    state.current_tick = 0;
    Province prov{};
    prov.id = 0;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    state.provinces.push_back(std::move(prov));
    state.significant_npcs.push_back(make_npc(12, NPCRole::criminal_operator, 0));
    // Only a legitimate business present.
    NPCBusiness legit{};
    legit.id = 7;
    legit.province_id = 0;
    legit.criminal_sector = false;
    legit.revenue_per_tick = 100.0f;
    state.npc_businesses.push_back(legit);

    CriminalOperationsModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.new_laundering_seeds.empty());
}
