#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "modules/addiction/addiction_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("Addiction: casual to regular transition", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::casual;
    state.consecutive_use_ticks = 30;
    state.craving = 0.35f;
    state.tolerance = 0.20f;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::regular);
}

TEST_CASE("Addiction: casual stays casual below threshold", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::casual;
    state.consecutive_use_ticks = 20;
    state.craving = 0.25f;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::casual);
}

TEST_CASE("Addiction: regular to dependent transition", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::regular;
    state.consecutive_use_ticks = 90;
    state.craving = 0.75f;
    state.tolerance = 0.35f;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::dependent);
}

TEST_CASE("Addiction: dependent to active transition", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::dependent;
    state.craving = 0.75f;
    state.consecutive_use_ticks = 60;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::active);
}

TEST_CASE("Addiction: active to recovery with clean ticks", "[addiction][tier10]") {
    AddictionState state;
    state.stage = AddictionStage::active;
    state.clean_ticks = 14;

    auto next = AddictionModule::compute_next_stage(state);
    REQUIRE(next == AddictionStage::recovery);
}

TEST_CASE("Addiction: craving increment per stage", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::casual),
                 WithinAbs(0.01f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::regular),
                 WithinAbs(0.02f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::dependent),
                 WithinAbs(0.03f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::active),
                 WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::recovery),
                 WithinAbs(-0.003f, 0.001f));
    REQUIRE_THAT(AddictionModule::craving_increment(AddictionStage::none), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Addiction: withdrawal damage at dependent stage", "[addiction][tier10]") {
    float dmg = AddictionModule::compute_withdrawal_damage(AddictionStage::dependent, 5);
    REQUIRE_THAT(dmg, WithinAbs(0.005f, 0.001f));
}

TEST_CASE("Addiction: no withdrawal below dependent", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::compute_withdrawal_damage(AddictionStage::casual, 10),
                 WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(AddictionModule::compute_withdrawal_damage(AddictionStage::regular, 10),
                 WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Addiction: no withdrawal with zero supply gap", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::compute_withdrawal_damage(AddictionStage::dependent, 0),
                 WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Addiction: work efficiency per stage", "[addiction][tier10]") {
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::none),
                 WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::casual),
                 WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::dependent),
                 WithinAbs(0.70f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::active),
                 WithinAbs(0.50f, 0.01f));
    REQUIRE_THAT(AddictionModule::compute_work_efficiency(AddictionStage::terminal),
                 WithinAbs(0.20f, 0.01f));
}

TEST_CASE("Addiction: rate delta from stage transition", "[addiction][tier10]") {
    // Entering counted stage
    float enter = AddictionModule::compute_addiction_rate_delta(AddictionStage::regular,
                                                                AddictionStage::dependent);
    REQUIRE_THAT(enter, WithinAbs(0.001f, 0.0001f));

    // Leaving counted stage (recovery)
    float leave = AddictionModule::compute_addiction_rate_delta(AddictionStage::active,
                                                                AddictionStage::recovery);
    REQUIRE_THAT(leave, WithinAbs(-0.001f, 0.0001f));

    // No change within counted stages
    float same = AddictionModule::compute_addiction_rate_delta(AddictionStage::dependent,
                                                               AddictionStage::active);
    REQUIRE_THAT(same, WithinAbs(0.0f, 0.0001f));
}

TEST_CASE("Addiction: recovery complete check", "[addiction][tier10]") {
    REQUIRE(AddictionModule::is_recovery_complete(365, 0.04f) == true);
    REQUIRE(AddictionModule::is_recovery_complete(365, 0.06f) == false);
    REQUIRE(AddictionModule::is_recovery_complete(300, 0.04f) == false);
}

TEST_CASE("Addiction: config defaults match spec", "[addiction][tier10]") {
    constexpr AddictionConfig cfg{};
    REQUIRE_THAT(cfg.tolerance_per_use_casual, WithinAbs(0.05f, 0.001f));
    REQUIRE(cfg.regular_use_threshold == 30);
    REQUIRE(cfg.dependency_threshold == 90);
    REQUIRE_THAT(cfg.withdrawal_health_hit, WithinAbs(0.005f, 0.001f));
    REQUIRE_THAT(cfg.dependent_work_efficiency, WithinAbs(0.70f, 0.01f));
    REQUIRE(cfg.full_recovery_ticks == 365);
    REQUIRE_THAT(cfg.terminal_health_threshold, WithinAbs(0.15f, 0.01f));
    REQUIRE(cfg.terminal_persistence_ticks == 90);
}
