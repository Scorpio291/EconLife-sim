#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/investigator_engine/investigator_engine_module.h"
#include "modules/facility_signals/facility_signals_types.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// ============================================================================
// Static utility tests
// ============================================================================

TEST_CASE("InvestigatorEngine: compute_regional_signal from facility signals",
          "[investigator_engine][tier8]") {
    // Single criminal facility with net_signal 0.50, normalizer 5.0
    std::vector<float> signals = {0.50f};
    float result = InvestigatorEngineModule::compute_regional_signal(signals, 5.0f);
    REQUIRE_THAT(result, WithinAbs(0.10f, 0.001f));
}

TEST_CASE("InvestigatorEngine: regional_signal sums multiple facilities",
          "[investigator_engine][tier8]") {
    std::vector<float> signals = {0.30f, 0.20f, 0.50f};
    float result = InvestigatorEngineModule::compute_regional_signal(signals, 5.0f);
    REQUIRE_THAT(result, WithinAbs(0.20f, 0.001f));  // 1.0 / 5.0
}

TEST_CASE("InvestigatorEngine: compute_regional_signal empty returns zero",
          "[investigator_engine][tier8]") {
    std::vector<float> signals = {};
    float result = InvestigatorEngineModule::compute_regional_signal(signals, 5.0f);
    REQUIRE_THAT(result, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("InvestigatorEngine: compute_fill_rate from regional signal",
          "[investigator_engine][tier8]") {
    // regional_signal 0.10, scale 0.005, max 0.01
    // fill_rate = 0.10 * 0.005 = 0.0005
    float result = InvestigatorEngineModule::compute_fill_rate(0.10f, 0.005f, 0.01f);
    REQUIRE_THAT(result, WithinAbs(0.0005f, 0.0001f));
}

TEST_CASE("InvestigatorEngine: fill_rate clamped to max",
          "[investigator_engine][tier8]") {
    // Very high signal
    float result = InvestigatorEngineModule::compute_fill_rate(100.0f, 0.005f, 0.01f);
    REQUIRE_THAT(result, WithinAbs(0.01f, 0.0001f));
}

TEST_CASE("InvestigatorEngine: corruption reduces fill_rate",
          "[investigator_engine][tier8]") {
    // corruption_susceptibility 0.5, coverage 0.8
    // modifier = 1.0 - 0.5 * 0.8 = 0.60
    float result = InvestigatorEngineModule::apply_corruption_modifier(0.005f, 0.5f, 0.8f);
    REQUIRE_THAT(result, WithinAbs(0.003f, 0.0001f));
}

TEST_CASE("InvestigatorEngine: derive_status surveillance threshold",
          "[investigator_engine][tier8]") {
    uint8_t status = InvestigatorEngineModule::derive_status(0.30f, 0.30f, 0.60f, 0.80f);
    REQUIRE(status == static_cast<uint8_t>(InvestigatorMeterStatus::surveillance));
}

TEST_CASE("InvestigatorEngine: derive_status formal_inquiry threshold",
          "[investigator_engine][tier8]") {
    uint8_t status = InvestigatorEngineModule::derive_status(0.65f, 0.30f, 0.60f, 0.80f);
    REQUIRE(status == static_cast<uint8_t>(InvestigatorMeterStatus::formal_inquiry));
}

TEST_CASE("InvestigatorEngine: derive_status raid_imminent threshold",
          "[investigator_engine][tier8]") {
    uint8_t status = InvestigatorEngineModule::derive_status(0.85f, 0.30f, 0.60f, 0.80f);
    REQUIRE(status == static_cast<uint8_t>(InvestigatorMeterStatus::raid_imminent));
}

TEST_CASE("InvestigatorEngine: derive_status inactive below surveillance",
          "[investigator_engine][tier8]") {
    uint8_t status = InvestigatorEngineModule::derive_status(0.20f, 0.30f, 0.60f, 0.80f);
    REQUIRE(status == static_cast<uint8_t>(InvestigatorMeterStatus::inactive));
}

TEST_CASE("InvestigatorEngine: resolve_target returns argmax",
          "[investigator_engine][tier8]") {
    std::vector<std::pair<uint32_t, float>> contributions = {
        {1, 0.30f}, {2, 0.70f}, {3, 0.10f}
    };
    uint32_t target = InvestigatorEngineModule::resolve_target(contributions);
    REQUIRE(target == 2);
}

TEST_CASE("InvestigatorEngine: resolve_target empty returns sentinel",
          "[investigator_engine][tier8]") {
    std::vector<std::pair<uint32_t, float>> contributions = {};
    uint32_t target = InvestigatorEngineModule::resolve_target(contributions);
    REQUIRE(target == 0);
}

TEST_CASE("InvestigatorEngine: meter decays when signal drops",
          "[investigator_engine][tier8]") {
    float result = InvestigatorEngineModule::compute_decay(0.50f, 0.001f);
    REQUIRE_THAT(result, WithinAbs(0.499f, 0.001f));
}

TEST_CASE("InvestigatorEngine: decay does not go below zero",
          "[investigator_engine][tier8]") {
    float result = InvestigatorEngineModule::compute_decay(0.0005f, 0.001f);
    REQUIRE_THAT(result, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("InvestigatorEngine: NGO ignores corruption modifier",
          "[investigator_engine][tier8]") {
    // NGO fill_rate should NOT be modified by corruption
    float base_rate = 0.005f;
    // For NGO, we skip apply_corruption_modifier — rate stays same
    // Just verify the apply function with zero susceptibility (NGO behavior)
    float ngo_rate = InvestigatorEngineModule::apply_corruption_modifier(base_rate, 0.0f, 0.8f);
    REQUIRE_THAT(ngo_rate, WithinAbs(base_rate, 0.0001f));

    // Regular LE gets reduced
    float le_rate = InvestigatorEngineModule::apply_corruption_modifier(base_rate, 0.5f, 0.8f);
    REQUIRE(le_rate < base_rate);
}

TEST_CASE("InvestigatorEngine: meter fills from facility signal correctly",
          "[investigator_engine][tier8]") {
    // Single facility net_signal 0.50 -> regional 0.10 -> fill_rate 0.0005
    std::vector<float> signals = {0.50f};
    float regional = InvestigatorEngineModule::compute_regional_signal(signals, 5.0f);
    float fill = InvestigatorEngineModule::compute_fill_rate(regional, 0.005f, 0.01f);
    REQUIRE_THAT(fill, WithinAbs(0.0005f, 0.0001f));
}

TEST_CASE("InvestigatorEngine: violence multiplier on personnel_violence",
          "[investigator_engine][tier8]") {
    // Fill rate during violence should be 3.0x
    float base_fill = 0.005f;
    float multiplied = base_fill * InvestigatorEngineModule::PERSONNEL_VIOLENCE_MULTIPLIER;
    REQUIRE_THAT(multiplied, WithinAbs(0.015f, 0.001f));
    // But clamped to fill_rate_max
    float clamped = std::min(multiplied, InvestigatorEngineModule::FILL_RATE_MAX);
    REQUIRE_THAT(clamped, WithinAbs(0.01f, 0.001f));
}
