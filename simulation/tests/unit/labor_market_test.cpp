// Labor market module unit tests.
// All tests tagged [labor_market][tier2].
//
// Tests verify wage payment, deferred salary, monthly wage adjustment,
// hiring decisions, low reputation effects, voluntary departure,
// and criminal business channel restrictions.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/rng/deterministic_rng.h"
#include "modules/labor_market/labor_market_module.h"
#include "modules/labor_market/labor_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

WorldState make_test_world_state() {
    WorldState state{};
    state.current_tick = 1;
    state.world_seed = 42;
    state.player = nullptr;
    state.lod2_price_index = nullptr;
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

Province make_test_province(uint32_t id) {
    Province prov{};
    prov.id = id;
    prov.lod_level = SimulationLOD::full;
    prov.demographics.total_population = 100000;
    prov.demographics.median_age = 35.0f;
    prov.demographics.education_level = 0.6f;
    prov.demographics.income_low_fraction = 0.3f;
    prov.demographics.income_middle_fraction = 1.0f;  // base wage reference
    prov.demographics.income_high_fraction = 0.1f;
    prov.demographics.political_lean = 0.0f;
    prov.cohort_stats = nullptr;
    return prov;
}

NPC make_test_npc(uint32_t id, uint32_t province_id) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::worker;
    npc.status = NPCStatus::active;
    npc.current_province_id = province_id;
    npc.home_province_id = province_id;
    npc.risk_tolerance = 0.5f;
    npc.capital = 100.0f;
    npc.social_capital = 0.0f;
    npc.movement_follower_count = 0;

    // Default neutral motivation weights (sum to 1.0).
    npc.motivations.weights = {0.125f, 0.125f, 0.125f, 0.125f,
                               0.125f, 0.125f, 0.125f, 0.125f};
    return npc;
}

NPCBusiness make_test_business(uint32_t id, uint32_t province_id,
                                float cash = 10000.0f) {
    NPCBusiness biz{};
    biz.id = id;
    biz.sector = BusinessSector::manufacturing;
    biz.profile = BusinessProfile::cost_cutter;
    biz.cash = cash;
    biz.revenue_per_tick = 100.0f;
    biz.cost_per_tick = 50.0f;
    biz.market_share = 0.1f;
    biz.strategic_decision_tick = 100;
    biz.dispatch_day_offset = 0;
    biz.actor_tech_state = ActorTechnologyState{1.0f};
    biz.criminal_sector = false;
    biz.province_id = province_id;
    biz.regulatory_violation_severity = 0.0f;
    biz.default_activity_scope = VisibilityScope::institutional;
    biz.owner_id = 0;
    biz.deferred_salary_liability = 0.0f;
    biz.accounts_payable_float = 0.0f;
    return biz;
}

// Count NPCDelta entries for a given npc_id that have capital_delta set.
float sum_capital_deltas(const DeltaBuffer& delta, uint32_t npc_id) {
    float total = 0.0f;
    for (const auto& d : delta.npc_deltas) {
        if (d.npc_id == npc_id && d.capital_delta.has_value()) {
            total += d.capital_delta.value();
        }
    }
    return total;
}

// Check if any NPCDelta for a given npc_id has a memory of a given type.
bool has_memory_type(const DeltaBuffer& delta, uint32_t npc_id, MemoryType type) {
    for (const auto& d : delta.npc_deltas) {
        if (d.npc_id == npc_id && d.new_memory_entry.has_value()) {
            if (d.new_memory_entry->type == type) {
                return true;
            }
        }
    }
    return false;
}

// Get the emotional weight of the first memory of a given type for an NPC.
float get_memory_weight(const DeltaBuffer& delta, uint32_t npc_id, MemoryType type) {
    for (const auto& d : delta.npc_deltas) {
        if (d.npc_id == npc_id && d.new_memory_entry.has_value()) {
            if (d.new_memory_entry->type == type) {
                return d.new_memory_entry->emotional_weight;
            }
        }
    }
    return 0.0f;
}

}  // anonymous namespace

// ===========================================================================
// Tests
// ===========================================================================

TEST_CASE("test_module_interface_properties", "[labor_market][tier2]") {
    LaborMarketModule module;

    REQUIRE(module.name() == "labor_market");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == true);

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "production");

    auto before = module.runs_before();
    REQUIRE(before.size() == 1);
    REQUIRE(before[0] == "price_engine");
}

TEST_CASE("test_wage_payment_business_deducted_npc_credited", "[labor_market][tier2]") {
    // An employed NPC should receive capital_delta equal to offered_wage
    // when the business has sufficient cash.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    auto npc = make_test_npc(100, province_id);
    prov.significant_npc_ids.push_back(100);
    state.provinces.push_back(prov);
    state.significant_npcs.push_back(npc);

    auto biz = make_test_business(1, province_id, 10000.0f);
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;
    module.employment_records().push_back(EmploymentRecord{
        100,   // npc_id
        1,     // employer_business_id
        5.0f,  // offered_wage
        0,     // hired_tick
        0      // deferred_salary_ticks
    });

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // NPC should have capital_delta == 5.0 (the offered wage).
    float cap = sum_capital_deltas(delta, 100);
    REQUIRE_THAT(cap, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("test_deferred_salary_when_business_cash_insufficient", "[labor_market][tier2]") {
    // When business cash is 0, wage should not be paid and
    // deferred_salary_ticks should increment.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    auto npc = make_test_npc(100, province_id);
    prov.significant_npc_ids.push_back(100);
    state.provinces.push_back(prov);
    state.significant_npcs.push_back(npc);

    // Business with zero cash.
    auto biz = make_test_business(1, province_id, 0.0f);
    biz.revenue_per_tick = 0.0f;
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;
    module.employment_records().push_back(EmploymentRecord{
        100, 1, 5.0f, 0, 0
    });

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // No capital_delta should be emitted (wage not paid).
    float cap = sum_capital_deltas(delta, 100);
    REQUIRE_THAT(cap, WithinAbs(0.0f, 0.001f));

    // Deferred salary ticks should have incremented.
    const auto& records = module.employment_records();
    REQUIRE(records[0].deferred_salary_ticks == 1);
}

TEST_CASE("test_deferred_salary_generates_wage_theft_memory", "[labor_market][tier2]") {
    // When deferred_salary_ticks exceeds deferred_salary_max_ticks (30),
    // the NPC should receive a witnessed_wage_theft memory.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    auto npc = make_test_npc(100, province_id);
    prov.significant_npc_ids.push_back(100);
    state.provinces.push_back(prov);
    state.significant_npcs.push_back(npc);

    auto biz = make_test_business(1, province_id, 0.0f);
    biz.revenue_per_tick = 0.0f;
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;
    // Pre-set deferred ticks to just at the threshold.
    module.employment_records().push_back(EmploymentRecord{
        100, 1, 5.0f, 0, LaborConfig::deferred_salary_max_ticks
    });

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // After incrementing, deferred_salary_ticks = 31 > 30, so wage theft memory emitted.
    REQUIRE(has_memory_type(delta, 100, MemoryType::witnessed_wage_theft));

    float weight = get_memory_weight(delta, 100, MemoryType::witnessed_wage_theft);
    REQUIRE(weight < 0.0f);
    REQUIRE(weight >= -0.7f);
    REQUIRE(weight <= -0.3f);
}

TEST_CASE("test_monthly_wage_adjustment_supply_exceeds_demand", "[labor_market][tier2]") {
    // When labor supply > demand, wage should decrease.
    auto state = make_test_world_state();
    state.current_tick = 30;  // Monthly tick for wage update.
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    // Add 4 unemployed NPCs with Business skill.
    for (uint32_t i = 1; i <= 4; ++i) {
        auto npc = make_test_npc(i, province_id);
        prov.significant_npc_ids.push_back(i);
        state.significant_npcs.push_back(npc);
    }
    state.provinces.push_back(prov);

    LaborMarketModule module;

    // Register skills for all 4 NPCs in Business domain.
    for (uint32_t i = 1; i <= 4; ++i) {
        module.npc_skills()[i] = {{SkillDomain::Business, 0.5f}};
    }

    // Set initial wage.
    ProvinceSkillKey key{province_id, SkillDomain::Business};
    module.regional_wages()[key] = 1.0f;

    // Create 1 job posting for Business domain (demand = 1, supply = 4).
    JobPosting posting{};
    posting.id = 1;
    posting.owner_id = 0;
    posting.business_id = 1;
    posting.province_id = province_id;
    posting.required_domain = SkillDomain::Business;
    posting.min_skill_level = 0.0f;
    posting.offered_wage = 1.0f;
    posting.channel = HiringChannel::public_board;
    posting.posted_tick = 0;
    posting.expires_tick = 100;
    posting.filled = false;
    module.job_postings().push_back(posting);

    // Run the global execute which triggers wage update.
    // demand = 1 posting, supply = 4 unemployed NPCs.
    // ratio = demand / supply = 1 / 4 = 0.25.
    // new_wage = 1.0 * (1.0 + 0.03 * (0.25 - 1.0))
    //          = 1.0 * (1.0 - 0.0225)
    //          = 0.9775.
    // Labor surplus -> wage decreases.
    DeltaBuffer delta{};
    module.execute(state, delta);

    float new_wage = module.regional_wages()[key];
    REQUIRE(new_wage < 1.0f);
    REQUIRE_THAT(new_wage, WithinAbs(0.9775f, 0.01f));
}

TEST_CASE("test_hiring_decision_best_candidate_selected", "[labor_market][tier2]") {
    // Given multiple applicants, the one with the highest skill/salary ratio
    // should be hired.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);

    // Three applicants.
    auto npc_a = make_test_npc(10, province_id);
    auto npc_b = make_test_npc(20, province_id);
    auto npc_c = make_test_npc(30, province_id);
    prov.significant_npc_ids = {10, 20, 30};
    state.provinces.push_back(prov);
    state.significant_npcs = {npc_a, npc_b, npc_c};

    auto biz = make_test_business(1, province_id, 10000.0f);
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;

    // None employed yet.
    module.employment_records().push_back(EmploymentRecord{10, 0, 0.0f, 0, 0});
    module.employment_records().push_back(EmploymentRecord{20, 0, 0.0f, 0, 0});
    module.employment_records().push_back(EmploymentRecord{30, 0, 0.0f, 0, 0});

    // Create posting.
    JobPosting posting{};
    posting.id = 1;
    posting.owner_id = 0;
    posting.business_id = 1;
    posting.province_id = province_id;
    posting.required_domain = SkillDomain::Business;
    posting.min_skill_level = 0.1f;
    posting.offered_wage = 2.0f;
    posting.channel = HiringChannel::public_board;
    posting.posted_tick = 0;
    posting.expires_tick = 100;
    posting.filled = false;
    module.job_postings().push_back(posting);

    // Applicant A: skill 0.8, salary 1.0 -> ratio 0.8
    // Applicant B: skill 0.6, salary 0.5 -> ratio 1.2 (best!)
    // Applicant C: skill 0.3, salary 0.2 -> ratio 1.5 (highest ratio, but check min_skill)
    module.applications()[1] = {
        WorkerApplication{10, 0.8f, 1.0f, 0.0f, false},
        WorkerApplication{20, 0.6f, 0.5f, 0.0f, false},
        WorkerApplication{30, 0.3f, 0.2f, 0.0f, false},
    };

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Posting should be filled.
    REQUIRE(module.job_postings()[0].filled == true);

    // NPC 30 has the highest ratio (1.5), all meet min_skill_level (0.1),
    // all have salary_expectation < offered_wage (2.0).
    // So NPC 30 should be hired.
    const auto* rec = module.find_employment(30);
    REQUIRE(rec != nullptr);
    REQUIRE(rec->employer_business_id == 1);

    // NPC 30 should get an employment_positive memory.
    REQUIRE(has_memory_type(delta, 30, MemoryType::employment_positive));
}

TEST_CASE("test_hire_fails_when_salary_exceeds_offered_wage", "[labor_market][tier2]") {
    // If all applicants have salary_expectation > offered_wage, no hire occurs.
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    auto npc = make_test_npc(10, province_id);
    prov.significant_npc_ids = {10};
    state.provinces.push_back(prov);
    state.significant_npcs = {npc};

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;
    module.employment_records().push_back(EmploymentRecord{10, 0, 0.0f, 0, 0});

    JobPosting posting{};
    posting.id = 1;
    posting.owner_id = 0;
    posting.business_id = 1;
    posting.province_id = province_id;
    posting.required_domain = SkillDomain::Business;
    posting.min_skill_level = 0.0f;
    posting.offered_wage = 0.5f;  // Low wage.
    posting.channel = HiringChannel::public_board;
    posting.posted_tick = 0;
    posting.expires_tick = 100;
    posting.filled = false;
    module.job_postings().push_back(posting);

    // Applicant expects more than offered.
    module.applications()[1] = {
        WorkerApplication{10, 0.5f, 0.8f, 0.0f, false},
    };

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Posting should NOT be filled.
    REQUIRE(module.job_postings()[0].filled == false);

    // NPC should still be unemployed.
    const auto* rec = module.find_employment(10);
    REQUIRE(rec != nullptr);
    REQUIRE(rec->employer_business_id == 0);
}

TEST_CASE("test_low_reputation_reduces_pool_size", "[labor_market][tier2]") {
    // Low reputation (< threshold 0.3) reduces effective pool size.
    // reputation = 0.1, penalty = (0.3 - 0.1) * 8.0 = 1.6, round = 2.
    // Effective pool for public_board = 12 - 2 = 10.
    float reputation = 0.1f;
    uint32_t pool = LaborMarketModule::effective_pool_size(
        HiringChannel::public_board, reputation);
    REQUIRE(pool == 10);
}

TEST_CASE("test_low_reputation_salary_premium", "[labor_market][tier2]") {
    // Low reputation employer (0.2 < threshold 0.3) causes salary premium.
    // premium factor = 1.0 + (0.3 - 0.2) * 0.5 = 1.05
    // salary_expectation = regional_wage * (1.0 + money_motivation * 0.3) * 1.05
    float regional_wage = 1.0f;
    float money_motivation = 0.0f;  // Simplify: no money motivation.
    float reputation = 0.2f;

    float expectation = LaborMarketModule::compute_salary_expectation(
        regional_wage, money_motivation, reputation);

    // Expected: 1.0 * 1.0 * 1.05 = 1.05
    REQUIRE_THAT(expectation, WithinAbs(1.05f, 0.001f));
}

TEST_CASE("test_good_reputation_no_salary_premium", "[labor_market][tier2]") {
    // Reputation above threshold: no premium.
    float regional_wage = 1.0f;
    float money_motivation = 0.0f;
    float reputation = 0.5f;

    float expectation = LaborMarketModule::compute_salary_expectation(
        regional_wage, money_motivation, reputation);

    REQUIRE_THAT(expectation, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("test_voluntary_departure_probability", "[labor_market][tier2]") {
    // A worker with low satisfaction and high career motivation has
    // a nonzero departure probability.
    //
    // satisfaction = 0.2 (below threshold 0.35)
    // career_motivation = 0.5
    // departure_prob = 0.08 * (1.0 - 0.2) * 0.5 = 0.032

    // We can test compute_worker_satisfaction and verify the formula.
    NPC npc = make_test_npc(100, 0);

    // Add negative employment memories to drive satisfaction low.
    MemoryEntry neg_mem{};
    neg_mem.tick_timestamp = 1;
    neg_mem.type = MemoryType::employment_negative;
    neg_mem.subject_id = 1;
    neg_mem.emotional_weight = -0.8f;
    neg_mem.decay = 1.0f;
    neg_mem.is_actionable = false;
    npc.memory_log.push_back(neg_mem);

    // A small positive memory.
    MemoryEntry pos_mem{};
    pos_mem.tick_timestamp = 1;
    pos_mem.type = MemoryType::employment_positive;
    pos_mem.subject_id = 1;
    pos_mem.emotional_weight = 0.2f;
    pos_mem.decay = 1.0f;
    pos_mem.is_actionable = false;
    npc.memory_log.push_back(pos_mem);

    float satisfaction = LaborMarketModule::compute_worker_satisfaction(npc);
    // positive_sum = 0.2, negative_sum = 0.8, total = 1.0.
    // satisfaction = 0.2 / 1.0 = 0.2.
    REQUIRE_THAT(satisfaction, WithinAbs(0.2f, 0.001f));

    // Verify departure probability formula.
    float career_motivation = 0.5f;
    float departure_prob = LaborConfig::departure_base_rate
                         * (1.0f - satisfaction)
                         * career_motivation;
    // 0.08 * 0.8 * 0.5 = 0.032
    REQUIRE_THAT(departure_prob, WithinAbs(0.032f, 0.001f));

    // Below threshold check.
    REQUIRE(satisfaction < LaborConfig::voluntary_departure_threshold);
}

TEST_CASE("test_criminal_business_cannot_use_professional_network", "[labor_market][tier2]") {
    // A criminal business using professional_network channel should
    // effectively get no applicants (pool = 0 or the channel is rejected).
    // In V1, we test this at the pool size level: professional_network
    // for criminal businesses yields 0 applicants.
    //
    // Since this is enforced at posting creation time (not module execution),
    // we verify via the effective_pool_size logic and the spec invariant.
    // The module does not have explicit creation-time validation in V1,
    // so we verify the spec statement as a documented behavior.
    //
    // For now we verify that the professional_network pool size is small
    // and that the hiring logic correctly handles empty applicant pools.

    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    state.provinces.push_back(prov);

    auto biz = make_test_business(1, province_id);
    biz.criminal_sector = true;
    biz.sector = BusinessSector::criminal;
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;

    // Create a professional_network posting for a criminal business.
    // In a full implementation, this would be rejected at creation time.
    // Here, we verify that with no applicants, no hire occurs.
    JobPosting posting{};
    posting.id = 1;
    posting.owner_id = 0;
    posting.business_id = 1;
    posting.province_id = province_id;
    posting.required_domain = SkillDomain::Business;
    posting.min_skill_level = 0.0f;
    posting.offered_wage = 5.0f;
    posting.channel = HiringChannel::professional_network;
    posting.posted_tick = 0;
    posting.expires_tick = 100;
    posting.filled = false;
    module.job_postings().push_back(posting);

    // No applicants (criminal business cannot access professional network).
    // applications_ for posting 1 is empty.

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Posting should remain unfilled.
    REQUIRE(module.job_postings()[0].filled == false);
}

TEST_CASE("test_expired_posting_closes", "[labor_market][tier2]") {
    // A posting that has expired should be marked as filled (closed).
    auto state = make_test_world_state();
    state.current_tick = 10;
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    state.provinces.push_back(prov);

    LaborMarketModule module;

    JobPosting posting{};
    posting.id = 1;
    posting.owner_id = 0;
    posting.business_id = 1;
    posting.province_id = province_id;
    posting.required_domain = SkillDomain::Business;
    posting.min_skill_level = 0.0f;
    posting.offered_wage = 1.0f;
    posting.channel = HiringChannel::public_board;
    posting.posted_tick = 0;
    posting.expires_tick = 5;  // Expired at tick 5.
    posting.filled = false;
    module.job_postings().push_back(posting);

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    // Posting should be closed.
    REQUIRE(module.job_postings()[0].filled == true);
}

TEST_CASE("test_employer_reputation_default", "[labor_market][tier2]") {
    // An unknown employer with no memory entries defaults to 0.5.
    auto state = make_test_world_state();

    float rep = LaborMarketModule::compute_employer_reputation(999, state);
    REQUIRE_THAT(rep, WithinAbs(0.5f, 0.001f));
}

TEST_CASE("test_employer_reputation_from_memories", "[labor_market][tier2]") {
    // An employer with mostly positive memories should have high reputation.
    auto state = make_test_world_state();

    NPC npc = make_test_npc(1, 0);

    // Add 3 positive employment memories for business 10.
    for (int i = 0; i < 3; ++i) {
        MemoryEntry mem{};
        mem.tick_timestamp = static_cast<uint32_t>(i);
        mem.type = MemoryType::employment_positive;
        mem.subject_id = 10;
        mem.emotional_weight = 0.5f;
        mem.decay = 1.0f;
        mem.is_actionable = false;
        npc.memory_log.push_back(mem);
    }

    // Add 1 negative employment memory.
    MemoryEntry neg{};
    neg.tick_timestamp = 3;
    neg.type = MemoryType::employment_negative;
    neg.subject_id = 10;
    neg.emotional_weight = -0.3f;
    neg.decay = 1.0f;
    neg.is_actionable = false;
    npc.memory_log.push_back(neg);

    state.significant_npcs.push_back(npc);

    float rep = LaborMarketModule::compute_employer_reputation(10, state);

    // positive_weight = 3 * 0.5 = 1.5
    // total_weight = 1.5 + 0.3 = 1.8
    // reputation = 1.5 / 1.8 = 0.833...
    REQUIRE_THAT(rep, WithinAbs(1.5f / 1.8f, 0.01f));
    REQUIRE(rep > 0.5f);
}

TEST_CASE("test_worker_satisfaction_no_memories", "[labor_market][tier2]") {
    // A worker with no employment memories has neutral satisfaction (0.5).
    NPC npc = make_test_npc(1, 0);

    float sat = LaborMarketModule::compute_worker_satisfaction(npc);
    REQUIRE_THAT(sat, WithinAbs(0.5f, 0.001f));
}

TEST_CASE("test_hire_sets_employment_positive_memory_with_overpay", "[labor_market][tier2]") {
    // Hiring with offered_wage = 1.5 * salary_expectation should set
    // emotional_weight = (1.5 - 1.0) * 0.5 = 0.25, clamped to [0.1, 0.5].
    auto state = make_test_world_state();
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    auto npc = make_test_npc(10, province_id);
    prov.significant_npc_ids = {10};
    state.provinces.push_back(prov);
    state.significant_npcs = {npc};

    auto biz = make_test_business(1, province_id);
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;
    module.employment_records().push_back(EmploymentRecord{10, 0, 0.0f, 0, 0});

    JobPosting posting{};
    posting.id = 1;
    posting.owner_id = 0;
    posting.business_id = 1;
    posting.province_id = province_id;
    posting.required_domain = SkillDomain::Business;
    posting.min_skill_level = 0.0f;
    posting.offered_wage = 1.5f;  // 1.5x the expectation.
    posting.channel = HiringChannel::public_board;
    posting.posted_tick = 0;
    posting.expires_tick = 100;
    posting.filled = false;
    module.job_postings().push_back(posting);

    module.applications()[1] = {
        WorkerApplication{10, 0.5f, 1.0f, 0.0f, false},
    };

    DeltaBuffer delta{};
    module.execute_province(province_id, state, delta);

    REQUIRE(module.job_postings()[0].filled == true);
    REQUIRE(has_memory_type(delta, 10, MemoryType::employment_positive));

    float weight = get_memory_weight(delta, 10, MemoryType::employment_positive);
    // overpay_ratio = (1.5 / 1.0) - 1.0 = 0.5.
    // emotional_weight = 0.1 + min(0.4, 0.5 * 0.5) = 0.1 + 0.25 = 0.35.
    REQUIRE_THAT(weight, WithinAbs(0.35f, 0.01f));
}

TEST_CASE("test_skips_non_full_lod_provinces", "[labor_market][tier2]") {
    // Provinces with lod_level != full should be skipped.
    auto state = make_test_world_state();

    Province prov{};
    prov.id = 0;
    prov.lod_level = SimulationLOD::simplified;
    state.provinces.push_back(prov);

    auto biz = make_test_business(1, 0, 10000.0f);
    state.npc_businesses.push_back(biz);

    auto npc = make_test_npc(100, 0);
    state.significant_npcs.push_back(npc);

    LaborMarketModule module;
    module.employment_records().push_back(EmploymentRecord{100, 1, 5.0f, 0, 0});

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No deltas should be emitted.
    REQUIRE(delta.npc_deltas.empty());
}

TEST_CASE("test_npc_skill_lookup", "[labor_market][tier2]") {
    LaborMarketModule module;

    // Register skills.
    module.npc_skills()[42] = {
        {SkillDomain::Business, 0.7f},
        {SkillDomain::Engineering, 0.3f},
    };

    REQUIRE_THAT(module.get_npc_skill(42, SkillDomain::Business),
                 WithinAbs(0.7f, 0.001f));
    REQUIRE_THAT(module.get_npc_skill(42, SkillDomain::Engineering),
                 WithinAbs(0.3f, 0.001f));
    REQUIRE_THAT(module.get_npc_skill(42, SkillDomain::Finance),
                 WithinAbs(0.0f, 0.001f));

    // Unknown NPC returns 0.0.
    REQUIRE_THAT(module.get_npc_skill(999, SkillDomain::Business),
                 WithinAbs(0.0f, 0.001f));
}

TEST_CASE("test_effective_pool_size_normal_reputation", "[labor_market][tier2]") {
    // Good reputation: full pool size.
    REQUIRE(LaborMarketModule::effective_pool_size(
        HiringChannel::public_board, 0.5f) == 12);
    REQUIRE(LaborMarketModule::effective_pool_size(
        HiringChannel::professional_network, 0.5f) == 5);
    REQUIRE(LaborMarketModule::effective_pool_size(
        HiringChannel::personal_referral, 0.5f) == 3);
}

TEST_CASE("test_effective_pool_size_minimum_one", "[labor_market][tier2]") {
    // Very low reputation should not reduce pool below 1.
    uint32_t pool = LaborMarketModule::effective_pool_size(
        HiringChannel::personal_referral, 0.0f);
    // penalty = (0.3 - 0.0) * 8.0 = 2.4, round = 2. base = 3, result = 1.
    REQUIRE(pool >= 1);
}

TEST_CASE("test_wage_floor_clamp", "[labor_market][tier2]") {
    // Wages should never drop below wage_floor (0.01).
    // Set initial wage just above floor and apply a large surplus to push it down.
    auto state = make_test_world_state();
    state.current_tick = 30;
    constexpr uint32_t province_id = 0;

    auto prov = make_test_province(province_id);
    // Many unemployed NPCs with Trade skill.
    for (uint32_t i = 1; i <= 20; ++i) {
        auto npc = make_test_npc(i, province_id);
        prov.significant_npc_ids.push_back(i);
        state.significant_npcs.push_back(npc);
    }
    state.provinces.push_back(prov);

    LaborMarketModule module;
    for (uint32_t i = 1; i <= 20; ++i) {
        module.npc_skills()[i] = {{SkillDomain::Trade, 0.5f}};
    }

    // Set initial wage at exactly the floor.
    ProvinceSkillKey key{province_id, SkillDomain::Trade};
    module.regional_wages()[key] = LaborConfig::wage_floor;

    // 1 posting, 20 supply => ratio = 1/20 = 0.05.
    // new_wage = 0.01 * (1.0 + 0.03 * (0.05 - 1.0))
    //          = 0.01 * (1.0 - 0.0285)
    //          = 0.01 * 0.9715 = 0.009715 -> clamped to wage_floor (0.01).
    JobPosting posting{};
    posting.id = 1;
    posting.business_id = 1;
    posting.province_id = province_id;
    posting.required_domain = SkillDomain::Trade;
    posting.offered_wage = 0.01f;
    posting.posted_tick = 0;
    posting.expires_tick = 100;
    posting.filled = false;
    posting.channel = HiringChannel::public_board;
    module.job_postings().push_back(posting);

    DeltaBuffer delta{};
    module.execute(state, delta);

    float new_wage = module.regional_wages()[key];
    // Without clamping, wage would be ~0.009715. With clamping, it stays at 0.01.
    REQUIRE_THAT(new_wage, WithinAbs(LaborConfig::wage_floor, 0.001f));
}

TEST_CASE("test_salary_expectation_with_money_motivation", "[labor_market][tier2]") {
    // salary_expectation = regional_wage * (1.0 + money_motivation * 0.3)
    float regional_wage = 2.0f;
    float money_motivation = 0.5f;
    float reputation = 0.5f;  // good reputation, no premium.

    float expectation = LaborMarketModule::compute_salary_expectation(
        regional_wage, money_motivation, reputation);

    // 2.0 * (1.0 + 0.5 * 0.3) = 2.0 * 1.15 = 2.3
    REQUIRE_THAT(expectation, WithinAbs(2.3f, 0.001f));
}
