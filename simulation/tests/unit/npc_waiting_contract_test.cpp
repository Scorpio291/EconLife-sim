// EL-SLICE-N2 — the `waiting` contract (see core/world_state/npc.h).
//
// `waiting` means "present and free, took no discretionary daily action this
// tick". It is not a capacity state. These tests pin the contract at module
// level, one consumer at a time:
//
//   1. An NPC with a genuinely viable action takes it.
//   2. An NPC with no viable discretionary action remains inactive, and the
//      inaction decision does not depend on how the motivation vector happens
//      to be normalised.
//   3. Inactivity does not remove the NPC from mandatory mechanisms: it is paid
//      for the job it holds, can be hired, ages, is registered as an
//      investigator, and counts in the labour force. Imprisoned/dead/fled do not.
//   4. Changed circumstances restore participation.
//
// Tagged [n2][waiting_contract]. Integration-level checks (game world,
// justice loop, determinism) are in tests/integration/el_slice_n2_test.cpp.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>

#include "core/world_state/apply_deltas.h"  // rebuild_npc_indices
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/labor_market/labor_market_module.h"
#include "modules/npc_behavior/npc_behavior_module.h"
#include "modules/population_aging/population_aging_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {

// The shipped package value (packages/base_game/config/modules.json). Stated
// explicitly so these tests do not silently follow a header default.
constexpr float kShippedInactionThreshold = 0.10f;

NpcBehaviorModule shipped_behavior() {
    NpcBehaviorModuleConfig m{};
    m.inaction_threshold = kShippedInactionThreshold;
    return NpcBehaviorModule(m);
}

WorldState make_world(uint32_t tick = 1) {
    WorldState state{};
    state.current_tick = tick;
    state.world_seed = 42;
    state.player.reset();
    state.lod2_price_index.reset();
    state.ticks_this_session = 1;
    state.game_mode = GameMode::standard;
    state.current_schema_version = 1;
    state.network_health_dirty = false;
    return state;
}

Province make_province(uint32_t id) {
    Province prov{};
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = id;
    prov.region_id = 0;
    prov.nation_id = 0;
    prov.lod_level = SimulationLOD::full;
    prov.conditions.stability_score = 0.5f;
    prov.conditions.inequality_index = 0.3f;
    prov.cohort_stats->crime_rate = 0.1f;
    prov.cohort_stats->formal_employment_rate = 0.7f;
    prov.infrastructure_rating = 0.6f;
    prov.demographics.total_population = 100000;
    prov.demographics.median_age = 35.0f;
    prov.demographics.education_level = 0.6f;
    prov.demographics.income_low_fraction = 0.3f;
    prov.demographics.income_middle_fraction = 1.0f;
    prov.demographics.income_high_fraction = 0.1f;
    return prov;
}

NPC make_npc(uint32_t id, NPCStatus status = NPCStatus::active) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::worker;
    npc.risk_tolerance = 0.5f;
    npc.current_province_id = 0;
    npc.home_province_id = 0;
    npc.status = status;
    npc.capital = 1000.0f;
    npc.social_capital = 0.5f;
    npc.age_years = 40.0f;
    for (auto& w : npc.motivations.weights)
        w = 1.0f / 8.0f;
    return npc;
}

// A profile whose weight sits on revenge and ideology — motives none of the
// routine daily actions serve (no revenge action exists; ideology is served
// only by whistleblowing, which needs a witnessed crime).
void set_unserved_profile(NPC& npc) {
    npc.motivations.weights = {0.01f, 0.01f, 0.01f, 0.45f, 0.45f, 0.05f, 0.01f, 0.01f};
}

bool status_written(const DeltaBuffer& d, uint32_t id, NPCStatus s) {
    for (const auto& nd : d.npc_deltas)
        if (nd.npc_id == id && nd.new_status.has_value() && *nd.new_status == s)
            return true;
    return false;
}

const NPCDelta* delta_for(const DeltaBuffer& d, uint32_t id) {
    for (const auto& nd : d.npc_deltas)
        if (nd.npc_id == id)
            return &nd;
    return nullptr;
}

float capital_paid(const DeltaBuffer& d, uint32_t id) {
    float total = 0.0f;
    for (const auto& nd : d.npc_deltas)
        if (nd.npc_id == id && nd.capital_delta.has_value())
            total += *nd.capital_delta;
    return total;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. A genuinely viable action is taken
// ---------------------------------------------------------------------------
TEST_CASE("N2 contract 1: an NPC with a viable action takes it", "[n2][waiting_contract]") {
    auto state = make_world();
    state.provinces.push_back(make_province(0));
    state.significant_npcs.push_back(make_npc(100));  // balanced profile
    rebuild_npc_indices(state);

    const NpcBehaviorModule module = shipped_behavior();
    const auto ctx = module.province_context(state, 0);
    const auto choice =
        module.choose_action(state.significant_npcs[0],
                             module.generate_candidates(state.significant_npcs[0], state, ctx));
    // Explainable: the best option is work (financial motive, ~0.78 chance of
    // pay at 70% formal employment) at ~0.39 of what this NPC most wants.
    CHECK(choice.best.action == DailyAction::work);
    CHECK(module.chooses_to_act(state.significant_npcs[0], choice));

    NpcBehaviorModule mut = shipped_behavior();
    DeltaBuffer delta{};
    mut.execute_province(0, state, delta);
    CHECK_FALSE(status_written(delta, 100, NPCStatus::waiting));
    const NPCDelta* d = delta_for(delta, 100);
    REQUIRE(d != nullptr);
    REQUIRE(d->new_memory_entry.has_value());  // the action happened
    CHECK(capital_paid(delta, 100) > 0.0f);    // and paid the informal wage
}

// ---------------------------------------------------------------------------
// 2. No viable discretionary action: the NPC may stay inactive
// ---------------------------------------------------------------------------
TEST_CASE("N2 contract 2: an NPC with no viable action waits, and nothing happens for it",
          "[n2][waiting_contract]") {
    auto state = make_world();
    state.provinces.push_back(make_province(0));
    NPC npc = make_npc(100);
    set_unserved_profile(npc);
    state.significant_npcs.push_back(npc);
    rebuild_npc_indices(state);

    NpcBehaviorModule module = shipped_behavior();
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    CHECK(status_written(delta, 100, NPCStatus::waiting));
    const NPCDelta* d = delta_for(delta, 100);
    REQUIRE(d != nullptr);
    CHECK_FALSE(d->new_memory_entry.has_value());  // no action taken
    CHECK_FALSE(d->capital_delta.has_value());     // no action income or cost
}

TEST_CASE("N2 contract 2b: the inaction gate does not depend on weight normalisation",
          "[n2][waiting_contract]") {
    // The same preferences written at two scales (weights x 0.5, as if the
    // OutcomeType enum had twice as many entries) give the same decision.
    // Under the previous absolute gate the full NPC's net utility for a
    // near-certain, large financial payoff is 0.125 x 1.0 x 0.9 = 0.1125 (acts
    // at 0.10) and the halved NPC's is 0.056 (waits): the gate measured the
    // normalisation, not the action.
    NPC full = make_npc(1);
    NPC half = make_npc(2);
    for (auto& w : half.motivations.weights)
        w *= 0.5f;
    std::vector<ActionOutcome> work = {{OutcomeType::financial_gain, 1.0f, 0.9f}};
    const NpcBehaviorModule m = shipped_behavior();
    const float u_full =
        m.evaluate_action(full, DailyAction::work, work, 0.0f, -2.0f, 0.3f).net_utility;
    const float u_half =
        m.evaluate_action(half, DailyAction::work, work, 0.0f, -2.0f, 0.3f).net_utility;
    REQUIRE_THAT(u_half, WithinAbs(0.5f * u_full, 1e-6f));
    CHECK_THAT(NpcBehaviorModule::relative_utility(full, u_full),
               WithinAbs(NpcBehaviorModule::relative_utility(half, u_half), 1e-6f));
    NpcBehaviorModule::ActionChoice c_full{}, c_half{};
    c_full.best.net_utility = u_full;
    c_half.best.net_utility = u_half;
    CHECK(m.chooses_to_act(full, c_full));
    CHECK(m.chooses_to_act(half, c_half));
}

// ---------------------------------------------------------------------------
// 3. Inactivity does not remove the NPC from mandatory mechanisms
// ---------------------------------------------------------------------------
TEST_CASE("N2 contract 3a: a waiting employee is paid for the job it holds",
          "[n2][waiting_contract][labor_market]") {
    auto state = make_world();
    state.provinces.push_back(make_province(0));
    state.significant_npcs.push_back(make_npc(100, NPCStatus::waiting));
    state.significant_npcs.push_back(make_npc(101, NPCStatus::imprisoned));
    rebuild_npc_indices(state);
    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.cash = 10000.0f;
    biz.revenue_per_tick = 100.0f;
    biz.cost_per_tick = 50.0f;
    biz.actor_tech_state = ActorTechnologyState{1.0f, {}};
    state.npc_businesses.push_back(biz);

    LaborMarketModule module;
    module.employment_records().push_back(EmploymentRecord{100, 1, 5.0f, 0, 0});
    module.employment_records().push_back(EmploymentRecord{101, 1, 5.0f, 0, 0});
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    CHECK_THAT(capital_paid(delta, 100), WithinAbs(5.0f, 1e-4f));  // waiting: paid
    CHECK_THAT(capital_paid(delta, 101), WithinAbs(0.0f, 1e-6f));  // imprisoned: not paid
}

TEST_CASE("N2 contract 3b: a waiting unemployed NPC can be hired",
          "[n2][waiting_contract][labor_market]") {
    auto state = make_world();
    state.provinces.push_back(make_province(0));
    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.cash = 10000.0f;
    biz.revenue_per_tick = 800.0f;  // supports several workers
    biz.cost_per_tick = 50.0f;
    biz.actor_tech_state = ActorTechnologyState{1.0f, {}};
    state.npc_businesses.push_back(biz);
    for (uint32_t i = 0; i < 5; ++i)
        state.significant_npcs.push_back(make_npc(100 + i, NPCStatus::waiting));
    rebuild_npc_indices(state);

    LaborMarketModule module;
    state.current_tick = 30;  // monthly hiring cadence
    module.init_for_tick(state);
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    int hired = 0;
    for (const auto& rec : module.employment_records())
        if (rec.employer_business_id == 1u)
            ++hired;
    CHECK(hired > 0);
}

TEST_CASE("N2 contract 3c: unemployment counts a waiting NPC without a job, not one with a job",
          "[n2][waiting_contract][labor_market]") {
    // Labour force = present NPCs. Waiting without a formal job = no formal and
    // no informal work = unemployed. Waiting WITH a formal job = employed.
    auto state = make_world();
    state.provinces.push_back(make_province(0));
    state.significant_npcs.push_back(make_npc(100, NPCStatus::waiting));  // job below
    state.significant_npcs.push_back(make_npc(101, NPCStatus::waiting));  // no job
    state.significant_npcs.push_back(make_npc(102, NPCStatus::dead));     // not in force
    rebuild_npc_indices(state);
    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.cash = 10000.0f;
    biz.revenue_per_tick = 100.0f;
    biz.cost_per_tick = 50.0f;
    biz.actor_tech_state = ActorTechnologyState{1.0f, {}};
    state.npc_businesses.push_back(biz);
    state.provinces[0].cohort_stats->unemployment_rate = 0.0f;

    LaborMarketModule module;
    module.employment_records().push_back(EmploymentRecord{100, 1, 5.0f, 0, 0});
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Sample = 1 unemployed / 2 present = 0.5; delta = 0.05 x (0.5 - 0.0).
    bool found = false;
    for (const auto& rd : delta.region_deltas)
        if (rd.unemployment_rate_delta.has_value()) {
            CHECK_THAT(*rd.unemployment_rate_delta, WithinAbs(0.025f, 1e-5f));
            found = true;
        }
    CHECK(found);
}

TEST_CASE("N2 contract 3d: a waiting NPC ages", "[n2][waiting_contract][population_aging]") {
    auto state = make_world(PopulationAgingModule::TICKS_PER_YEAR);
    state.world_seed = 1;
    state.provinces.push_back(make_province(0));
    state.significant_npcs.push_back(make_npc(5, NPCStatus::waiting));
    state.significant_npcs.push_back(make_npc(6, NPCStatus::imprisoned));
    rebuild_npc_indices(state);

    PopulationAgingModule module;
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    const NPCDelta* waiting = delta_for(delta, 5);
    REQUIRE(waiting != nullptr);
    REQUIRE(waiting->age_delta.has_value());
    CHECK_THAT(*waiting->age_delta, WithinAbs(1.0f, 1e-6f));
    // Unchanged pre-existing behaviour, not decided here: imprisoned NPCs do
    // not age in population_aging (recorded as a follow-up in the handoff).
    CHECK(delta_for(delta, 6) == nullptr);
}

TEST_CASE("N2 contract 3e: a waiting law-enforcement NPC is still an investigator",
          "[n2][waiting_contract][investigator_engine]") {
    auto state = make_world();
    state.provinces.push_back(make_province(0));
    NPC le_waiting = make_npc(10, NPCStatus::waiting);
    le_waiting.role = NPCRole::law_enforcement;
    NPC le_jailed = make_npc(11, NPCStatus::imprisoned);
    le_jailed.role = NPCRole::law_enforcement;
    NPC le_dead = make_npc(12, NPCStatus::dead);
    le_dead.role = NPCRole::law_enforcement;
    state.significant_npcs = {le_waiting, le_jailed, le_dead};
    rebuild_npc_indices(state);

    InvestigatorEngineModule module;
    module.init_for_tick(state);

    bool has_waiting = false, has_jailed = false, has_dead = false;
    for (const auto& c : module.cases()) {
        has_waiting |= c.investigator_npc_id == 10;
        has_jailed |= c.investigator_npc_id == 11;
        has_dead |= c.investigator_npc_id == 12;
    }
    CHECK(has_waiting);
    CHECK_FALSE(has_jailed);
    CHECK_FALSE(has_dead);
}

// ---------------------------------------------------------------------------
// 4. Changed circumstances restore participation
// ---------------------------------------------------------------------------
TEST_CASE("N2 contract 4: a waiting NPC returns to active when circumstances change",
          "[n2][waiting_contract]") {
    // A worker whose dominant motive is ideological has nothing to do on an
    // ordinary day and waits. Then it witnesses serious illegal activity at
    // work: whistleblowing becomes available, serves exactly that motive, and
    // the NPC acts again. Nothing about the gate changed; the world did.
    auto state = make_world();
    state.provinces.push_back(make_province(0));
    NPC npc = make_npc(100, NPCStatus::waiting);
    set_unserved_profile(npc);
    npc.risk_tolerance = 0.6f;
    state.significant_npcs.push_back(npc);
    rebuild_npc_indices(state);

    {
        NpcBehaviorModule module = shipped_behavior();
        DeltaBuffer delta{};
        module.execute_province(0, state, delta);
        CHECK(status_written(delta, 100, NPCStatus::waiting));
        CHECK_FALSE(status_written(delta, 100, NPCStatus::active));
    }

    MemoryEntry witnessed{};
    witnessed.tick_timestamp = state.current_tick;
    witnessed.type = MemoryType::witnessed_illegal_activity;
    witnessed.subject_id = 7;
    witnessed.emotional_weight = -0.8f;
    witnessed.decay = 1.0f;
    witnessed.is_actionable = true;
    state.significant_npcs[0].memory_log.push_back(witnessed);
    state.current_tick += 1;

    NpcBehaviorModule module = shipped_behavior();
    const auto ctx = module.province_context(state, 0);
    const auto choice =
        module.choose_action(state.significant_npcs[0],
                             module.generate_candidates(state.significant_npcs[0], state, ctx));
    CHECK(choice.best.action == DailyAction::whistleblow);

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);
    CHECK(status_written(delta, 100, NPCStatus::active));
    CHECK_FALSE(delta.evidence_deltas.empty());  // the whistleblow produced testimony
}
