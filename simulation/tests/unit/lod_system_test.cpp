#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/economy/economy_types.h"
#include "modules/lod_system/lod_system_module.h"
#include "modules/trade_infrastructure/trade_types.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("LOD: lod1 production calculation", "[lod_system][tier11]") {
    // 100 * 1.2 * (1.0 - 0.1) * 0.8 = 100 * 1.2 * 0.9 * 0.8 = 86.4
    float prod = LodSystemModule::compute_lod1_production(100.0f, 1.2f, 0.1f, 0.8f);
    REQUIRE_THAT(prod, WithinAbs(86.4f, 0.1f));
}

TEST_CASE("LOD: lod1 consumption calculation", "[lod_system][tier11]") {
    // 50 * 1.5 * 1.1 = 82.5
    float cons = LodSystemModule::compute_lod1_consumption(50.0f, 1.5f, 1.1f);
    REQUIRE_THAT(cons, WithinAbs(82.5f, 0.1f));
}

TEST_CASE("LOD: lod2 price modifier scarcity", "[lod_system][tier11]") {
    // consumption=1500, production=1000: ratio=1.5
    float mod = LodSystemModule::compute_lod2_price_modifier(1500.0f, 1000.0f, 1.0f);
    REQUIRE_THAT(mod, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("LOD: lod2 price modifier surplus", "[lod_system][tier11]") {
    // consumption=500, production=1000: ratio=0.5 (at min)
    float mod = LodSystemModule::compute_lod2_price_modifier(500.0f, 1000.0f, 1.0f);
    REQUIRE_THAT(mod, WithinAbs(0.5f, 0.01f));
}

TEST_CASE("LOD: lod2 price modifier clamped max", "[lod_system][tier11]") {
    float mod = LodSystemModule::compute_lod2_price_modifier(5000.0f, 100.0f, 1.0f);
    REQUIRE_THAT(mod, WithinAbs(2.0f, 0.01f));
}

TEST_CASE("LOD: lod2 supply floor prevents div by zero", "[lod_system][tier11]") {
    float mod = LodSystemModule::compute_lod2_price_modifier(100.0f, 0.0f, 1.0f);
    // ratio = 100/1 = 100, clamped to 2.0
    REQUIRE_THAT(mod, WithinAbs(2.0f, 0.01f));
}

TEST_CASE("LOD: smoothing prevents sharp jump", "[lod_system][tier11]") {
    // lerp(1.0, 2.0, 0.30) = 1.0 + 0.30*(2.0-1.0) = 1.30
    float result = LodSystemModule::compute_smoothed_modifier(1.0f, 2.0f, 0.30f);
    REQUIRE_THAT(result, WithinAbs(1.30f, 0.01f));
}

TEST_CASE("LOD: smoothing same values", "[lod_system][tier11]") {
    float result = LodSystemModule::compute_smoothed_modifier(1.5f, 1.5f, 0.30f);
    REQUIRE_THAT(result, WithinAbs(1.5f, 0.01f));
}

TEST_CASE("LOD: monthly tick check", "[lod_system][tier11]") {
    REQUIRE(LodSystemModule::is_monthly_tick(0) == true);
    REQUIRE(LodSystemModule::is_monthly_tick(30) == true);
    REQUIRE(LodSystemModule::is_monthly_tick(15) == false);
}

TEST_CASE("LOD: annual tick check", "[lod_system][tier11]") {
    REQUIRE(LodSystemModule::is_annual_tick(0) == true);
    REQUIRE(LodSystemModule::is_annual_tick(365) == true);
    REQUIRE(LodSystemModule::is_annual_tick(180) == false);
}

TEST_CASE("LOD: config defaults match spec", "[lod_system][tier11]") {
    REQUIRE(LodSystemModule::TICKS_PER_MONTH == 30);
    REQUIRE(LodSystemModule::TICKS_PER_YEAR == 365);
    constexpr LodSystemConfig cfg{};
    REQUIRE_THAT(cfg.lod2_min_modifier, WithinAbs(0.50f, 0.001f));
    REQUIRE_THAT(cfg.lod2_max_modifier, WithinAbs(2.00f, 0.001f));
    REQUIRE_THAT(cfg.lod2_smoothing_rate, WithinAbs(0.30f, 0.001f));
}

// ===========================================================================
// LOD 2 price index delta emission + apply path
// ===========================================================================
//
// On annual ticks, lod_system aggregates supply/demand per good across LOD 2
// statistical provinces and emits one Lod2PriceIndexDelta per good. The
// apply function lerps from the prior stored modifier (default 1.0 if absent)
// using LodSystemConfig::lod2_smoothing_rate.

namespace {

WorldState make_lod2_world(uint32_t tick) {
    WorldState state{};
    state.current_tick = tick;
    state.world_seed = 1;
    state.lod2_price_index = std::make_unique<GlobalCommodityPriceIndex>();
    state.player.reset();
    return state;
}

Province make_lod2_province(uint32_t id, SimulationLOD lod) {
    Province prov{};
    prov.id = id;
    prov.region_id = id;
    prov.lod_level = lod;
    prov.cohort_stats = std::make_unique<RegionCohortStats>();
    return prov;
}

}  // namespace

TEST_CASE("LOD: annual tick emits per-good price index deltas",
          "[lod_system][lod2_index][tier11]") {
    auto state = make_lod2_world(365);  // annual tick
    state.provinces.push_back(make_lod2_province(0, SimulationLOD::statistical));

    // Two markets in the LOD 2 province: good_id 1 (high consumption),
    // good_id 2 (high production).
    RegionalMarket m1{};
    m1.good_id = 1;
    m1.province_id = 0;
    m1.supply = 10.0f;
    m1.demand_buffer = 50.0f;
    state.regional_markets.push_back(m1);
    RegionalMarket m2{};
    m2.good_id = 2;
    m2.province_id = 0;
    m2.supply = 100.0f;
    m2.demand_buffer = 10.0f;
    state.regional_markets.push_back(m2);

    LodSystemModule mod;
    DeltaBuffer delta;
    mod.execute(state, delta);

    REQUIRE(delta.lod2_price_index_deltas.size() == 2);
    // Ascending good_id order from the std::map aggregation.
    REQUIRE(delta.lod2_price_index_deltas[0].good_id == 1);
    REQUIRE(delta.lod2_price_index_deltas[1].good_id == 2);
    // good 1: consumption 50 / max(10, 1) = 5.0 → clamped to 2.0 (max).
    REQUIRE_THAT(delta.lod2_price_index_deltas[0].raw_modifier, WithinAbs(2.0f, 0.001f));
    // good 2: consumption 10 / max(100, 1) = 0.1 → clamped to 0.5 (min).
    REQUIRE_THAT(delta.lod2_price_index_deltas[1].raw_modifier, WithinAbs(0.5f, 0.001f));
}

TEST_CASE("LOD: non-annual tick emits no price index deltas",
          "[lod_system][lod2_index][tier11]") {
    auto state = make_lod2_world(180);  // not annual
    state.provinces.push_back(make_lod2_province(0, SimulationLOD::statistical));
    RegionalMarket m{};
    m.good_id = 1;
    m.province_id = 0;
    m.supply = 10.0f;
    m.demand_buffer = 50.0f;
    state.regional_markets.push_back(m);

    LodSystemModule mod;
    DeltaBuffer delta;
    mod.execute(state, delta);

    REQUIRE(delta.lod2_price_index_deltas.empty());
}

TEST_CASE("LOD: only statistical provinces contribute to LOD 2 aggregation",
          "[lod_system][lod2_index][tier11]") {
    auto state = make_lod2_world(365);
    state.provinces.push_back(make_lod2_province(0, SimulationLOD::full));         // skip
    state.provinces.push_back(make_lod2_province(1, SimulationLOD::statistical));  // include

    RegionalMarket m_full{};
    m_full.good_id = 1;
    m_full.province_id = 0;
    m_full.supply = 100.0f;
    m_full.demand_buffer = 100.0f;
    state.regional_markets.push_back(m_full);

    RegionalMarket m_lod2{};
    m_lod2.good_id = 1;
    m_lod2.province_id = 1;
    m_lod2.supply = 1.0f;
    m_lod2.demand_buffer = 10.0f;
    state.regional_markets.push_back(m_lod2);

    LodSystemModule mod;
    DeltaBuffer delta;
    mod.execute(state, delta);

    REQUIRE(delta.lod2_price_index_deltas.size() == 1);
    // Should reflect only the LOD2 province's signal.
    REQUIRE_THAT(delta.lod2_price_index_deltas[0].raw_modifier, WithinAbs(2.0f, 0.001f));
}

TEST_CASE("LOD: apply lerps from prior modifier using smoothing rate",
          "[lod_system][lod2_index][tier11]") {
    auto state = make_lod2_world(365);
    // Pre-seed the existing modifier for good_id 1.
    state.lod2_price_index->lod2_price_modifier[1] = 1.0f;

    Lod2PriceIndexDelta lpd{};
    lpd.good_id = 1;
    lpd.raw_modifier = 2.0f;

    DeltaBuffer delta;
    delta.lod2_price_index_deltas.push_back(lpd);

    apply_deltas(state, delta);

    // smoothed = 1.0 + 0.30 * (2.0 - 1.0) = 1.30 (default smoothing_rate=0.30).
    REQUIRE_THAT(state.lod2_price_index->lod2_price_modifier[1], WithinAbs(1.30f, 0.001f));
    REQUIRE(state.lod2_price_index->last_updated_tick == 365);
}

TEST_CASE("LOD: apply seeds at 1.0 when good_id is new",
          "[lod_system][lod2_index][tier11]") {
    auto state = make_lod2_world(365);
    REQUIRE(state.lod2_price_index->lod2_price_modifier.find(42) ==
            state.lod2_price_index->lod2_price_modifier.end());

    Lod2PriceIndexDelta lpd{};
    lpd.good_id = 42;
    lpd.raw_modifier = 1.5f;

    DeltaBuffer delta;
    delta.lod2_price_index_deltas.push_back(lpd);
    apply_deltas(state, delta);

    // smoothed = 1.0 + 0.30 * (1.5 - 1.0) = 1.15
    REQUIRE_THAT(state.lod2_price_index->lod2_price_modifier[42], WithinAbs(1.15f, 0.001f));
}

TEST_CASE("LOD: apply rejects NaN raw_modifier",
          "[lod_system][lod2_index][tier11]") {
    auto state = make_lod2_world(365);
    state.lod2_price_index->lod2_price_modifier[1] = 1.2f;

    Lod2PriceIndexDelta lpd{};
    lpd.good_id = 1;
    lpd.raw_modifier = std::numeric_limits<float>::quiet_NaN();

    DeltaBuffer delta;
    delta.lod2_price_index_deltas.push_back(lpd);
    apply_deltas(state, delta);

    // Pathological delta dropped; existing modifier preserved.
    REQUIRE_THAT(state.lod2_price_index->lod2_price_modifier[1], WithinAbs(1.2f, 0.001f));
}
