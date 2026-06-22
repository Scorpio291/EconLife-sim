#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/good_id_hash.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/designer_drug/designer_drug_module.h"

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
    DesignerDrugModule mod;
    float margin = mod.compute_market_margin(SchedulingStage::unscheduled, false);
    REQUIRE_THAT(margin, WithinAbs(2.5f, 0.01f));
}

TEST_CASE("DesignerDrug: market margin review_initiated still 2.5x", "[designer_drug][tier9]") {
    DesignerDrugModule mod;
    float margin = mod.compute_market_margin(SchedulingStage::review_initiated, false);
    REQUIRE_THAT(margin, WithinAbs(2.5f, 0.01f));
}

TEST_CASE("DesignerDrug: scheduled with successor is 1.0x", "[designer_drug][tier9]") {
    DesignerDrugModule mod;
    float margin = mod.compute_market_margin(SchedulingStage::scheduled, true);
    REQUIRE_THAT(margin, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("DesignerDrug: scheduled without successor is 0.80x", "[designer_drug][tier9]") {
    DesignerDrugModule mod;
    float margin = mod.compute_market_margin(SchedulingStage::scheduled, false);
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

TEST_CASE("DesignerDrug: config defaults match spec", "[designer_drug][tier9]") {
    DesignerDrugConfig cfg{};
    REQUIRE_THAT(cfg.detection_threshold, WithinAbs(2.5f, 0.01f));
    REQUIRE(cfg.base_review_duration == 180);
    REQUIRE_THAT(cfg.unscheduled_margin, WithinAbs(2.5f, 0.01f));
    REQUIRE_THAT(cfg.scheduled_margin, WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(cfg.no_successor_margin, WithinAbs(0.80f, 0.01f));
}

TEST_CASE("DesignerDrug: scheduling stage enum values", "[designer_drug][tier9]") {
    REQUIRE(static_cast<uint8_t>(SchedulingStage::unscheduled) == 0);
    REQUIRE(static_cast<uint8_t>(SchedulingStage::review_initiated) == 1);
    REQUIRE(static_cast<uint8_t>(SchedulingStage::scheduled) == 2);
}

// =============================================================================
// Post-scheduling supply behavior (execute path)
// =============================================================================
namespace {
DesignerDrugCompound make_scheduled_compound(uint32_t id, bool has_successor) {
    DesignerDrugCompound c{};
    c.compound_id = id;
    c.creator_actor_id = 1;
    c.stage = SchedulingStage::scheduled;
    c.has_successor = has_successor;
    c.market_margin_multiplier = has_successor ? 1.0f : 0.8f;
    c.province_id = 0;
    c.review_duration = 180;
    return c;
}
}  // namespace

TEST_CASE("DesignerDrug: scheduled-with-successor keeps supplying; without-successor stops",
          "[designer_drug][tier9]") {
    WorldState state{};
    state.current_tick = 5;  // not a monthly tick -> no detection churn
    state.world_seed = 1;

    // Grounded supply requires a real criminal operation (the creator's business)
    // and located precursor stock that production consumes.
    NPCBusiness biz{};
    biz.id = 7;
    biz.owner_id = 1;  // matches make_scheduled_compound's creator_actor_id
    biz.province_id = 0;
    biz.criminal_sector = true;
    biz.revenue_per_tick = 1000.0f;
    state.npc_businesses.push_back(biz);

    RegionalMarket pre{};
    pre.good_id = good_id_hash("drug_precursors");
    pre.province_id = 0;
    pre.supply = 1000.0f;
    state.regional_markets.push_back(pre);
    state.market_index_by_good_province[(static_cast<uint64_t>(pre.good_id) << 32) | 0ull] =
        state.regional_markets.size() - 1;

    DesignerDrugModule with_succ;
    with_succ.compounds_mut().push_back(make_scheduled_compound(10, /*has_successor=*/true));
    DeltaBuffer d1{};
    with_succ.execute(state, d1);
    bool supplied = false;
    for (const auto& md : d1.market_deltas)
        if (md.good_id == 10 && md.supply_delta.has_value() && *md.supply_delta > 0.0f)
            supplied = true;
    REQUIRE(supplied);  // successor line keeps the product on the informal market

    DesignerDrugModule no_succ;
    no_succ.compounds_mut().push_back(make_scheduled_compound(11, /*has_successor=*/false));
    DeltaBuffer d2{};
    no_succ.execute(state, d2);
    bool supplied2 = false;
    for (const auto& md : d2.market_deltas)
        if (md.good_id == 11 && md.supply_delta.has_value() && *md.supply_delta > 0.0f)
            supplied2 = true;
    REQUIRE_FALSE(supplied2);  // no successor -> supply drops to zero
}
