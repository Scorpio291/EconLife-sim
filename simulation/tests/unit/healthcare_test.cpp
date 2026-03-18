// Healthcare module unit tests.
// All tests tagged [healthcare][tier5].
//
// Tests verify the NPC health processing pipeline:
//   1. Passive health recovery based on province access and quality
//   2. Treatment for critically ill NPCs who can afford care
//   3. NPC death when health reaches zero
//   4. Overload quality degradation when capacity exceeded
//   5. Sick leave fraction and effective labour supply computation
//   6. Static utility function correctness
//   7. Module interface properties (name, province_parallel, runs_after)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "modules/healthcare/healthcare_module.h"
#include "modules/healthcare/healthcare_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Test helpers — create minimal WorldState and supporting structures
// ---------------------------------------------------------------------------

namespace {

// Create a minimal WorldState suitable for healthcare tests.
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
Province make_test_province(uint32_t id) {
    Province prov{};
    prov.id = id;
    prov.region_id = 0;
    prov.nation_id = 0;
    prov.conditions.stability_score = 0.5f;
    prov.conditions.inequality_index = 0.3f;
    prov.conditions.crime_rate = 0.1f;
    prov.conditions.criminal_dominance_index = 0.0f;
    prov.conditions.formal_employment_rate = 0.7f;
    prov.conditions.regulatory_compliance_index = 0.8f;
    prov.conditions.drought_modifier = 1.0f;
    prov.conditions.flood_modifier = 1.0f;
    prov.conditions.addiction_rate = 0.0f;
    prov.infrastructure_rating = 0.6f;
    prov.demographics.total_population = 100000;
    prov.cohort_stats = nullptr;
    return prov;
}

// Create an NPC with sensible defaults for healthcare tests.
NPC make_test_npc(uint32_t id, uint32_t province_id, float capital = 1000.0f) {
    NPC npc{};
    npc.id = id;
    npc.role = NPCRole::worker;
    npc.status = NPCStatus::active;
    npc.current_province_id = province_id;
    npc.home_province_id = province_id;
    npc.capital = capital;
    npc.social_capital = 0.0f;
    npc.risk_tolerance = 0.5f;
    npc.movement_follower_count = 0;
    npc.motivations.weights = {0.125f, 0.125f, 0.125f, 0.125f,
                               0.125f, 0.125f, 0.125f, 0.125f};
    return npc;
}

// Create a HealthcareProfile with sensible defaults.
HealthcareProfile make_test_profile(float access = 0.8f,
                                     float quality = 0.9f,
                                     float cost = 500.0f,
                                     float utilisation = 0.5f) {
    HealthcareProfile profile{};
    profile.access_level = access;
    profile.quality_level = quality;
    profile.cost_per_treatment = cost;
    profile.capacity_utilisation = utilisation;
    return profile;
}

// Create a ProvinceHealthState for a province.
HealthcareModule::ProvinceHealthState make_test_province_health(
        uint32_t province_id,
        HealthcareProfile profile = make_test_profile()) {
    HealthcareModule::ProvinceHealthState phs{};
    phs.province_id = province_id;
    phs.profile = profile;
    phs.sick_leave_fraction = 0.0f;
    phs.effective_labour_supply = 0.0f;
    return phs;
}

// Create an NpcHealthRecord.
HealthcareModule::NpcHealthRecord make_test_health_record(
        uint32_t npc_id,
        float health = 1.0f,
        uint32_t last_treatment_tick = 0) {
    HealthcareModule::NpcHealthRecord rec{};
    rec.npc_id = npc_id;
    rec.health = health;
    rec.last_treatment_tick = last_treatment_tick;
    return rec;
}

}  // namespace

// ===========================================================================
// Section 1: Static Utility Function Tests
// ===========================================================================

TEST_CASE("compute_passive_recovery basic", "[healthcare][tier5]") {
    // access=0.8, quality=0.9, base_rate=0.001 => 0.00072
    float result = HealthcareModule::compute_passive_recovery(0.8f, 0.9f, 0.001f);
    REQUIRE_THAT(result, WithinAbs(0.00072f, 1e-7f));
}

TEST_CASE("compute_passive_recovery zero access", "[healthcare][tier5]") {
    float result = HealthcareModule::compute_passive_recovery(0.0f, 0.9f, 0.001f);
    REQUIRE_THAT(result, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("compute_passive_recovery zero quality", "[healthcare][tier5]") {
    float result = HealthcareModule::compute_passive_recovery(0.8f, 0.0f, 0.001f);
    REQUIRE_THAT(result, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("compute_passive_recovery full access and quality", "[healthcare][tier5]") {
    float result = HealthcareModule::compute_passive_recovery(1.0f, 1.0f, 0.001f);
    REQUIRE_THAT(result, WithinAbs(0.001f, 1e-7f));
}

TEST_CASE("compute_treatment_boost", "[healthcare][tier5]") {
    // quality=0.9, boost=0.25 => 0.225
    float result = HealthcareModule::compute_treatment_boost(0.9f, 0.25f);
    REQUIRE_THAT(result, WithinAbs(0.225f, 1e-6f));
}

TEST_CASE("compute_treatment_boost with zero quality", "[healthcare][tier5]") {
    float result = HealthcareModule::compute_treatment_boost(0.0f, 0.25f);
    REQUIRE_THAT(result, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("compute_overload_quality below threshold", "[healthcare][tier5]") {
    // capacity=0.80 < threshold=0.85 => no penalty
    float result = HealthcareModule::compute_overload_quality(0.80f, 0.80f, 0.85f, 0.999f);
    REQUIRE_THAT(result, WithinAbs(0.80f, 1e-7f));
}

TEST_CASE("compute_overload_quality at threshold", "[healthcare][tier5]") {
    // capacity=0.85 == threshold=0.85 => no penalty (> not >=)
    float result = HealthcareModule::compute_overload_quality(0.80f, 0.85f, 0.85f, 0.999f);
    REQUIRE_THAT(result, WithinAbs(0.80f, 1e-7f));
}

TEST_CASE("compute_overload_quality above threshold", "[healthcare][tier5]") {
    // capacity=0.90 > threshold=0.85 => quality * 0.999
    float result = HealthcareModule::compute_overload_quality(0.80f, 0.90f, 0.85f, 0.999f);
    REQUIRE_THAT(result, WithinAbs(0.80f * 0.999f, 1e-7f));
}

TEST_CASE("compute_sick_leave_fraction basic", "[healthcare][tier5]") {
    // 3 sick out of 100 => 0.03
    float result = HealthcareModule::compute_sick_leave_fraction(3, 100);
    REQUIRE_THAT(result, WithinAbs(0.03f, 1e-7f));
}

TEST_CASE("compute_sick_leave_fraction zero labour force", "[healthcare][tier5]") {
    // Avoid division by zero: 0 labour force => 0.0 fraction.
    float result = HealthcareModule::compute_sick_leave_fraction(5, 0);
    REQUIRE_THAT(result, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("compute_sick_leave_fraction zero sick", "[healthcare][tier5]") {
    float result = HealthcareModule::compute_sick_leave_fraction(0, 100);
    REQUIRE_THAT(result, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("compute_effective_labour_supply", "[healthcare][tier5]") {
    // labour_force=100, sick_leave=0.03, impact=0.80
    // effective = 100 * (1.0 - 0.03 * 0.80) = 100 * 0.976 = 97.6
    float result = HealthcareModule::compute_effective_labour_supply(100, 0.03f, 0.80f);
    REQUIRE_THAT(result, WithinAbs(97.6f, 1e-4f));
}

TEST_CASE("compute_effective_labour_supply zero sick leave", "[healthcare][tier5]") {
    float result = HealthcareModule::compute_effective_labour_supply(100, 0.0f, 0.80f);
    REQUIRE_THAT(result, WithinAbs(100.0f, 1e-4f));
}

TEST_CASE("compute_effective_labour_supply all sick", "[healthcare][tier5]") {
    // All sick: fraction=1.0, impact=0.80 => effective = 100 * (1.0 - 0.80) = 20.0
    float result = HealthcareModule::compute_effective_labour_supply(100, 1.0f, 0.80f);
    REQUIRE_THAT(result, WithinAbs(20.0f, 1e-4f));
}

// ===========================================================================
// Section 2: Module Interface Property Tests
// ===========================================================================

TEST_CASE("module interface properties", "[healthcare][tier5]") {
    HealthcareModule mod;
    REQUIRE(mod.name() == "healthcare");
    REQUIRE(mod.package_id() == "base_game");
    REQUIRE(mod.scope() == ModuleScope::v1);
}

TEST_CASE("province parallel flag", "[healthcare][tier5]") {
    HealthcareModule mod;
    REQUIRE(mod.is_province_parallel() == true);
}

TEST_CASE("runs after financial_distribution", "[healthcare][tier5]") {
    HealthcareModule mod;
    auto deps = mod.runs_after();
    REQUIRE(deps.size() == 1);
    REQUIRE(deps[0] == "financial_distribution");
}

TEST_CASE("runs before is empty", "[healthcare][tier5]") {
    HealthcareModule mod;
    auto deps = mod.runs_before();
    REQUIRE(deps.empty());
}

// ===========================================================================
// Section 3: Integration Tests — execute_province
// ===========================================================================

TEST_CASE("passive recovery applies correctly", "[healthcare][tier5]") {
    // INTERFACE.md test_passive_recovery_applies_correctly:
    // NPC health=0.70, access=0.8, quality=0.9 => recovery = 0.00072
    // Expected final health = 0.70072
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.8f, 0.9f, 500.0f, 0.5f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.70f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(0.70072f, 1e-6f));
}

TEST_CASE("critical NPC receives treatment when affordable", "[healthcare][tier5]") {
    // INTERFACE.md: health=0.25, cash=1000, cost=500, quality=0.9
    // Treatment boost = 0.25 * 0.9 = 0.225
    // Passive recovery = 0.8 * 0.9 * 0.001 = 0.00072
    // Final health = 0.25 + 0.00072 + 0.225 = 0.47572
    HealthcareModule mod;
    WorldState state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.8f, 0.9f, 500.0f, 0.5f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.25f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(0.25f + 0.00072f + 0.225f, 1e-5f));

    // Verify treatment tick updated.
    REQUIRE(hr->last_treatment_tick == 10);

    // Verify capital deduction delta exists.
    bool found_cost_delta = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 1 && nd.capital_delta.has_value()) {
            REQUIRE_THAT(nd.capital_delta.value(), WithinAbs(-500.0f, 1e-6f));
            found_cost_delta = true;
        }
    }
    REQUIRE(found_cost_delta);
}

TEST_CASE("critical NPC denied treatment when broke", "[healthcare][tier5]") {
    // INTERFACE.md: health=0.20, cash=100, cost=500
    // Cannot afford treatment; only passive recovery applied.
    // Passive recovery = 0.8 * 0.9 * 0.001 = 0.00072
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 100.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.8f, 0.9f, 500.0f, 0.5f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.20f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    // Only passive recovery: 0.20 + 0.00072
    REQUIRE_THAT(hr->health, WithinAbs(0.20072f, 1e-6f));

    // Verify no capital delta was emitted (no treatment).
    bool found_cost_delta = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 1 && nd.capital_delta.has_value()) {
            found_cost_delta = true;
        }
    }
    REQUIRE_FALSE(found_cost_delta);
}

TEST_CASE("no recovery without healthcare access", "[healthcare][tier5]") {
    // INTERFACE.md: access=0.0, NPC health=0.50
    // Recovery = 0.0 * quality * base_rate = 0.0; no treatment available.
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.0f, 0.9f, 500.0f, 0.5f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.50f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(0.50f, 1e-7f));
}

TEST_CASE("overload degrades quality", "[healthcare][tier5]") {
    // INTERFACE.md: capacity_utilisation=0.90, quality=0.80
    // quality *= 0.999 => 0.7992
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    // No NPCs needed; we just test the overload degradation.
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.8f, 0.80f, 500.0f, 0.90f)));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    REQUIRE_THAT(phs->profile.quality_level, WithinAbs(0.80f * 0.999f, 1e-6f));
}

TEST_CASE("sick leave fraction computed correctly", "[healthcare][tier5]") {
    // INTERFACE.md: 10 NPCs, 3 with health < 0.50 (labour_impairment_threshold)
    // sick_leave_fraction = 3/10 = 0.30
    // effective_labour_supply = 10 * (1.0 - 0.30 * 0.80) = 10 * 0.76 = 7.6
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    // Create 10 NPCs. NPCs 1-3 have low health, NPCs 4-10 have good health.
    for (uint32_t i = 1; i <= 10; ++i) {
        NPC npc = make_test_npc(i, 0, 1000.0f);
        state.significant_npcs.push_back(npc);

        float health = (i <= 3) ? 0.30f : 0.80f;
        mod.npc_health_records().push_back(make_test_health_record(i, health));
    }

    // No treatment (high health threshold for sick, not critical).
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.8f, 0.9f, 500.0f, 0.5f)));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    REQUIRE_THAT(phs->sick_leave_fraction, WithinAbs(0.30f, 1e-6f));
    REQUIRE_THAT(phs->effective_labour_supply, WithinAbs(7.6f, 1e-4f));
}

TEST_CASE("NPC death at zero health", "[healthcare][tier5]") {
    // NPC health very low in province with no access (no recovery, no treatment).
    // After passive recovery of 0.0, health remains at 0.0. Status set to dead.
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 0.0f);
    state.significant_npcs.push_back(npc);

    // Health exactly at 0.0 at entry, no access.
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.0f, 0.0f, 0.0f, 0.0f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.0f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Verify death delta emitted.
    bool found_death = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 1 && nd.new_status.has_value()
            && nd.new_status.value() == NPCStatus::dead) {
            found_death = true;
        }
    }
    REQUIRE(found_death);
}

TEST_CASE("health clamped to one", "[healthcare][tier5]") {
    // NPC health=0.999, access=1.0, quality=1.0 => recovery=0.001 => health=1.0 (clamped)
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 1.0f, 500.0f, 0.5f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.9999f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(1.0f, 1e-7f));
}

TEST_CASE("treatment only when access positive", "[healthcare][tier5]") {
    // NPC health below critical, has cash, but access=0 => no treatment.
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 10000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.0f, 0.9f, 100.0f, 0.5f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.20f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    // No recovery (access=0), no treatment (access=0).
    REQUIRE_THAT(hr->health, WithinAbs(0.20f, 1e-7f));
    REQUIRE(hr->last_treatment_tick == 0);
}

TEST_CASE("treatment deducts cost from NPC capital", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state(5);
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 2000.0f);
    state.significant_npcs.push_back(npc);

    float cost = 750.0f;
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 1.0f, cost, 0.0f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.10f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Find capital delta for this NPC.
    float total_capital_delta = 0.0f;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 1 && nd.capital_delta.has_value()) {
            total_capital_delta += nd.capital_delta.value();
        }
    }
    REQUIRE_THAT(total_capital_delta, WithinAbs(-cost, 1e-6f));
}

TEST_CASE("capacity utilisation incremented by treatment", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    float initial_utilisation = 0.50f;
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 0.9f, 100.0f, initial_utilisation)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.20f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    REQUIRE_THAT(phs->profile.capacity_utilisation,
                 WithinAbs(initial_utilisation + HealthcareModule::Constants::capacity_per_treatment, 1e-7f));
}

TEST_CASE("execute processes all provinces", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();

    // Two provinces.
    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    // One NPC per province.
    NPC npc0 = make_test_npc(1, 0, 1000.0f);
    NPC npc1 = make_test_npc(2, 1, 1000.0f);
    state.significant_npcs.push_back(npc0);
    state.significant_npcs.push_back(npc1);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 1.0f, 500.0f, 0.0f)));
    mod.province_health_states().push_back(
        make_test_province_health(1, make_test_profile(0.5f, 0.5f, 500.0f, 0.0f)));

    mod.npc_health_records().push_back(make_test_health_record(1, 0.50f));
    mod.npc_health_records().push_back(make_test_health_record(2, 0.50f));

    DeltaBuffer delta{};
    mod.execute(state, delta);

    // NPC 1 in province 0: access=1.0, quality=1.0 => recovery = 0.001
    auto* hr1 = mod.find_npc_health(1);
    REQUIRE(hr1 != nullptr);
    REQUIRE_THAT(hr1->health, WithinAbs(0.501f, 1e-6f));

    // NPC 2 in province 1: access=0.5, quality=0.5 => recovery = 0.00025
    auto* hr2 = mod.find_npc_health(2);
    REQUIRE(hr2 != nullptr);
    REQUIRE_THAT(hr2->health, WithinAbs(0.50025f, 1e-6f));
}

TEST_CASE("multiple NPC treatment in single province", "[healthcare][tier5]") {
    // Two NPCs below critical threshold in same province; both get treated.
    HealthcareModule mod;
    WorldState state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));

    NPC npc1 = make_test_npc(1, 0, 1000.0f);
    NPC npc2 = make_test_npc(2, 0, 1000.0f);
    state.significant_npcs.push_back(npc1);
    state.significant_npcs.push_back(npc2);

    float cost = 200.0f;
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 0.8f, cost, 0.0f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.10f));
    mod.npc_health_records().push_back(make_test_health_record(2, 0.15f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Both should have received treatment.
    auto* hr1 = mod.find_npc_health(1);
    auto* hr2 = mod.find_npc_health(2);
    REQUIRE(hr1 != nullptr);
    REQUIRE(hr2 != nullptr);

    // Treatment boost = 0.25 * 0.8 = 0.20
    // Passive recovery = 1.0 * 0.8 * 0.001 = 0.0008
    // NPC 1: 0.10 + 0.0008 + 0.20 = 0.3008
    // NPC 2: 0.15 + 0.0008 + 0.20 = 0.3508
    REQUIRE_THAT(hr1->health, WithinAbs(0.3008f, 1e-4f));
    REQUIRE_THAT(hr2->health, WithinAbs(0.3508f, 1e-4f));

    // Both had treatment tick updated.
    REQUIRE(hr1->last_treatment_tick == 10);
    REQUIRE(hr2->last_treatment_tick == 10);

    // Capacity utilisation incremented twice.
    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    REQUIRE_THAT(phs->profile.capacity_utilisation,
                 WithinAbs(2.0f * HealthcareModule::Constants::capacity_per_treatment, 1e-7f));
}

TEST_CASE("inactive NPC is not processed", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    npc.status = NPCStatus::imprisoned;
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 1.0f, 100.0f, 0.0f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.50f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Health should be unchanged — NPC was not processed.
    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(0.50f, 1e-7f));
}

TEST_CASE("dead NPC is not processed for recovery", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    npc.status = NPCStatus::dead;
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 1.0f, 100.0f, 0.0f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.0f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("province without health state is skipped", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    // Do NOT register a ProvinceHealthState for province 0.
    mod.npc_health_records().push_back(make_test_health_record(1, 0.50f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Health unchanged (province was skipped).
    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(0.50f, 1e-7f));
}

TEST_CASE("NPC in different province is not affected", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));
    state.provinces.push_back(make_test_province(1));

    // NPC is in province 1 but we process province 0.
    NPC npc = make_test_npc(1, 1, 1000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 1.0f, 100.0f, 0.0f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.50f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Health unchanged (NPC not in province 0).
    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE_THAT(hr->health, WithinAbs(0.50f, 1e-7f));
}

TEST_CASE("treatment generates memory entry", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state(5);
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 0.9f, 100.0f, 0.0f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.20f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Find memory entry delta.
    bool found_memory = false;
    for (const auto& nd : delta.npc_deltas) {
        if (nd.npc_id == 1 && nd.new_memory_entry.has_value()) {
            REQUIRE(nd.new_memory_entry->type == MemoryType::event);
            REQUIRE(nd.new_memory_entry->tick_timestamp == 5);
            found_memory = true;
        }
    }
    REQUIRE(found_memory);
}

TEST_CASE("deterministic processing order by NPC id", "[healthcare][tier5]") {
    // NPCs added in reverse order should still be processed in ascending id order.
    HealthcareModule mod;
    WorldState state = make_test_world_state(10);
    state.provinces.push_back(make_test_province(0));

    // Add NPCs in reverse order.
    NPC npc3 = make_test_npc(3, 0, 1000.0f);
    NPC npc1 = make_test_npc(1, 0, 1000.0f);
    NPC npc2 = make_test_npc(2, 0, 1000.0f);
    state.significant_npcs.push_back(npc3);
    state.significant_npcs.push_back(npc1);
    state.significant_npcs.push_back(npc2);

    float cost = 100.0f;
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 0.8f, cost, 0.0f)));

    // All below critical.
    mod.npc_health_records().push_back(make_test_health_record(3, 0.10f));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.10f));
    mod.npc_health_records().push_back(make_test_health_record(2, 0.10f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    // Verify all three were treated (capital deltas emitted).
    // Capacity utilisation should be incremented 3 times.
    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    REQUIRE_THAT(phs->profile.capacity_utilisation,
                 WithinAbs(3.0f * HealthcareModule::Constants::capacity_per_treatment, 1e-7f));
}

TEST_CASE("no overload when capacity below threshold", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.8f, 0.80f, 500.0f, 0.50f)));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    // Quality unchanged when below overload threshold.
    REQUIRE_THAT(phs->profile.quality_level, WithinAbs(0.80f, 1e-7f));
}

TEST_CASE("capacity utilisation clamped to 1.0", "[healthcare][tier5]") {
    // Set capacity very close to 1.0 and treat an NPC; should clamp.
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 1000.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(1.0f, 0.9f, 100.0f, 0.9999f)));
    mod.npc_health_records().push_back(make_test_health_record(1, 0.10f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    // 0.9999 + 0.001 = 1.0009, clamped to 1.0.
    REQUIRE(phs->profile.capacity_utilisation <= 1.0f);
}

TEST_CASE("find_npc_health returns nullptr for unknown id", "[healthcare][tier5]") {
    HealthcareModule mod;
    mod.npc_health_records().push_back(make_test_health_record(1, 0.50f));
    REQUIRE(mod.find_npc_health(999) == nullptr);
}

TEST_CASE("find_province_health returns nullptr for unknown id", "[healthcare][tier5]") {
    HealthcareModule mod;
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile()));
    REQUIRE(mod.find_province_health(999) == nullptr);
}

TEST_CASE("sick leave fraction is zero when all NPCs healthy", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    for (uint32_t i = 1; i <= 5; ++i) {
        NPC npc = make_test_npc(i, 0, 1000.0f);
        state.significant_npcs.push_back(npc);
        mod.npc_health_records().push_back(make_test_health_record(i, 0.90f));
    }

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.8f, 0.9f, 500.0f, 0.5f)));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    REQUIRE_THAT(phs->sick_leave_fraction, WithinAbs(0.0f, 1e-7f));
    REQUIRE_THAT(phs->effective_labour_supply, WithinAbs(5.0f, 1e-4f));
}

TEST_CASE("empty province has zero sick leave fraction", "[healthcare][tier5]") {
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    // No NPCs.
    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile()));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* phs = mod.find_province_health(0);
    REQUIRE(phs != nullptr);
    REQUIRE_THAT(phs->sick_leave_fraction, WithinAbs(0.0f, 1e-7f));
    REQUIRE_THAT(phs->effective_labour_supply, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("NPC health does not go below zero from clamping", "[healthcare][tier5]") {
    // NPC at very low health with no recovery and no treatment.
    HealthcareModule mod;
    WorldState state = make_test_world_state();
    state.provinces.push_back(make_test_province(0));

    NPC npc = make_test_npc(1, 0, 0.0f);
    state.significant_npcs.push_back(npc);

    mod.province_health_states().push_back(
        make_test_province_health(0, make_test_profile(0.0f, 0.0f, 0.0f, 0.0f)));
    // Set health to small positive value that should stay clamped.
    mod.npc_health_records().push_back(make_test_health_record(1, 0.001f));

    DeltaBuffer delta{};
    mod.execute_province(0, state, delta);

    auto* hr = mod.find_npc_health(1);
    REQUIRE(hr != nullptr);
    REQUIRE(hr->health >= 0.0f);
    REQUIRE(hr->health <= 1.0f);
}
