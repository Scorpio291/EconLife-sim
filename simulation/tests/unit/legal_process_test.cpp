#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/legal_process/legal_process_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("LegalProcess: conviction probability formula", "[legal_process][tier9]") {
    // evidence 0.8, defense 0.5, bias 1.0, witness 1.0
    // 0.8 * (1 - 0.5*0.4) * 1.0 * 1.0 = 0.8 * 0.8 = 0.64
    float prob = LegalProcessModule::compute_conviction_probability(0.8f, 0.5f, 1.0f, 1.0f);
    REQUIRE_THAT(prob, WithinAbs(0.64f, 0.01f));
}

TEST_CASE("LegalProcess: perfect defense reduces conviction", "[legal_process][tier9]") {
    float prob = LegalProcessModule::compute_conviction_probability(0.8f, 1.0f, 1.0f, 1.0f);
    // 0.8 * (1 - 0.4) = 0.48
    REQUIRE_THAT(prob, WithinAbs(0.48f, 0.01f));
}

TEST_CASE("LegalProcess: zero evidence gives zero probability", "[legal_process][tier9]") {
    float prob = LegalProcessModule::compute_conviction_probability(0.0f, 0.5f, 1.0f, 1.0f);
    REQUIRE_THAT(prob, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("LegalProcess: conviction probability clamped to [0,1]", "[legal_process][tier9]") {
    float prob = LegalProcessModule::compute_conviction_probability(1.5f, 0.0f, 1.0f, 1.0f);
    REQUIRE(prob <= 1.0f);
}

TEST_CASE("LegalProcess: sentence scales with severity", "[legal_process][tier9]") {
    REQUIRE(LegalProcessModule::compute_sentence_ticks(CaseSeverity::minor, 365) == 365);
    REQUIRE(LegalProcessModule::compute_sentence_ticks(CaseSeverity::moderate, 365) == 730);
    REQUIRE(LegalProcessModule::compute_sentence_ticks(CaseSeverity::capital, 365) == 2190);
}

TEST_CASE("LegalProcess: double jeopardy cooldown", "[legal_process][tier9]") {
    REQUIRE(LegalProcessModule::is_double_jeopardy_active(100, 200) == true);
    REQUIRE(LegalProcessModule::is_double_jeopardy_active(200, 200) == false);
    REQUIRE(LegalProcessModule::is_double_jeopardy_active(300, 200) == false);
}

TEST_CASE("LegalProcess: stage advancement", "[legal_process][tier9]") {
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::investigation, false) ==
            LegalCaseStage::arrested);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::arrested, false) ==
            LegalCaseStage::charged);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::charged, false) ==
            LegalCaseStage::trial);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::trial, true) ==
            LegalCaseStage::convicted);
    REQUIRE(LegalProcessModule::advance_stage(LegalCaseStage::trial, false) ==
            LegalCaseStage::acquitted);
}

TEST_CASE("LegalProcess: evidence weight aggregation", "[legal_process][tier9]") {
    std::vector<float> tokens = {0.3f, 0.2f, 0.4f};
    float weight = LegalProcessModule::compute_evidence_weight(tokens);
    REQUIRE_THAT(weight, WithinAbs(0.9f, 0.01f));
}

TEST_CASE("LegalProcess: evidence weight clamped to 1.0", "[legal_process][tier9]") {
    std::vector<float> tokens = {0.5f, 0.4f, 0.3f};
    float weight = LegalProcessModule::compute_evidence_weight(tokens);
    REQUIRE_THAT(weight, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("LegalProcess: constants match spec", "[legal_process][tier9]") {
    REQUIRE(LegalProcessModule::TICKS_PER_SEVERITY == 365);
    REQUIRE(LegalProcessModule::DOUBLE_JEOPARDY_COOLDOWN == 1825);
    REQUIRE_THAT(LegalProcessModule::DEFENSE_QUALITY_FACTOR, WithinAbs(0.40f, 0.001f));
}
