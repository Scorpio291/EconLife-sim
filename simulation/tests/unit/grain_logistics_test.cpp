#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/geography.h"
#include "core/world_state/world_state.h"
#include "modules/grain_logistics/grain_logistics_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

namespace {
GrainLogisticsConfig cfg() { return GrainLogisticsConfig{}; }

// Add a province with a given h3 id, region id (1:1 with province in world-gen), and
// haulable grain surplus.
void add_province(WorldState& w, uint64_t h3, uint32_t region_id, float surplus) {
    Province p{};
    p.h3_index = static_cast<H3Index>(h3);
    p.id = region_id;
    p.region_id = region_id;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->grain_surplus = surplus;
    w.provinces.push_back(std::move(p));
}

ProvinceLink link_to(uint64_t neighbor_h3, LinkType type, float terrain = 0.0f, float infra = 0.0f) {
    ProvinceLink l{};
    l.neighbor_h3 = static_cast<H3Index>(neighbor_h3);
    l.type = type;
    l.shared_border_km = 50.0f;
    l.transit_terrain_cost = terrain;
    l.infrastructure_bonus = infra;
    return l;
}

WorldState dawn_world() {
    WorldState w{};
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;  // medieval / feudal — a commons regime
    return w;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// delivered_fraction (pure)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("grain_logistics: water delivers far more than land", "[grain_logistics][tier1]") {
    const auto c = cfg();
    const float land = GrainLogisticsModule::delivered_fraction(LinkType::Land, 0, 0, 1.0f, c);
    const float river = GrainLogisticsModule::delivered_fraction(LinkType::River, 0, 0, 1.0f, c);
    const float sea = GrainLogisticsModule::delivered_fraction(LinkType::Maritime, 0, 0, 1.0f, c);
    CHECK(land < river);
    CHECK(river <= sea);            // maritime is the cheapest mode
    CHECK(river > 0.85f);           // water haulage barely loses anything
    CHECK(land < 0.6f);             // land is the tyranny
    CHECK(land > 0.0f);
}

TEST_CASE("grain_logistics: mountains block land hauling; roads relieve it",
          "[grain_logistics][tier1]") {
    const auto c = cfg();
    const float flat = GrainLogisticsModule::delivered_fraction(LinkType::Land, 0.0f, 0.0f, 1.0f, c);
    const float mountains =
        GrainLogisticsModule::delivered_fraction(LinkType::Land, 1.0f, 0.0f, 1.0f, c);
    const float roaded =
        GrainLogisticsModule::delivered_fraction(LinkType::Land, 0.0f, 1.0f, 1.0f, c);
    CHECK(mountains < flat);
    CHECK(mountains == 0.0f);  // impassable terrain → nothing arrives
    CHECK(roaded > flat);      // a road extends the radius
}

TEST_CASE("grain_logistics: heavier gravity shrinks the haulage radius (§5.5 coupling)",
          "[grain_logistics][tier1]") {
    const auto c = cfg();
    const float earth = GrainLogisticsModule::delivered_fraction(LinkType::Land, 0, 0, 1.0f, c);
    const float heavy = GrainLogisticsModule::delivered_fraction(LinkType::Land, 0, 0, 1.5f, c);
    CHECK(heavy < earth);  // oxen carry less and tire faster on a heavy world
}

// ─────────────────────────────────────────────────────────────────────────────
// module execute (conservation + the catchment map)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("grain_logistics: a province with no links keeps all its surplus locally",
          "[grain_logistics][tier1]") {
    WorldState w = dawn_world();
    add_province(w, 100, 0, 1000.0f);  // isolated, no links

    GrainLogisticsModule mod;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    // Self-delivery is lossless (df = 1.0): net feedable == its own surplus.
    CHECK_THAT(w.provinces[0].cohort_stats->net_feedable_surplus, WithinAbs(1000.0f, 0.5f));
}

TEST_CASE("grain_logistics: conserved haulage — water neighbour out-feeds a land neighbour",
          "[grain_logistics][tier1]") {
    WorldState w = dawn_world();
    add_province(w, 100, 0, 1000.0f);  // A: the only surplus
    add_province(w, 200, 1, 0.0f);     // B: reached from A by RIVER
    add_province(w, 300, 2, 0.0f);     // C: reached from A by LAND
    w.provinces[0].links.push_back(link_to(200, LinkType::River));
    w.provinces[0].links.push_back(link_to(300, LinkType::Land));

    GrainLogisticsModule mod;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    const float a = w.provinces[0].cohort_stats->net_feedable_surplus;
    const float b = w.provinces[1].cohort_stats->net_feedable_surplus;
    const float cc = w.provinces[2].cohort_stats->net_feedable_surplus;

    // The river neighbour aggregates far more of A's surplus than the land neighbour.
    CHECK(b > cc);
    CHECK(b > 0.0f);
    CHECK(cc > 0.0f);
    // A keeps a real local share (self-delivery, the highest-weight destination).
    CHECK(a > 0.0f);
    // CONSERVATION: total delivered cannot exceed what was produced; the shortfall is
    // the grain the draft teams ate in transit (a real, positive loss here).
    const float total = a + b + cc;
    CHECK(total <= 1000.0f);
    CHECK(total < 1000.0f);  // some eaten on the land + river hauls
    CHECK(total > 850.0f);   // but water keeps most of it in the system
}

TEST_CASE("grain_logistics: inert in market eras (no commons surplus to haul)",
          "[grain_logistics][tier1]") {
    WorldState w = dawn_world();
    w.technology.current_era = 8;       // modern (market regime)
    add_province(w, 100, 0, 1000.0f);   // surplus present but module must not run

    GrainLogisticsModule mod;
    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK(d.region_deltas.empty());  // no publication in a market era
}
