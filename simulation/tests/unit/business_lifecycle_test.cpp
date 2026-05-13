// Smoke tests for BusinessLifecycleModule. The module fires exactly once on
// the tick immediately after an era transition; on every other tick it is a
// no-op. These tests confirm both branches and exercise the two effects
// (stranded-asset penalty + era-entrant spawn).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/config/package_config.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"
#include "modules/business_lifecycle/business_lifecycle_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {

WorldState make_world_with_business(uint32_t business_id, BusinessSector sector,
                                    uint32_t province_id, uint32_t current_tick,
                                    uint32_t era_started_tick, SimulationEra era) {
    WorldState w{};
    w.current_tick = current_tick;
    w.world_seed = 1;
    w.game_mode = GameMode::standard;
    w.technology.current_era = era;
    w.technology.era_started_tick = era_started_tick;

    Province p{};
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.id = province_id;
    w.provinces.push_back(p);

    NPCBusiness biz{};
    biz.id = business_id;
    biz.sector = sector;
    biz.province_id = province_id;
    biz.revenue_per_tick = 100.0f;
    biz.cost_per_tick = 50.0f;
    biz.cash = 10000.0f;
    biz.owner_id = 1;
    w.npc_businesses.push_back(biz);

    rebuild_npc_indices(w);
    return w;
}

BusinessLifecycleConfig make_cfg_with_stranded_sector(uint8_t target_era, BusinessSector sector,
                                                      float revenue_penalty, float cost_increase) {
    BusinessLifecycleConfig cfg{};
    StrandedSectorEntry entry{};
    entry.sector = sector;
    entry.revenue_penalty = revenue_penalty;
    entry.cost_increase = cost_increase;
    cfg.stranded_sectors[target_era] = {entry};
    return cfg;
}

}  // namespace

TEST_CASE("BusinessLifecycle: no-op when not on era transition tick",
          "[business_lifecycle][tier2]") {
    // era_started_tick + 1 != current_tick → module should do nothing.
    auto state = make_world_with_business(
        /*business_id=*/1, BusinessSector::energy, /*province=*/0,
        /*current_tick=*/100, /*era_started_tick=*/50, SimulationEra::era_3_acceleration);

    auto cfg = make_cfg_with_stranded_sector(
        /*target_era=*/3, BusinessSector::energy, /*revenue_penalty=*/0.5f, /*cost_increase=*/0.2f);

    BusinessLifecycleModule module(cfg);
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.business_deltas.empty());
    REQUIRE(delta.new_businesses.empty());
}

TEST_CASE("BusinessLifecycle: stranded-asset penalty applied on era-transition tick",
          "[business_lifecycle][tier2]") {
    // era_started_tick + 1 == current_tick → module fires; energy in era 3
    // is stranded, so the business sees a revenue cut and cost bump.
    auto state = make_world_with_business(
        /*business_id=*/1, BusinessSector::energy, /*province=*/0,
        /*current_tick=*/51, /*era_started_tick=*/50, SimulationEra::era_3_acceleration);

    auto cfg = make_cfg_with_stranded_sector(
        /*target_era=*/3, BusinessSector::energy, /*revenue_penalty=*/0.4f, /*cost_increase=*/0.3f);

    BusinessLifecycleModule module(cfg);
    DeltaBuffer delta{};
    module.execute(state, delta);

    // One business_delta for the stranded business.
    REQUIRE(delta.business_deltas.size() == 1);
    REQUIRE(delta.business_deltas[0].business_id == 1);
    REQUIRE(delta.business_deltas[0].revenue_per_tick_update.has_value());
    REQUIRE(delta.business_deltas[0].cost_per_tick_update.has_value());

    // Revenue cut to (1 - 0.4) * 100 = 60, but floored at
    // stranded_revenue_floor * 100 = 20. 60 > 20 so the cut applies fully.
    REQUIRE_THAT(*delta.business_deltas[0].revenue_per_tick_update, WithinAbs(60.0f, 0.01f));
    // Cost: 50 * 1.3 = 65.
    REQUIRE_THAT(*delta.business_deltas[0].cost_per_tick_update, WithinAbs(65.0f, 0.01f));
}

TEST_CASE("BusinessLifecycle: stranded_revenue_floor caps the cut", "[business_lifecycle][tier2]") {
    // A 90% revenue penalty would drop revenue to 10, but the floor (20%
    // of original = 20) clamps it.
    auto state = make_world_with_business(
        /*business_id=*/1, BusinessSector::energy, /*province=*/0,
        /*current_tick=*/51, /*era_started_tick=*/50, SimulationEra::era_3_acceleration);

    auto cfg = make_cfg_with_stranded_sector(
        /*target_era=*/3, BusinessSector::energy, /*revenue_penalty=*/0.9f, /*cost_increase=*/0.0f);

    BusinessLifecycleModule module(cfg);
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(delta.business_deltas.size() == 1);
    REQUIRE(delta.business_deltas[0].revenue_per_tick_update.has_value());
    // Floor: 0.20 * 100 = 20. Computed: (1 - 0.9) * 100 = 10 < 20 → clamp.
    REQUIRE_THAT(*delta.business_deltas[0].revenue_per_tick_update, WithinAbs(20.0f, 0.01f));
}

TEST_CASE("BusinessLifecycle: businesses outside the stranded sector untouched",
          "[business_lifecycle][tier2]") {
    auto state = make_world_with_business(
        /*business_id=*/1, BusinessSector::technology, /*province=*/0,
        /*current_tick=*/51, /*era_started_tick=*/50, SimulationEra::era_3_acceleration);

    auto cfg = make_cfg_with_stranded_sector(
        /*target_era=*/3, BusinessSector::energy, /*revenue_penalty=*/0.4f, /*cost_increase=*/0.3f);

    BusinessLifecycleModule module(cfg);
    DeltaBuffer delta{};
    module.execute(state, delta);

    // Technology business is not in the stranded set for era 3.
    REQUIRE(delta.business_deltas.empty());
}
