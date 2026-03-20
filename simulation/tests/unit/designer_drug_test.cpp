#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "modules/designer_drug/designer_drug_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("DesignerDrug: detection triggered at threshold", "[designer_drug][tier9]") {
    REQUIRE(DesignerDrugModule::is_detection_triggered(2.5f, 2.5f) == true);
    REQUIRE(DesignerDrugModule::is_detection_triggered(2.4f, 2.5f) == false);
    REQUIRE(DesignerDrugModule::is_detection_triggered(3.0f, 2.5f) == true);
}

TEST_CASE("DesignerDrug: review duration scales with political delay", "[designer_drug][tier9]") {
    REQUIRE(DesignerDrugModule::compute_review_duration(180, 1.0f) == 180);
    REQUIRE(DesignerDrugModule::compute_review_duration(180, 1.5f) == 270);
    REQUIRE(DesignerDrugModule::compute_review_duration(180, 2.0f) == 360);
}

TEST_CASE("DesignerDrug: market margin unscheduled is 2.5x", "[designer_drug][tier9]") {
    float margin = DesignerDrugModule::compute_market_margin(SchedulingStage::unscheduled, false);
    REQUIRE_THAT(margin, WithinAbs(2.5f, 0.01f));
}

TEST_CASE("DesignerDrug: market margin review_initiated still 2.5x", "[designer_drug][tier9]") {
    float margin = DesignerDrugModule::compute_market_margin(SchedulingStage::review_initiated, false);
    REQUIRE_THAT(margin, WithinAbs(2.5f, 0.01f));
}

TEST_CASE("DesignerDrug: scheduled with successor is 1.0x", "[designer_drug][tier9]") {
    float margin = DesignerDrugModule::compute_market_margin(SchedulingStage::scheduled, true);
    REQUIRE_THAT(margin, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("DesignerDrug: scheduled without successor is 0.80x", "[designer_drug][tier9]") {
    float margin = DesignerDrugModule::compute_market_margin(SchedulingStage::scheduled, false);
    REQUIRE_THAT(margin, WithinAbs(0.80f, 0.01f));
}

TEST_CASE("DesignerDrug: monthly detection check", "[designer_drug][tier9]") {
    REQUIRE(DesignerDrugModule::should_check_detection(30, 30) == true);
    REQUIRE(DesignerDrugModule::should_check_detection(60, 30) == true);
    REQUIRE(DesignerDrugModule::should_check_detection(15, 30) == false);
    REQUIRE(DesignerDrugModule::should_check_detection(0, 30) == false);
}

TEST_CASE("DesignerDrug: evidence weight accumulation", "[designer_drug][tier9]") {
    float total = DesignerDrugModule::accumulate_evidence_weight(1.0f, 0.5f);
    REQUIRE_THAT(total, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("DesignerDrug: negative evidence weight ignored", "[designer_drug][tier9]") {
    float total = DesignerDrugModule::accumulate_evidence_weight(1.0f, -0.5f);
    REQUIRE_THAT(total, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("DesignerDrug: constants match spec", "[designer_drug][tier9]") {
    REQUIRE_THAT(DesignerDrugModule::DETECTION_THRESHOLD, WithinAbs(2.5f, 0.01f));
    REQUIRE(DesignerDrugModule::BASE_REVIEW_DURATION == 180);
    REQUIRE_THAT(DesignerDrugModule::UNSCHEDULED_MARGIN, WithinAbs(2.5f, 0.01f));
    REQUIRE_THAT(DesignerDrugModule::SCHEDULED_MARGIN, WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(DesignerDrugModule::NO_SUCCESSOR_MARGIN, WithinAbs(0.80f, 0.01f));
}

TEST_CASE("DesignerDrug: scheduling stage enum values", "[designer_drug][tier9]") {
    REQUIRE(static_cast<uint8_t>(SchedulingStage::unscheduled) == 0);
    REQUIRE(static_cast<uint8_t>(SchedulingStage::review_initiated) == 1);
    REQUIRE(static_cast<uint8_t>(SchedulingStage::scheduled) == 2);
}
