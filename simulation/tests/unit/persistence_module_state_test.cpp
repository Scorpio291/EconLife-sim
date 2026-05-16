// v7 module-private state persistence — exercises the
// ITickModule::serialize_state / deserialize_state hook through
// PersistenceModule's optional-modules overload.
//
// Covers eleven V1 opt-ins. Round 1 (6 modules): random_events,
// protection_rackets, real_estate, legal_process, money_laundering,
// labor_market. Round 2 (5 modules): investigator_engine, banking,
// criminal_operations, informant_system, drug_economy.

#include <catch2/catch_test_macros.hpp>

#include "core/world_state/player.h"  // SkillDomain
#include "core/world_state/world_state.h"
#include "modules/banking/banking_module.h"
#include "modules/community_response/community_response_module.h"
#include "modules/criminal_operations/criminal_operations_module.h"
#include "modules/drug_economy/drug_economy_module.h"
#include "modules/facility_signals/facility_signals_types.h"  // InvestigatorMeterStatus
#include "modules/government_budget/government_budget_module.h"
#include "modules/healthcare/healthcare_module.h"
#include "modules/informant_system/informant_system_module.h"
#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/media_system/media_system_module.h"
#include "modules/labor_market/labor_market_module.h"
#include "modules/legal_process/legal_process_module.h"
#include "modules/money_laundering/money_laundering_module.h"
#include "modules/persistence/persistence_module.h"
#include "modules/protection_rackets/protection_rackets_module.h"
#include "modules/random_events/random_events_module.h"
#include "modules/real_estate/real_estate_module.h"

using namespace econlife;

namespace {

WorldState minimal_world() {
    WorldState w{};
    w.current_tick = 1;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;
    return w;
}

}  // namespace

TEST_CASE("v7 module-state: random_events active_events round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RandomEventsModule mod;

    ActiveRandomEvent e1{};
    e1.id = 7;
    e1.template_id = "drought";
    e1.template_key = "drought";
    e1.province_id = 0;
    e1.category = EventCategory::natural;
    e1.severity = 0.42f;
    e1.started_tick = 10;
    e1.end_tick = 100;
    e1.evidence_generated = false;
    e1.effects_applied_this_tick = true;
    mod.add_active_event(e1);

    ActiveRandomEvent e2{};
    e2.id = 8;
    e2.template_id = "factory_fire";
    e2.template_key = "factory_fire";
    e2.province_id = 2;
    e2.category = EventCategory::accident;
    e2.severity = 0.9f;
    e2.started_tick = 12;
    e2.end_tick = 15;
    e2.evidence_generated = true;
    e2.effects_applied_this_tick = false;
    mod.add_active_event(e2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    RandomEventsModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    const auto& restored_events = restored_mod.active_events();
    REQUIRE(restored_events.size() == 2);
    REQUIRE(restored_events[0].id == 7);
    REQUIRE(restored_events[0].template_id == "drought");
    REQUIRE(restored_events[0].severity == 0.42f);
    REQUIRE(restored_events[0].end_tick == 100);
    REQUIRE(restored_events[1].id == 8);
    REQUIRE(restored_events[1].template_id == "factory_fire");
    REQUIRE(restored_events[1].category == EventCategory::accident);
    REQUIRE(restored_events[1].evidence_generated == true);
}

TEST_CASE("v7 module-state: protection_rackets round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    ProtectionRacketsModule mod;

    ProtectionRacket r1{};
    r1.id = 100;
    r1.criminal_org_id = 7;
    r1.target_business_id = 42;
    r1.demand_per_tick = 12.5f;
    r1.status = RacketStatus::active;
    r1.escalation_stage = RacketEscalationStage::demand_issued;
    r1.last_payment_tick = 30;
    r1.demand_issued_tick = 0;
    r1.community_grievance_contribution = 0.025f;
    mod.rackets_mut().push_back(r1);

    ProtectionRacket r2{};
    r2.id = 101;
    r2.criminal_org_id = 7;
    r2.target_business_id = 43;
    r2.demand_per_tick = 8.0f;
    r2.status = RacketStatus::refused;
    r2.escalation_stage = RacketEscalationStage::violence;
    r2.last_payment_tick = 0;
    r2.demand_issued_tick = 5;
    r2.community_grievance_contribution = 0.016f;
    mod.rackets_mut().push_back(r2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    ProtectionRacketsModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.rackets().size() == 2);
    REQUIRE(restored_mod.rackets()[0].id == 100);
    REQUIRE(restored_mod.rackets()[0].demand_per_tick == 12.5f);
    REQUIRE(restored_mod.rackets()[0].status == RacketStatus::active);
    REQUIRE(restored_mod.rackets()[1].id == 101);
    REQUIRE(restored_mod.rackets()[1].escalation_stage == RacketEscalationStage::violence);
    REQUIRE(restored_mod.rackets()[1].community_grievance_contribution == 0.016f);
}

TEST_CASE("v7 module-state: real_estate properties round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RealEstateModule mod;

    PropertyListing p1{};
    p1.id = 1;
    p1.type = PropertyType::residential;
    p1.province_id = 0;
    p1.owner_id = 99;
    p1.asking_price = 250000.0f;
    p1.market_value = 230000.0f;
    p1.rental_yield_rate = 0.003f;
    p1.rental_income_per_tick = 690.0f;
    p1.rented = true;
    p1.tenant_id = 1500;
    p1.launder_eligible = false;
    p1.purchased_tick = 100;
    p1.purchase_price = 200000.0f;
    mod.add_property(p1);

    PropertyListing p2{};
    p2.id = 2;
    p2.type = PropertyType::commercial;
    p2.province_id = 1;
    p2.owner_id = 100;
    p2.asking_price = 800000.0f;
    p2.market_value = 750000.0f;
    p2.rental_yield_rate = 0.004f;
    p2.rental_income_per_tick = 3000.0f;
    p2.rented = false;
    p2.tenant_id = 0;
    p2.launder_eligible = true;
    p2.purchased_tick = 200;
    p2.purchase_price = 600000.0f;
    mod.add_property(p2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    RealEstateModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    const auto& restored_props = restored_mod.properties();
    REQUIRE(restored_props.size() == 2);
    REQUIRE(restored_props[0].id == 1);
    REQUIRE(restored_props[0].type == PropertyType::residential);
    REQUIRE(restored_props[0].rented == true);
    REQUIRE(restored_props[0].tenant_id == 1500);
    REQUIRE(restored_props[0].market_value == 230000.0f);
    REQUIRE(restored_props[1].id == 2);
    REQUIRE(restored_props[1].launder_eligible == true);
    REQUIRE(restored_props[1].rental_income_per_tick == 3000.0f);
}

TEST_CASE("v7 module-state: all three modules in one save",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RandomEventsModule re;
    ProtectionRacketsModule pr;
    RealEstateModule rs;

    ActiveRandomEvent e{};
    e.id = 1;
    e.template_id = "drought";
    e.province_id = 0;
    e.severity = 0.5f;
    e.started_tick = 5;
    e.end_tick = 50;
    re.add_active_event(e);

    ProtectionRacket r{};
    r.id = 1;
    r.criminal_org_id = 1;
    r.target_business_id = 1;
    r.demand_per_tick = 10.0f;
    r.status = RacketStatus::active;
    pr.rackets_mut().push_back(r);

    PropertyListing p{};
    p.id = 1;
    p.type = PropertyType::industrial;
    p.province_id = 0;
    p.market_value = 100000.0f;
    rs.add_property(p);

    auto bytes = PersistenceModule::serialize(world, {&re, &pr, &rs});

    RandomEventsModule re_r;
    ProtectionRacketsModule pr_r;
    RealEstateModule rs_r;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&re_r, &pr_r, &rs_r}) ==
            RestoreResult::success);

    REQUIRE(re_r.active_events().size() == 1);
    REQUIRE(pr_r.rackets().size() == 1);
    REQUIRE(rs_r.properties().size() == 1);
    REQUIRE(rs_r.properties()[0].type == PropertyType::industrial);
}

TEST_CASE("v7 module-state: modules in save but absent from load are skipped",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    RandomEventsModule re;
    ProtectionRacketsModule pr;

    ActiveRandomEvent e{};
    e.id = 1;
    e.template_id = "drought";
    e.province_id = 0;
    re.add_active_event(e);

    ProtectionRacket r{};
    r.id = 1;
    r.status = RacketStatus::active;
    pr.rackets_mut().push_back(r);

    auto bytes = PersistenceModule::serialize(world, {&re, &pr});

    // Only RandomEventsModule provided on load — the protection_rackets
    // block in the save is skipped without error (forward compatibility).
    RandomEventsModule re_r;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&re_r}) == RestoreResult::success);
    REQUIRE(re_r.active_events().size() == 1);
}

TEST_CASE("v7 module-state: empty modules list serializes count=0",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    // No modules passed — section header is written with count 0, then read.
    auto bytes_no_modules = PersistenceModule::serialize(world);
    auto bytes_empty_vec = PersistenceModule::serialize(world, {});
    REQUIRE(bytes_no_modules == bytes_empty_vec);

    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes_no_modules, restored) == RestoreResult::success);
}

TEST_CASE("v7 module-state: legal_process cases round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    LegalProcessModule mod;

    LegalCase c1{};
    c1.id = 11;
    c1.defendant_npc_id = 200;
    c1.prosecutor_npc_id = 300;
    c1.judge_npc_id = 400;
    c1.stage = LegalCaseStage::trial;
    c1.severity = CaseSeverity::serious;
    c1.evidence_weight = 0.75f;
    c1.defense_quality = 0.6f;
    c1.conviction_probability = 0.7f;
    c1.opened_tick = 100;
    c1.sentence_ticks = 1095;
    c1.release_tick = 0;
    c1.double_jeopardy_until = 0;
    c1.is_player_case = false;
    mod.cases_mut().push_back(c1);

    LegalCase c2{};
    c2.id = 12;
    c2.defendant_npc_id = 0;  // player
    c2.severity = CaseSeverity::capital;
    c2.stage = LegalCaseStage::imprisoned;
    c2.opened_tick = 50;
    c2.sentence_ticks = 2190;
    c2.release_tick = 2240;
    c2.is_player_case = true;
    mod.cases_mut().push_back(c2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    LegalProcessModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.cases().size() == 2);
    REQUIRE(restored_mod.cases()[0].id == 11);
    REQUIRE(restored_mod.cases()[0].stage == LegalCaseStage::trial);
    REQUIRE(restored_mod.cases()[0].evidence_weight == 0.75f);
    REQUIRE(restored_mod.cases()[1].is_player_case == true);
    REQUIRE(restored_mod.cases()[1].stage == LegalCaseStage::imprisoned);
    REQUIRE(restored_mod.cases()[1].release_tick == 2240);
}

TEST_CASE("v7 module-state: money_laundering operations + fiu_results round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    MoneyLaunderingModule mod;

    LaunderingOperation op1{};
    op1.id = 1;
    op1.actor_id = 99;
    op1.method = LaunderingMethod::shell_company_chain;
    op1.dirty_amount = 10000.0f;
    op1.laundered_so_far = 2500.0f;
    op1.launder_rate_per_tick = 500.0f;
    op1.conversion_loss_rate = 0.08f;
    op1.started_tick = 10;
    op1.destination_business_id = 42;
    op1.shell_chain_business_ids = {1001, 1002, 1003};
    op1.evidence_generated_total = 0.35f;
    op1.paused = false;
    op1.completed = false;
    mod.operations_mut().push_back(op1);

    LaunderingOperation op2{};
    op2.id = 2;
    op2.actor_id = 100;
    op2.method = LaunderingMethod::crypto_mixing;
    op2.dirty_amount = 5000.0f;
    op2.laundered_so_far = 5000.0f;
    op2.completed = true;
    mod.operations_mut().push_back(op2);

    FIUPatternResult f1{};
    f1.target_actor_id = 99;
    f1.suspicion_score = 0.85f;
    f1.inferred_method = LaunderingMethod::shell_company_chain;
    mod.fiu_results_mut().push_back(f1);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    MoneyLaunderingModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.operations().size() == 2);
    REQUIRE(restored_mod.operations()[0].id == 1);
    REQUIRE(restored_mod.operations()[0].method == LaunderingMethod::shell_company_chain);
    REQUIRE(restored_mod.operations()[0].laundered_so_far == 2500.0f);
    REQUIRE(restored_mod.operations()[0].shell_chain_business_ids ==
            std::vector<uint32_t>{1001, 1002, 1003});
    REQUIRE(restored_mod.operations()[1].completed == true);

    REQUIRE(restored_mod.fiu_results().size() == 1);
    REQUIRE(restored_mod.fiu_results()[0].target_actor_id == 99);
    REQUIRE(restored_mod.fiu_results()[0].suspicion_score == 0.85f);
    REQUIRE(restored_mod.fiu_results()[0].inferred_method ==
            LaunderingMethod::shell_company_chain);
}

TEST_CASE("v7 module-state: labor_market full deployed state round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    LaborMarketModule mod;

    // Job postings
    JobPosting jp{};
    jp.id = 50;
    jp.owner_id = 7;
    jp.business_id = 100;
    jp.province_id = 1;
    jp.required_domain = SkillDomain::Business;
    jp.min_skill_level = 0.4f;
    jp.offered_wage = 12.5f;
    jp.channel = HiringChannel::professional_network;
    jp.posted_tick = 30;
    jp.expires_tick = 60;
    jp.applicant_ids = {200, 201, 202};
    jp.filled = false;
    mod.job_postings().push_back(jp);

    // Employment records
    EmploymentRecord er{};
    er.npc_id = 500;
    er.employer_business_id = 100;
    er.offered_wage = 11.0f;
    er.hired_tick = 5;
    er.deferred_salary_ticks = 0;
    mod.employment_records().push_back(er);

    EmploymentRecord er2{};
    er2.npc_id = 501;
    er2.employer_business_id = 100;
    er2.offered_wage = 9.5f;
    er2.hired_tick = 10;
    er2.deferred_salary_ticks = 3;
    mod.employment_records().push_back(er2);

    // NPC skills (intentionally inserted out of sorted order to verify
    // canonicalisation on write — same input must yield same bytes regardless
    // of unordered_map iteration order).
    mod.npc_skills()[501].push_back({SkillDomain::Engineering, 0.55f});
    mod.npc_skills()[500].push_back({SkillDomain::Business, 0.7f});
    mod.npc_skills()[500].push_back({SkillDomain::Engineering, 0.3f});

    // Regional wages
    ProvinceSkillKey k1{1, SkillDomain::Business};
    ProvinceSkillKey k2{0, SkillDomain::Engineering};
    mod.regional_wages()[k1] = 12.0f;
    mod.regional_wages()[k2] = 14.0f;

    // Applications
    WorkerApplication wa1{};
    wa1.applicant_npc_id = 200;
    wa1.skill_level = 0.5f;
    wa1.salary_expectation = 12.0f;
    wa1.loyalty_prior = 0.0f;
    wa1.background_visible = false;
    WorkerApplication wa2{};
    wa2.applicant_npc_id = 201;
    wa2.skill_level = 0.6f;
    wa2.salary_expectation = 13.0f;
    wa2.loyalty_prior = 0.3f;
    wa2.background_visible = true;
    mod.applications()[50] = {wa1, wa2};

    auto bytes = PersistenceModule::serialize(world, {&mod});

    LaborMarketModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.job_postings().size() == 1);
    REQUIRE(restored_mod.job_postings()[0].id == 50);
    REQUIRE(restored_mod.job_postings()[0].applicant_ids ==
            std::vector<uint32_t>{200, 201, 202});
    REQUIRE(restored_mod.job_postings()[0].channel == HiringChannel::professional_network);

    REQUIRE(restored_mod.employment_records().size() == 2);
    REQUIRE(restored_mod.employment_records()[0].npc_id == 500);
    REQUIRE(restored_mod.employment_records()[0].offered_wage == 11.0f);
    REQUIRE(restored_mod.employment_records()[1].deferred_salary_ticks == 3);

    REQUIRE(restored_mod.npc_skills().size() == 2);
    REQUIRE(restored_mod.npc_skills().at(500).size() == 2);
    REQUIRE(restored_mod.npc_skills().at(501).size() == 1);
    REQUIRE(restored_mod.get_npc_skill(500, SkillDomain::Business) == 0.7f);
    REQUIRE(restored_mod.get_npc_skill(501, SkillDomain::Engineering) == 0.55f);

    REQUIRE(restored_mod.regional_wages().size() == 2);
    REQUIRE(restored_mod.regional_wages().at(k1) == 12.0f);
    REQUIRE(restored_mod.regional_wages().at(k2) == 14.0f);

    REQUIRE(restored_mod.applications().size() == 1);
    REQUIRE(restored_mod.applications().at(50).size() == 2);
    REQUIRE(restored_mod.applications().at(50)[0].applicant_npc_id == 200);
    REQUIRE(restored_mod.applications().at(50)[1].background_visible == true);
}

TEST_CASE("v7 module-state: labor_market serialization is deterministic across runs",
          "[persistence][v7][module_state][determinism]") {
    // Two modules populated identically but in different insertion order:
    // unordered_map iteration order can differ, but the canonical sort in
    // serialize_state must yield byte-identical output. Catches regressions
    // where a new field is added without canonicalisation.
    auto world = minimal_world();

    LaborMarketModule a;
    a.npc_skills()[300].push_back({SkillDomain::Business, 0.5f});
    a.npc_skills()[100].push_back({SkillDomain::Engineering, 0.6f});
    a.npc_skills()[200].push_back({SkillDomain::Business, 0.7f});
    a.regional_wages()[{2, SkillDomain::Business}] = 10.0f;
    a.regional_wages()[{0, SkillDomain::Engineering}] = 11.0f;
    a.regional_wages()[{1, SkillDomain::Business}] = 12.0f;
    a.applications()[7] = {};
    a.applications()[1] = {};
    a.applications()[4] = {};

    LaborMarketModule b;
    b.npc_skills()[100].push_back({SkillDomain::Engineering, 0.6f});
    b.npc_skills()[200].push_back({SkillDomain::Business, 0.7f});
    b.npc_skills()[300].push_back({SkillDomain::Business, 0.5f});
    b.regional_wages()[{1, SkillDomain::Business}] = 12.0f;
    b.regional_wages()[{0, SkillDomain::Engineering}] = 11.0f;
    b.regional_wages()[{2, SkillDomain::Business}] = 10.0f;
    b.applications()[1] = {};
    b.applications()[4] = {};
    b.applications()[7] = {};

    auto bytes_a = PersistenceModule::serialize(world, {&a});
    auto bytes_b = PersistenceModule::serialize(world, {&b});
    REQUIRE(bytes_a == bytes_b);
}

// ─── Round 2 modules ────────────────────────────────────────────────────────

TEST_CASE("v7 module-state: investigator_engine cases round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    InvestigatorEngineModule mod;

    InvestigationCase c1{};
    c1.investigator_npc_id = 700;
    c1.investigator_type = InvestigatorType::law_enforcement;
    c1.target_id = 1000;
    c1.current_level = 0.65f;
    c1.fill_rate = 0.012f;
    c1.status = static_cast<uint8_t>(InvestigatorMeterStatus::formal_inquiry);
    c1.opened_tick = 50;
    c1.formally_opened = true;
    c1.province_id = 2;
    mod.cases_mut().push_back(c1);

    InvestigationCase c2{};
    c2.investigator_npc_id = 701;
    c2.investigator_type = InvestigatorType::journalist;
    c2.target_id = 2000;
    c2.current_level = 0.15f;
    c2.fill_rate = 0.003f;
    c2.status = static_cast<uint8_t>(InvestigatorMeterStatus::surveillance);
    c2.opened_tick = 100;
    c2.formally_opened = false;
    c2.province_id = 0;
    mod.cases_mut().push_back(c2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    InvestigatorEngineModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.cases().size() == 2);
    REQUIRE(restored_mod.cases()[0].investigator_npc_id == 700);
    REQUIRE(restored_mod.cases()[0].current_level == 0.65f);
    REQUIRE(restored_mod.cases()[0].formally_opened == true);
    REQUIRE(restored_mod.cases()[1].investigator_type == InvestigatorType::journalist);
    REQUIRE(restored_mod.cases()[1].fill_rate == 0.003f);
}

TEST_CASE("v7 module-state: banking loans + credit profiles round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    BankingModule mod;

    LoanRecord l1{};
    l1.id = 1;
    l1.borrower_id = 500;
    l1.lender_id = 99;
    l1.purpose = LoanPurpose::property_purchase;
    l1.principal = 200000.0f;
    l1.outstanding_balance = 150000.0f;
    l1.interest_rate = 0.00005f;
    l1.repayment_per_tick = 50.0f;
    l1.originated_tick = 100;
    l1.maturity_tick = 11100;
    l1.in_default = false;
    l1.collateral_id = 42;
    mod.active_loans().push_back(l1);

    LoanRecord l2{};
    l2.id = 2;
    l2.borrower_id = 501;
    l2.lender_id = 99;
    l2.purpose = LoanPurpose::personal;
    l2.principal = 5000.0f;
    l2.outstanding_balance = 4800.0f;
    l2.interest_rate = 0.0002f;
    l2.repayment_per_tick = 5.0f;
    l2.originated_tick = 150;
    l2.maturity_tick = 1150;
    l2.in_default = true;
    l2.collateral_id = 0;
    mod.active_loans().push_back(l2);

    BankingModule::BorrowerCredit bc{};
    bc.borrower_id = 500;
    bc.consecutive_misses = 0;
    bc.profile.credit_score = 0.75f;
    bc.profile.total_debt_outstanding = 150000.0f;
    bc.profile.debt_service_per_tick = 50.0f;
    bc.profile.debt_to_income_ratio = 0.3f;
    mod.borrower_credits().push_back(bc);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    BankingModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.active_loans().size() == 2);
    REQUIRE(restored_mod.active_loans()[0].id == 1);
    REQUIRE(restored_mod.active_loans()[0].outstanding_balance == 150000.0f);
    REQUIRE(restored_mod.active_loans()[0].purpose == LoanPurpose::property_purchase);
    REQUIRE(restored_mod.active_loans()[1].in_default == true);
    REQUIRE(restored_mod.active_loans()[1].collateral_id == 0);

    REQUIRE(restored_mod.borrower_credits().size() == 1);
    REQUIRE(restored_mod.borrower_credits()[0].borrower_id == 500);
    REQUIRE(restored_mod.borrower_credits()[0].profile.credit_score == 0.75f);
}

TEST_CASE("v7 module-state: criminal_operations orgs + expansions round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    CriminalOperationsModule mod;

    CriminalOrganization o1{};
    o1.id = 10;
    o1.leadership_npc_id = 600;
    o1.member_npc_ids = {601, 602, 603};
    o1.income_source_ids = {1000, 1001};
    o1.cash = 250000.0f;
    o1.strategic_decision_tick = 90;
    o1.decision_day_offset = 7;
    o1.dominance_by_province[1] = 0.6f;
    o1.dominance_by_province[2] = 0.4f;
    o1.conflict_state = TerritorialConflictStage::personnel_violence;
    o1.conflict_rival_org_id = 11;
    mod.organizations().push_back(o1);

    ExpansionTeam e1{};
    e1.org_id = 10;
    e1.target_province_id = 3;
    e1.member_npc_ids = {604, 605};
    e1.investment = 50000.0f;
    e1.arrival_tick = 200;
    mod.active_expansions().push_back(e1);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    CriminalOperationsModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.organizations().size() == 1);
    REQUIRE(restored_mod.organizations()[0].id == 10);
    REQUIRE(restored_mod.organizations()[0].member_npc_ids ==
            std::vector<uint32_t>{601, 602, 603});
    REQUIRE(restored_mod.organizations()[0].income_source_ids ==
            std::vector<uint32_t>{1000, 1001});
    REQUIRE(restored_mod.organizations()[0].dominance_by_province.size() == 2);
    REQUIRE(restored_mod.organizations()[0].dominance_by_province.at(1) == 0.6f);
    REQUIRE(restored_mod.organizations()[0].dominance_by_province.at(2) == 0.4f);
    REQUIRE(restored_mod.organizations()[0].conflict_state ==
            TerritorialConflictStage::personnel_violence);
    REQUIRE(restored_mod.organizations()[0].conflict_rival_org_id == 11);

    REQUIRE(restored_mod.active_expansions().size() == 1);
    REQUIRE(restored_mod.active_expansions()[0].member_npc_ids ==
            std::vector<uint32_t>{604, 605});
    REQUIRE(restored_mod.active_expansions()[0].arrival_tick == 200);
}

TEST_CASE("v7 module-state: informant_system records round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    InformantSystemModule mod;

    InformantRecord r1{};
    r1.npc_id = 800;
    r1.status = InformantStatus::cooperating;
    r1.flip_probability = 0.18f;
    r1.base_flip_rate = 0.10f;
    r1.arrest_tick = 50;
    r1.cooperation_start_tick = 60;
    r1.compartmentalization_level = 2;
    mod.records_mut().push_back(r1);

    InformantRecord r2{};
    r2.npc_id = 801;
    r2.status = InformantStatus::silenced;
    r2.flip_probability = 0.0f;
    r2.base_flip_rate = 0.10f;
    r2.arrest_tick = 0;
    r2.cooperation_start_tick = 0;
    r2.compartmentalization_level = 0;
    mod.records_mut().push_back(r2);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    InformantSystemModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.records().size() == 2);
    REQUIRE(restored_mod.records()[0].npc_id == 800);
    REQUIRE(restored_mod.records()[0].status == InformantStatus::cooperating);
    REQUIRE(restored_mod.records()[0].flip_probability == 0.18f);
    REQUIRE(restored_mod.records()[1].status == InformantStatus::silenced);
}

TEST_CASE("v7 module-state: drug_economy production + legalization round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    DrugEconomyModule mod;

    DrugProductionRecord p1{};
    p1.business_id = 2000;
    p1.drug_type = DrugType::cannabis;
    p1.market_tier = DrugMarketTier::retail;
    p1.output_quantity = 150.0f;
    p1.output_quality = 0.8f;
    p1.precursor_consumed = 50.0f;
    p1.province_id = 1;
    mod.production_records_mut().push_back(p1);

    DrugProductionRecord p2{};
    p2.business_id = 2001;
    p2.drug_type = DrugType::synthetic_opioid;
    p2.market_tier = DrugMarketTier::wholesale;
    p2.output_quantity = 50.0f;
    p2.output_quality = 0.6f;
    p2.precursor_consumed = 200.0f;
    p2.province_id = 0;
    mod.production_records_mut().push_back(p2);

    DrugLegalizationStatus s0{};
    s0.cannabis_legal = true;
    s0.methamphetamine_legal = false;
    s0.synthetic_opioid_legal = false;
    s0.designer_drug_legal = false;
    mod.legalization_status_mut().push_back(s0);

    DrugLegalizationStatus s1{};
    s1.cannabis_legal = false;
    s1.methamphetamine_legal = false;
    s1.synthetic_opioid_legal = false;
    s1.designer_drug_legal = true;
    mod.legalization_status_mut().push_back(s1);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    DrugEconomyModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.production_records().size() == 2);
    REQUIRE(restored_mod.production_records()[0].drug_type == DrugType::cannabis);
    REQUIRE(restored_mod.production_records()[0].output_quantity == 150.0f);
    REQUIRE(restored_mod.production_records()[1].market_tier == DrugMarketTier::wholesale);
    REQUIRE(restored_mod.production_records()[1].precursor_consumed == 200.0f);

    REQUIRE(restored_mod.legalization_status().size() == 2);
    REQUIRE(restored_mod.legalization_status()[0].cannabis_legal == true);
    REQUIRE(restored_mod.legalization_status()[0].designer_drug_legal == false);
    REQUIRE(restored_mod.legalization_status()[1].cannabis_legal == false);
    REQUIRE(restored_mod.legalization_status()[1].designer_drug_legal == true);
}

// ─── Round 3 modules ────────────────────────────────────────────────────────

TEST_CASE("v7 module-state: healthcare province + npc records round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    HealthcareModule mod;

    HealthcareModule::ProvinceHealthState p0{};
    p0.province_id = 0;
    p0.profile.access_level = 0.7f;
    p0.profile.quality_level = 0.6f;
    p0.profile.cost_per_treatment = 250.0f;
    p0.profile.capacity_utilisation = 0.85f;
    p0.sick_leave_fraction = 0.05f;
    p0.effective_labour_supply = 0.95f;
    mod.province_health_states().push_back(p0);

    HealthcareModule::NpcHealthRecord n0{};
    n0.npc_id = 500;
    n0.health = 0.65f;
    n0.last_treatment_tick = 200;
    mod.npc_health_records().push_back(n0);
    HealthcareModule::NpcHealthRecord n1{};
    n1.npc_id = 501;
    n1.health = 0.92f;
    n1.last_treatment_tick = 0;
    mod.npc_health_records().push_back(n1);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    HealthcareModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.province_health_states().size() == 1);
    REQUIRE(restored_mod.province_health_states()[0].profile.quality_level == 0.6f);
    REQUIRE(restored_mod.province_health_states()[0].sick_leave_fraction == 0.05f);
    REQUIRE(restored_mod.npc_health_records().size() == 2);
    REQUIRE(restored_mod.npc_health_records()[0].health == 0.65f);
    REQUIRE(restored_mod.npc_health_records()[0].last_treatment_tick == 200);
    REQUIRE(restored_mod.npc_health_records()[1].npc_id == 501);
}

TEST_CASE("v7 module-state: community_response province states round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    CommunityResponseModule mod;

    CommunityResponseModule::ProvinceOppositionState s0{};
    s0.opposition_org_exists = true;
    s0.last_stage_change_tick = 200;
    mod.province_states().push_back(s0);

    CommunityResponseModule::ProvinceOppositionState s1{};
    s1.opposition_org_exists = false;
    s1.last_stage_change_tick = 0;
    mod.province_states().push_back(s1);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    CommunityResponseModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.province_states().size() == 2);
    REQUIRE(restored_mod.province_states()[0].opposition_org_exists == true);
    REQUIRE(restored_mod.province_states()[0].last_stage_change_tick == 200);
    REQUIRE(restored_mod.province_states()[1].opposition_org_exists == false);
}

TEST_CASE("v7 module-state: media_system outlets + stories round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    MediaSystemModule mod;

    MediaOutlet o1{};
    o1.id = 1;
    o1.province_id = 0;
    o1.type = MediaOutletType::newspaper;
    o1.credibility = 0.7f;
    o1.reach = 0.45f;
    o1.editorial_independence = 0.6f;
    o1.owner_npc_id = 800;
    o1.journalist_ids = {900, 901, 902};
    mod.outlets().push_back(o1);

    Story s1{};
    s1.id = 10;
    s1.subject_id = 1500;
    s1.journalist_id = 900;
    s1.outlet_id = 1;
    s1.tone = StoryTone::damaging;
    s1.evidence_weight = 0.7f;
    s1.amplification = 2.5f;
    s1.published_tick = 300;
    s1.evidence_token_ids = {50, 51};
    s1.is_active = true;
    mod.active_stories().push_back(s1);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    MediaSystemModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.outlets().size() == 1);
    REQUIRE(restored_mod.outlets()[0].journalist_ids ==
            std::vector<uint32_t>{900, 901, 902});
    REQUIRE(restored_mod.outlets()[0].type == MediaOutletType::newspaper);
    REQUIRE(restored_mod.outlets()[0].credibility == 0.7f);

    REQUIRE(restored_mod.active_stories().size() == 1);
    REQUIRE(restored_mod.active_stories()[0].tone == StoryTone::damaging);
    REQUIRE(restored_mod.active_stories()[0].evidence_token_ids ==
            std::vector<uint32_t>{50, 51});
    REQUIRE(restored_mod.active_stories()[0].amplification == 2.5f);
}

TEST_CASE("v7 module-state: government_budget round-trip",
          "[persistence][v7][module_state][serialization]") {
    auto world = minimal_world();
    GovernmentBudgetModule mod;

    GovernmentBudget b{};
    b.level = GovernmentLevel::national;
    b.jurisdiction_id = 0;
    b.revenue_own_taxes = 1000000.0f;
    b.revenue_transfers_in = 0.0f;
    b.revenue_other = 50000.0f;
    b.total_revenue = 1050000.0f;
    b.spending_allocations[SpendingCategory::public_services] = 400000.0f;
    b.spending_allocations[SpendingCategory::law_enforcement] = 200000.0f;
    b.spending_actual[SpendingCategory::public_services] = 380000.0f;
    b.spending_actual[SpendingCategory::law_enforcement] = 200000.0f;
    b.total_expenditure = 580000.0f;
    b.surplus_deficit = 470000.0f;
    b.accumulated_debt = 2000000.0f;
    b.cash = 150000.0f;
    b.debt_to_revenue_ratio = 1.9f;
    b.deficit_to_revenue_ratio = -0.45f;
    mod.budgets().push_back(b);

    auto bytes = PersistenceModule::serialize(world, {&mod});

    GovernmentBudgetModule restored_mod;
    WorldState restored;
    REQUIRE(PersistenceModule::deserialize(bytes, restored, {&restored_mod}) ==
            RestoreResult::success);

    REQUIRE(restored_mod.budgets().size() == 1);
    REQUIRE(restored_mod.budgets()[0].level == GovernmentLevel::national);
    REQUIRE(restored_mod.budgets()[0].revenue_own_taxes == 1000000.0f);
    REQUIRE(restored_mod.budgets()[0].accumulated_debt == 2000000.0f);
    REQUIRE(restored_mod.budgets()[0].spending_allocations.size() == 2);
    REQUIRE(restored_mod.budgets()[0].spending_allocations.at(
                SpendingCategory::public_services) == 400000.0f);
    REQUIRE(restored_mod.budgets()[0].spending_actual.at(
                SpendingCategory::law_enforcement) == 200000.0f);
    REQUIRE(restored_mod.budgets()[0].cash == 150000.0f);
}
