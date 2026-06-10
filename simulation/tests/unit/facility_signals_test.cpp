#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/apply_deltas.h"  // rebuild_npc_indices
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/facility_signals/facility_signals_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Static utility tests
// =============================================================================

TEST_CASE("Signal composite weighted sum", "[facility_signals][tier7]") {
    FacilityTypeSignalWeights w{0.30f, 0.25f, 0.20f, 0.25f};
    float composite = FacilitySignalsModule::compute_signal_composite(0.8f, 0.6f, 0.3f, 0.9f, w);
    // 0.30*0.8 + 0.25*0.6 + 0.20*0.3 + 0.25*0.9 = 0.24 + 0.15 + 0.06 + 0.225 = 0.675
    CHECK_THAT(composite, WithinAbs(0.675f, 0.001f));
}

TEST_CASE("Signal composite default equal weights", "[facility_signals][tier7]") {
    FacilityTypeSignalWeights w{0.25f, 0.25f, 0.25f, 0.25f};
    float composite = FacilitySignalsModule::compute_signal_composite(1.0f, 1.0f, 1.0f, 1.0f, w);
    CHECK_THAT(composite, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("Signal composite clamped to 1.0", "[facility_signals][tier7]") {
    // Weights > 1.0 total shouldn't exceed 1.0 output
    FacilityTypeSignalWeights w{0.5f, 0.5f, 0.5f, 0.5f};
    float composite = FacilitySignalsModule::compute_signal_composite(1.0f, 1.0f, 1.0f, 1.0f, w);
    CHECK(composite == 1.0f);
}

TEST_CASE("Net signal reduced by mitigation", "[facility_signals][tier7]") {
    float net = FacilitySignalsModule::compute_net_signal(0.70f, 0.40f);
    CHECK_THAT(net, WithinAbs(0.30f, 0.001f));
}

TEST_CASE("Net signal cannot go negative", "[facility_signals][tier7]") {
    float net = FacilitySignalsModule::compute_net_signal(0.20f, 0.50f);
    CHECK(net == 0.0f);
}

TEST_CASE("Net signal with zero mitigation", "[facility_signals][tier7]") {
    float net = FacilitySignalsModule::compute_net_signal(0.80f, 0.0f);
    CHECK_THAT(net, WithinAbs(0.80f, 0.001f));
}

// Fill-rate / meter-threshold / corruption helpers moved with meter ownership
// to investigator_engine; see investigator_engine_test.cpp for their coverage.

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("Facility signals execute province computes signals", "[facility_signals][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    // Create a province with karst
    Province prov{};
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = 0;
    prov.has_karst = true;
    state.provinces.push_back(prov);

    // Create a criminal business
    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.criminal_sector = true;
    state.npc_businesses.push_back(biz);

    // Create an LE NPC
    NPC le_npc{};
    le_npc.id = 10;
    le_npc.role = NPCRole::law_enforcement;
    le_npc.current_province_id = 0;
    le_npc.status = NPCStatus::active;
    state.significant_npcs.push_back(le_npc);

    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

    FacilitySignalsModule module;

    // Pre-populate signal for the business
    FacilitySignals sig{};
    sig.facility_id = 1;
    sig.business_id = 1;
    sig.power_consumption_anomaly = 0.8f;
    sig.chemical_waste_signature = 0.6f;
    sig.foot_traffic_visibility = 0.3f;
    sig.olfactory_signature = 0.9f;
    sig.scrutiny_mitigation = 0.1f;
    module.facility_signals().push_back(sig);

    rebuild_npc_indices(state);
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Module should have updated signal composite and net_signal
    const auto& signals = module.facility_signals();
    REQUIRE(signals.size() == 1);

    // With default 0.25 weights: 0.25*(0.8+0.6+0.3+0.9) = 0.25*2.6 = 0.65
    CHECK_THAT(signals[0].base_signal_composite, WithinAbs(0.65f, 0.01f));

    // Net = 0.65 - (0.1 + 0.10 karst) = 0.65 - 0.20 = 0.45
    CHECK_THAT(signals[0].net_signal, WithinAbs(0.45f, 0.01f));

    // This module is a signal source only: it must NOT fill any NPC meter or
    // touch NPC motivation. Meter ownership belongs to investigator_engine.
    for (const auto& d : delta.npc_deltas) {
        CHECK_FALSE(d.motivation_delta.has_value());
    }

    // The net_signal must be published onto the business via BusinessDelta so
    // investigator_engine can read it from WorldState this same tick.
    REQUIRE(delta.business_deltas.size() == 1);
    CHECK(delta.business_deltas[0].business_id == 1u);
    REQUIRE(delta.business_deltas[0].net_signal_update.has_value());
    CHECK_THAT(*delta.business_deltas[0].net_signal_update, WithinAbs(0.45f, 0.01f));
}

TEST_CASE("Facility signals: bootstrap signal derived from violation severity",
          "[facility_signals][tier7]") {
    // With no facility-type signal profile populated (all four physical
    // dimensions zero), the net_signal is derived from the business's
    // regulatory_violation_severity so the detection pipeline stays live.
    WorldState state{};
    state.current_tick = 100;

    Province prov{};
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = 0;
    prov.has_karst = false;  // no concealment bonus -> net_signal == severity
    state.provinces.push_back(prov);

    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.criminal_sector = true;
    biz.regulatory_violation_severity = 0.4f;
    state.npc_businesses.push_back(biz);

    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

    FacilitySignalsModule module;
    module.init_for_tick(state);  // pre-populates a zero-dimension signal entry

    rebuild_npc_indices(state);
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Composite derived from severity; net_signal == severity (no mitigation).
    const auto& signals = module.facility_signals();
    REQUIRE(signals.size() == 1);
    CHECK_THAT(signals[0].net_signal, WithinAbs(0.4f, 0.001f));

    REQUIRE(delta.business_deltas.size() == 1);
    REQUIRE(delta.business_deltas[0].net_signal_update.has_value());
    CHECK_THAT(*delta.business_deltas[0].net_signal_update, WithinAbs(0.4f, 0.001f));
}

TEST_CASE("Regulator NPCs are not polluted by facility signals", "[facility_signals][tier7]") {
    // The civil regulator-scrutiny meter is owned by investigator_engine, not
    // this module. facility_signals must not write any delta to regulator NPCs.
    WorldState state{};
    state.current_tick = 100;

    Province prov{};
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    prov.id = 0;
    prov.has_karst = false;
    state.provinces.push_back(prov);

    // Business with high power anomaly but zero chemical/traffic
    NPCBusiness biz{};
    biz.id = 1;
    biz.province_id = 0;
    biz.criminal_sector = false;
    state.npc_businesses.push_back(biz);

    // Regulator NPC
    NPC reg_npc{};
    reg_npc.id = 20;
    reg_npc.role = NPCRole::regulator;
    reg_npc.current_province_id = 0;
    reg_npc.status = NPCStatus::active;
    state.significant_npcs.push_back(reg_npc);

    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

    FacilitySignalsModule module;

    // High power, zero chemical and traffic
    FacilitySignals sig{};
    sig.facility_id = 1;
    sig.business_id = 1;
    sig.power_consumption_anomaly = 0.9f;
    sig.chemical_waste_signature = 0.0f;
    sig.foot_traffic_visibility = 0.0f;
    sig.olfactory_signature = 0.9f;
    sig.scrutiny_mitigation = 0.0f;
    module.facility_signals().push_back(sig);

    rebuild_npc_indices(state);
    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // No delta should target the regulator NPC, and no motivation pollution.
    for (const auto& d : delta.npc_deltas) {
        CHECK(d.npc_id != 20u);
        CHECK_FALSE(d.motivation_delta.has_value());
    }
}
