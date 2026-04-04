#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

TEST_CASE("LE fill rate from regional signal", "[facility_signals][tier7]") {
    // 3 criminal facilities: net_signals 0.5, 0.3, 0.2 => sum = 1.0
    // regional = 1.0 / 5.0 = 0.2
    // fill_rate = 0.2 * 0.005 = 0.001
    float rate = FacilitySignalsModule::compute_le_fill_rate(0.2f, 0.005f, 0.01f);
    CHECK_THAT(rate, WithinAbs(0.001f, 0.0001f));
}

TEST_CASE("LE fill rate clamped to max", "[facility_signals][tier7]") {
    float rate = FacilitySignalsModule::compute_le_fill_rate(100.0f, 0.005f, 0.01f);
    CHECK_THAT(rate, WithinAbs(0.01f, 0.0001f));
}

TEST_CASE("Investigator meter threshold surveillance", "[facility_signals][tier7]") {
    CHECK(FacilitySignalsModule{}.evaluate_investigator_status(0.29f) ==
          InvestigatorMeterStatus::inactive);
    CHECK(FacilitySignalsModule{}.evaluate_investigator_status(0.30f) ==
          InvestigatorMeterStatus::surveillance);
    CHECK(FacilitySignalsModule{}.evaluate_investigator_status(0.59f) ==
          InvestigatorMeterStatus::surveillance);
}

TEST_CASE("Investigator meter threshold formal inquiry", "[facility_signals][tier7]") {
    CHECK(FacilitySignalsModule{}.evaluate_investigator_status(0.60f) ==
          InvestigatorMeterStatus::formal_inquiry);
    CHECK(FacilitySignalsModule{}.evaluate_investigator_status(0.79f) ==
          InvestigatorMeterStatus::formal_inquiry);
}

TEST_CASE("Investigator meter threshold raid imminent", "[facility_signals][tier7]") {
    CHECK(FacilitySignalsModule{}.evaluate_investigator_status(0.80f) ==
          InvestigatorMeterStatus::raid_imminent);
    CHECK(FacilitySignalsModule{}.evaluate_investigator_status(1.0f) ==
          InvestigatorMeterStatus::raid_imminent);
}

TEST_CASE("Regulator meter thresholds", "[facility_signals][tier7]") {
    CHECK(FacilitySignalsModule{}.evaluate_regulator_status(0.24f) ==
          RegulatorMeterStatus::inactive);
    CHECK(FacilitySignalsModule{}.evaluate_regulator_status(0.25f) ==
          RegulatorMeterStatus::notice_filed);
    CHECK(FacilitySignalsModule{}.evaluate_regulator_status(0.50f) ==
          RegulatorMeterStatus::formal_audit);
    CHECK(FacilitySignalsModule{}.evaluate_regulator_status(0.75f) ==
          RegulatorMeterStatus::enforcement_action);
}

TEST_CASE("Corruption reduces fill rate", "[facility_signals][tier7]") {
    // corruption_susceptibility=0.50, coverage=0.60 => factor = 1 - 0.30 = 0.70
    float adjusted = FacilitySignalsModule::apply_corruption_to_fill_rate(0.005f, 0.50f, 0.60f);
    CHECK_THAT(adjusted, WithinAbs(0.0035f, 0.0001f));
}

TEST_CASE("Full corruption eliminates fill rate", "[facility_signals][tier7]") {
    float adjusted = FacilitySignalsModule::apply_corruption_to_fill_rate(0.005f, 1.0f, 1.0f);
    CHECK_THAT(adjusted, WithinAbs(0.0f, 0.0001f));
}

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("Facility signals execute province computes signals", "[facility_signals][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    // Create a province with karst
    Province prov{};
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

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Module should have updated signal composite and net_signal
    const auto& signals = module.facility_signals();
    REQUIRE(signals.size() == 1);

    // With default 0.25 weights: 0.25*(0.8+0.6+0.3+0.9) = 0.25*2.6 = 0.65
    CHECK_THAT(signals[0].base_signal_composite, WithinAbs(0.65f, 0.01f));

    // Net = 0.65 - (0.1 + 0.10 karst) = 0.65 - 0.20 = 0.45
    CHECK_THAT(signals[0].net_signal, WithinAbs(0.45f, 0.01f));

    // LE NPC should have a delta
    REQUIRE(delta.npc_deltas.size() >= 1);
    CHECK(delta.npc_deltas[0].npc_id == 10);
    // Fill rate should be positive (criminal signal present)
    CHECK(delta.npc_deltas[0].motivation_delta.has_value());
    CHECK(*delta.npc_deltas[0].motivation_delta > 0.0f);
}

TEST_CASE("Regulator reads chemical and traffic only", "[facility_signals][tier7]") {
    WorldState state{};
    state.current_tick = 100;

    Province prov{};
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

    DeltaBuffer delta{};
    module.execute_province(0, state, delta);

    // Regulator should see near-zero signal (chemical + traffic = 0)
    // So regulator meter should decay
    bool found_reg_delta = false;
    for (const auto& d : delta.npc_deltas) {
        if (d.npc_id == 20 && d.motivation_delta.has_value()) {
            found_reg_delta = true;
            CHECK(*d.motivation_delta < 0.0f);  // decay
            break;
        }
    }
    CHECK(found_reg_delta);
}
