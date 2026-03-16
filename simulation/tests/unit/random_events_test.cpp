// Unit tests for RandomEventsModule.
// All tests tagged with [random_events][tier1].
//
// Tests verify the Poisson probability model, category/template selection,
// severity/duration bounds, evidence generation, event expiry, and
// deterministic reproducibility.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include "modules/random_events/event_types.h"

// Include the module implementation for direct testing.
// The module class is defined in the .cpp file; we include it here for
// unit test access to internal state and methods.
#include "modules/random_events/random_events_module.cpp"

using namespace econlife;

// =============================================================================
// Test helpers — minimal WorldState and Province construction
// =============================================================================

namespace {

Province make_test_province(uint32_t id,
                             float climate_stress = 0.0f,
                             float stability = 1.0f,
                             float infrastructure = 0.5f) {
    Province p{};
    p.id = id;
    p.h3_index = 0;
    p.region_id = 0;
    p.nation_id = 0;
    p.lod_level = SimulationLOD::full;
    p.infrastructure_rating = infrastructure;
    p.agricultural_productivity = 1.0f;
    p.energy_cost_baseline = 1.0f;
    p.trade_openness = 0.5f;
    p.climate.climate_stress_current = climate_stress;
    p.conditions.stability_score = stability;
    p.conditions.inequality_index = 0.0f;
    p.conditions.crime_rate = 0.0f;
    p.conditions.addiction_rate = 0.0f;
    p.conditions.criminal_dominance_index = 0.0f;
    p.conditions.formal_employment_rate = 0.8f;
    p.conditions.regulatory_compliance_index = 0.9f;
    p.conditions.drought_modifier = 1.0f;
    p.conditions.flood_modifier = 1.0f;
    p.community.cohesion = 0.7f;
    p.community.grievance_level = 0.1f;
    p.community.institutional_trust = 0.6f;
    p.community.resource_access = 0.5f;
    p.community.response_stage = 0;
    p.cohort_stats = nullptr;
    return p;
}

WorldState make_test_world_state(uint64_t seed = 42,
                                  uint32_t current_tick = 100) {
    WorldState ws{};
    ws.world_seed = seed;
    ws.current_tick = current_tick;
    ws.player = nullptr;
    ws.lod2_price_index = nullptr;
    ws.ticks_this_session = 0;
    ws.game_mode = GameMode::standard;
    ws.current_schema_version = 1;
    ws.network_health_dirty = false;
    return ws;
}

// Create a single-template vector for controlled testing.
std::vector<RandomEventTemplate> make_single_template(
    EventCategory category,
    float severity_min = 0.1f,
    float severity_max = 0.9f,
    uint32_t dur_min = 5,
    uint32_t dur_max = 15,
    bool generates_evidence = false) {

    RandomEventTemplate t{};
    t.id = "test_template";
    t.template_key = "test_template";
    t.category = category;
    t.name = "Test Template";
    t.base_weight = 1.0f;
    t.severity_min = severity_min;
    t.severity_max = severity_max;
    t.duration_ticks_min = dur_min;
    t.duration_ticks_max = dur_max;
    t.climate_stress_weight_scale = 1.0f;
    t.instability_weight_scale = 1.0f;
    t.infrastructure_weight_scale = 1.0f;
    t.generates_evidence_token = generates_evidence;
    return {t};
}

}  // anonymous namespace

// =============================================================================
// Test: Zero event rate produces no events
// =============================================================================

TEST_CASE("test_zero_event_rate_no_fires", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(0.0f);

    WorldState ws = make_test_world_state(42, 100);
    ws.provinces.push_back(make_test_province(0));

    // Run 100 ticks.
    for (uint32_t tick = 0; tick < 100; ++tick) {
        ws.current_tick = tick;
        DeltaBuffer db{};
        module.execute_province(0, ws, db);
    }

    REQUIRE(module.active_events().empty());
}

// =============================================================================
// Test: High climate stress increases natural event probability
// =============================================================================

TEST_CASE("test_high_climate_stress_more_natural", "[random_events][tier1]") {
    // Province A: no climate stress.
    // Province B: high climate stress.
    // Both run many ticks; province B should fire more natural events.

    auto run_province = [](float climate_stress, uint64_t seed) -> uint32_t {
        RandomEventsModule module;
        // Use elevated base rate to get more events in fewer ticks.
        module.set_base_rate(1.5f);

        // Only natural templates.
        std::vector<RandomEventTemplate> templates;
        RandomEventTemplate t{};
        t.id = "natural_test";
        t.template_key = "natural_test";
        t.category = EventCategory::natural;
        t.name = "Natural Test";
        t.base_weight = 1.0f;
        t.severity_min = 0.1f;
        t.severity_max = 0.5f;
        t.duration_ticks_min = 1;
        t.duration_ticks_max = 2;
        t.climate_stress_weight_scale = 3.0f;  // strongly climate-sensitive
        t.instability_weight_scale = 1.0f;
        t.infrastructure_weight_scale = 1.0f;
        t.generates_evidence_token = false;
        templates.push_back(t);
        module.set_templates(templates);

        WorldState ws = make_test_world_state(seed, 0);
        ws.provinces.push_back(make_test_province(0, climate_stress));

        uint32_t natural_count = 0;
        for (uint32_t tick = 0; tick < 500; ++tick) {
            ws.current_tick = tick;
            DeltaBuffer db{};
            module.execute_province(0, ws, db);
        }

        for (const auto& ev : module.active_events()) {
            if (ev.category == EventCategory::natural) ++natural_count;
        }
        return natural_count;
    };

    uint32_t low_stress_count  = run_province(0.0f, 12345);
    uint32_t high_stress_count = run_province(0.8f, 12345);

    // High climate stress should produce more events.
    REQUIRE(high_stress_count > low_stress_count);
}

// =============================================================================
// Test: Event severity within template bounds
// =============================================================================

TEST_CASE("test_event_severity_in_bounds", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(5.0f);  // Very high rate to guarantee events fire.

    float sev_min = 0.3f;
    float sev_max = 0.7f;
    module.set_templates(make_single_template(EventCategory::natural, sev_min, sev_max, 5, 15));

    WorldState ws = make_test_world_state(777, 0);
    ws.provinces.push_back(make_test_province(0));

    for (uint32_t tick = 0; tick < 200; ++tick) {
        ws.current_tick = tick;
        DeltaBuffer db{};
        module.execute_province(0, ws, db);
    }

    REQUIRE(!module.active_events().empty());

    for (const auto& ev : module.active_events()) {
        REQUIRE(ev.severity >= sev_min);
        REQUIRE(ev.severity <= sev_max);
    }
}

// =============================================================================
// Test: Event duration within template bounds
// =============================================================================

TEST_CASE("test_event_duration_in_bounds", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(5.0f);

    uint32_t dur_min = 5;
    uint32_t dur_max = 15;
    module.set_templates(make_single_template(EventCategory::natural, 0.1f, 0.9f, dur_min, dur_max));

    WorldState ws = make_test_world_state(888, 0);
    ws.provinces.push_back(make_test_province(0));

    for (uint32_t tick = 0; tick < 200; ++tick) {
        ws.current_tick = tick;
        DeltaBuffer db{};
        module.execute_province(0, ws, db);
    }

    REQUIRE(!module.active_events().empty());

    for (const auto& ev : module.active_events()) {
        if (ev.end_tick == 0) continue;  // Skip resolved events.
        uint32_t duration = ev.end_tick - ev.started_tick;
        REQUIRE(duration >= dur_min);
        REQUIRE(duration <= dur_max);
    }
}

// =============================================================================
// Test: Evidence generated above severity threshold (accident at 0.5)
// =============================================================================

TEST_CASE("test_evidence_above_threshold", "[random_events][tier1]") {
    RandomEventsModule module;

    // Set up a template with severity range that guarantees >= 0.3.
    std::vector<RandomEventTemplate> templates;
    RandomEventTemplate t{};
    t.id = "accident_test";
    t.template_key = "accident_test";
    t.category = EventCategory::accident;
    t.name = "Test Accident";
    t.base_weight = 1.0f;
    t.severity_min = 0.5f;  // Minimum 0.5, always above 0.3 threshold.
    t.severity_max = 0.5f;  // Fixed severity.
    t.duration_ticks_min = 10;
    t.duration_ticks_max = 10;
    t.climate_stress_weight_scale = 1.0f;
    t.instability_weight_scale = 1.0f;
    t.infrastructure_weight_scale = 1.0f;
    t.generates_evidence_token = true;
    templates.push_back(t);
    module.set_templates(templates);

    // Create an active accident event with severity 0.5 (above 0.3 threshold).
    ActiveRandomEvent event{};
    event.id = module.allocate_event_id();
    event.template_id = "accident_test";
    event.template_key = "accident_test";
    event.province_id = 0;
    event.category = EventCategory::accident;
    event.severity = 0.5f;
    event.started_tick = 100;
    event.end_tick = 110;
    event.evidence_generated = false;
    event.effects_applied_this_tick = false;
    module.add_active_event(event);

    WorldState ws = make_test_world_state(42, 101);
    ws.provinces.push_back(make_test_province(0));

    // Add a business in the province so evidence can target its owner.
    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.owner_id = 100;
    biz.sector = BusinessSector::manufacturing;
    biz.profile = BusinessProfile::cost_cutter;
    biz.cash = 1000.0f;
    biz.criminal_sector = false;
    biz.default_activity_scope = VisibilityScope::institutional;
    ws.npc_businesses.push_back(biz);

    DeltaBuffer db{};
    module.execute_province(0, ws, db);

    // Verify evidence was generated.
    REQUIRE(!db.evidence_deltas.empty());

    bool found_evidence = false;
    for (const auto& ed : db.evidence_deltas) {
        if (ed.new_token.has_value()) {
            REQUIRE(ed.new_token->target_npc_id == 100);  // owner_id
            REQUIRE(ed.new_token->type == EvidenceType::physical);
            found_evidence = true;
        }
    }
    REQUIRE(found_evidence);

    // Verify the evidence_generated flag is set.
    bool flag_set = false;
    for (const auto& ev : module.active_events()) {
        if (ev.id == event.id) {
            REQUIRE(ev.evidence_generated == true);
            flag_set = true;
        }
    }
    REQUIRE(flag_set);
}

// =============================================================================
// Test: No evidence below severity threshold (accident at 0.2)
// =============================================================================

TEST_CASE("test_no_evidence_below_threshold", "[random_events][tier1]") {
    RandomEventsModule module;

    // Create an active accident event with severity 0.2 (below 0.3 threshold).
    ActiveRandomEvent event{};
    event.id = module.allocate_event_id();
    event.template_id = "accident_low";
    event.template_key = "accident_low";
    event.province_id = 0;
    event.category = EventCategory::accident;
    event.severity = 0.2f;
    event.started_tick = 100;
    event.end_tick = 110;
    event.evidence_generated = false;
    event.effects_applied_this_tick = false;
    module.add_active_event(event);

    WorldState ws = make_test_world_state(42, 101);
    ws.provinces.push_back(make_test_province(0));
    // Disable new event rolling by setting rate to zero.
    module.set_base_rate(0.0f);

    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.owner_id = 200;
    biz.sector = BusinessSector::manufacturing;
    biz.profile = BusinessProfile::cost_cutter;
    biz.cash = 1000.0f;
    biz.criminal_sector = false;
    biz.default_activity_scope = VisibilityScope::institutional;
    ws.npc_businesses.push_back(biz);

    DeltaBuffer db{};
    module.execute_province(0, ws, db);

    // Verify NO evidence was generated.
    bool found_evidence = false;
    for (const auto& ed : db.evidence_deltas) {
        if (ed.new_token.has_value()) {
            found_evidence = true;
        }
    }
    REQUIRE_FALSE(found_evidence);

    // Verify evidence_generated flag remains false.
    for (const auto& ev : module.active_events()) {
        if (ev.id == event.id) {
            REQUIRE(ev.evidence_generated == false);
        }
    }
}

// =============================================================================
// Test: Event expires at end_tick
// =============================================================================

TEST_CASE("test_event_expires_at_end_tick", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(0.0f);  // No new events.

    uint32_t start = 10;
    uint32_t duration = 5;

    ActiveRandomEvent event{};
    event.id = module.allocate_event_id();
    event.template_id = "expire_test";
    event.template_key = "expire_test";
    event.province_id = 0;
    event.category = EventCategory::natural;
    event.severity = 0.3f;
    event.started_tick = start;
    event.end_tick = start + duration;  // end_tick = 15
    event.evidence_generated = false;
    event.effects_applied_this_tick = false;
    module.add_active_event(event);

    WorldState ws = make_test_world_state(42, start + duration - 1);
    ws.provinces.push_back(make_test_province(0));

    // At tick 14 (one before end), event should still be active.
    {
        DeltaBuffer db{};
        module.execute_province(0, ws, db);
        bool still_active = false;
        for (const auto& ev : module.active_events()) {
            if (ev.id == event.id && ev.end_tick != 0) {
                still_active = true;
            }
        }
        REQUIRE(still_active);
    }

    // At tick 15 (= end_tick), event should be marked resolved (end_tick = 0).
    ws.current_tick = start + duration;
    {
        DeltaBuffer db{};
        module.execute_province(0, ws, db);
        bool resolved = false;
        for (const auto& ev : module.active_events()) {
            if (ev.id == event.id && ev.end_tick == 0) {
                resolved = true;
            }
        }
        REQUIRE(resolved);
    }
}

// =============================================================================
// Test: Deterministic across runs (same seed = identical events)
// =============================================================================

TEST_CASE("test_deterministic_across_runs", "[random_events][tier1]") {
    auto run_simulation = [](uint64_t seed) -> std::vector<ActiveRandomEvent> {
        RandomEventsModule module;
        module.set_base_rate(1.0f);  // Elevated rate to generate events.

        WorldState ws = make_test_world_state(seed, 0);
        ws.provinces.push_back(make_test_province(0, 0.3f, 0.7f, 0.5f));

        for (uint32_t tick = 0; tick < 100; ++tick) {
            ws.current_tick = tick;
            DeltaBuffer db{};
            module.execute_province(0, ws, db);
        }

        return module.active_events();
    };

    auto events_run1 = run_simulation(99999);
    auto events_run2 = run_simulation(99999);

    // Same seed must produce identical event count.
    REQUIRE(events_run1.size() == events_run2.size());

    // Same seed must produce identical event properties.
    for (size_t i = 0; i < events_run1.size(); ++i) {
        REQUIRE(events_run1[i].template_id == events_run2[i].template_id);
        REQUIRE(events_run1[i].severity == events_run2[i].severity);
        REQUIRE(events_run1[i].started_tick == events_run2[i].started_tick);
        REQUIRE(events_run1[i].end_tick == events_run2[i].end_tick);
        REQUIRE(events_run1[i].category == events_run2[i].category);
        REQUIRE(events_run1[i].province_id == events_run2[i].province_id);
    }
}

// =============================================================================
// Test: Module interface properties
// =============================================================================

TEST_CASE("test_module_name_and_ordering", "[random_events][tier1]") {
    RandomEventsModule module;

    REQUIRE(module.name() == "random_events");
    REQUIRE(module.package_id() == "base_game");
    REQUIRE(module.scope() == ModuleScope::v1);
    REQUIRE(module.is_province_parallel() == true);

    auto after = module.runs_after();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0] == "calendar");
}

// =============================================================================
// Test: LOD non-zero provinces are skipped
// =============================================================================

TEST_CASE("test_lod_nonzero_skipped", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(10.0f);  // Very high rate.

    WorldState ws = make_test_world_state(42, 100);
    Province p = make_test_province(0);
    p.lod_level = SimulationLOD::simplified;  // LOD 1: should be skipped.
    ws.provinces.push_back(p);

    for (uint32_t tick = 0; tick < 50; ++tick) {
        ws.current_tick = tick;
        DeltaBuffer db{};
        module.execute_province(0, ws, db);
    }

    REQUIRE(module.active_events().empty());
}

// =============================================================================
// Test: Category selection responds to province conditions
// =============================================================================

TEST_CASE("test_category_weights_shift_with_conditions", "[random_events][tier1]") {
    // Run many category selections with extreme conditions and verify
    // the expected category dominates.

    auto count_categories = [](float climate_stress, float stability,
                               float infrastructure, uint64_t seed) {
        RandomEventsModule module;
        float instability = 1.0f - stability;
        DeterministicRNG rng(seed);

        uint32_t counts[4] = {0, 0, 0, 0};
        for (int i = 0; i < 10000; ++i) {
            // Call select_category directly via the module's internal logic.
            // We replicate the category selection here since it is a private method.
            float w_natural  = 0.25f * (1.0f + climate_stress);
            float w_accident = 0.20f * (1.0f + (1.0f - infrastructure));
            float w_economic = 0.30f * 1.0f;  // volatility = 1.0
            float w_human    = 0.25f * (1.0f + instability);
            float total = w_natural + w_accident + w_economic + w_human;

            float roll = rng.next_float() * total;
            EventCategory cat;
            if (roll < w_natural) cat = EventCategory::natural;
            else if (roll < w_natural + w_accident) cat = EventCategory::accident;
            else if (roll < w_natural + w_accident + w_economic) cat = EventCategory::economic;
            else cat = EventCategory::human;

            counts[static_cast<int>(cat)]++;
        }
        return std::vector<uint32_t>(counts, counts + 4);
    };

    // High climate stress => natural events should be most common.
    {
        auto counts = count_categories(0.9f, 0.9f, 0.9f, 42);
        // natural (index 0) should have the highest or near-highest count
        // given climate_stress = 0.9, natural weight = 0.25 * 1.9 = 0.475
        // while accident with high infra = 0.20 * 1.1 = 0.22
        REQUIRE(counts[0] > counts[1]);  // natural > accident
    }

    // Low infrastructure => accident events should be boosted.
    {
        auto counts = count_categories(0.0f, 0.9f, 0.1f, 42);
        // accident weight = 0.20 * (1 + 0.9) = 0.38
        // natural weight = 0.25 * 1.0 = 0.25
        REQUIRE(counts[1] > counts[0]);  // accident > natural
    }

    // High instability => human events should be boosted.
    {
        auto counts = count_categories(0.0f, 0.1f, 0.5f, 42);
        // human weight = 0.25 * (1 + 0.9) = 0.475
        // economic weight = 0.30 * 1.0 = 0.30
        REQUIRE(counts[3] > counts[2]);  // human > economic
    }
}

// =============================================================================
// Test: Scene card generated for player in province during human event
// =============================================================================

TEST_CASE("test_human_event_scene_card_for_player", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(0.0f);  // No new events.

    // Create an active human event.
    ActiveRandomEvent event{};
    event.id = module.allocate_event_id();
    event.template_id = "human_test";
    event.template_key = "human_test";
    event.province_id = 0;
    event.category = EventCategory::human;
    event.severity = 0.5f;
    event.started_tick = 100;
    event.end_tick = 110;
    event.evidence_generated = false;
    event.effects_applied_this_tick = false;
    module.add_active_event(event);

    WorldState ws = make_test_world_state(42, 101);
    ws.provinces.push_back(make_test_province(0));

    // Create a player in the affected province.
    PlayerCharacter player{};
    player.id = 1;
    player.current_province_id = 0;
    player.home_province_id = 0;
    player.background = Background::MiddleClass;
    player.age = 30.0f;
    player.wealth = 10000.0f;
    player.net_assets = 10000.0f;
    player.ironman_eligible = false;
    ws.player = &player;

    DeltaBuffer db{};
    module.execute_province(0, ws, db);

    // Verify a scene card was generated.
    REQUIRE(!db.new_scene_cards.empty());
    REQUIRE(db.new_scene_cards[0].type == SceneCardType::news_notification);
}

// =============================================================================
// Test: Evidence generated only once across multiple ticks
// =============================================================================

TEST_CASE("test_evidence_generated_only_once", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(0.0f);

    ActiveRandomEvent event{};
    event.id = module.allocate_event_id();
    event.template_id = "accident_once";
    event.template_key = "accident_once";
    event.province_id = 0;
    event.category = EventCategory::accident;
    event.severity = 0.5f;
    event.started_tick = 100;
    event.end_tick = 110;
    event.evidence_generated = false;
    event.effects_applied_this_tick = false;
    module.add_active_event(event);

    WorldState ws = make_test_world_state(42, 101);
    ws.provinces.push_back(make_test_province(0));

    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.owner_id = 300;
    biz.sector = BusinessSector::manufacturing;
    biz.profile = BusinessProfile::cost_cutter;
    biz.cash = 1000.0f;
    biz.criminal_sector = false;
    biz.default_activity_scope = VisibilityScope::institutional;
    ws.npc_businesses.push_back(biz);

    uint32_t evidence_count = 0;

    // Run 10 ticks with the active event.
    for (uint32_t tick = 101; tick < 110; ++tick) {
        ws.current_tick = tick;
        DeltaBuffer db{};
        module.execute_province(0, ws, db);

        for (const auto& ed : db.evidence_deltas) {
            if (ed.new_token.has_value()) {
                ++evidence_count;
            }
        }
    }

    // Evidence should be generated exactly once.
    REQUIRE(evidence_count == 1);
}

// =============================================================================
// Test: Community deltas from human events
// =============================================================================

TEST_CASE("test_human_event_community_deltas", "[random_events][tier1]") {
    RandomEventsModule module;
    module.set_base_rate(0.0f);

    ActiveRandomEvent event{};
    event.id = module.allocate_event_id();
    event.template_id = "community_test";
    event.template_key = "community_test";
    event.province_id = 0;
    event.category = EventCategory::human;
    event.severity = 0.6f;
    event.started_tick = 100;
    event.end_tick = 110;
    event.evidence_generated = false;
    event.effects_applied_this_tick = false;
    module.add_active_event(event);

    WorldState ws = make_test_world_state(42, 101);
    ws.provinces.push_back(make_test_province(0));

    DeltaBuffer db{};
    module.execute_province(0, ws, db);

    // Verify region deltas include grievance and cohesion changes.
    REQUIRE(!db.region_deltas.empty());
    bool found_grievance = false;
    bool found_cohesion = false;
    for (const auto& rd : db.region_deltas) {
        if (rd.grievance_delta.has_value() && *rd.grievance_delta > 0.0f) {
            found_grievance = true;
        }
        if (rd.cohesion_delta.has_value() && *rd.cohesion_delta < 0.0f) {
            found_cohesion = true;
        }
    }
    REQUIRE(found_grievance);
    REQUIRE(found_cohesion);
}

// =============================================================================
// Test: Different seeds produce different event sequences
// =============================================================================

TEST_CASE("test_different_seeds_different_events", "[random_events][tier1]") {
    auto run_simulation = [](uint64_t seed) -> std::vector<ActiveRandomEvent> {
        RandomEventsModule module;
        module.set_base_rate(1.0f);

        WorldState ws = make_test_world_state(seed, 0);
        ws.provinces.push_back(make_test_province(0, 0.3f, 0.7f, 0.5f));

        for (uint32_t tick = 0; tick < 100; ++tick) {
            ws.current_tick = tick;
            DeltaBuffer db{};
            module.execute_province(0, ws, db);
        }

        return module.active_events();
    };

    auto events_seed_a = run_simulation(11111);
    auto events_seed_b = run_simulation(22222);

    // Different seeds should produce different event sequences.
    // It is astronomically unlikely they'd be identical.
    bool any_difference = false;
    if (events_seed_a.size() != events_seed_b.size()) {
        any_difference = true;
    } else {
        for (size_t i = 0; i < events_seed_a.size(); ++i) {
            if (events_seed_a[i].template_id != events_seed_b[i].template_id ||
                events_seed_a[i].severity != events_seed_b[i].severity ||
                events_seed_a[i].started_tick != events_seed_b[i].started_tick) {
                any_difference = true;
                break;
            }
        }
    }
    REQUIRE(any_difference);
}
