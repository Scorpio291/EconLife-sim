#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/geography.h"
#include "core/world_state/world_state.h"
#include "modules/warfare/warfare_module.h"

using namespace econlife;

namespace {
void add_polity(WorldState& w, uint64_t h3, uint32_t region_id, uint32_t population,
                float surplus) {
    Province p{};
    p.id = region_id;
    p.region_id = region_id;
    p.h3_index = static_cast<H3Index>(h3);
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = population;
    p.cohort_stats->subsistence_surplus_ratio = surplus;
    w.provinces.push_back(std::move(p));
}
ProvinceLink link_to(uint64_t neighbor_h3) {
    ProvinceLink l{};
    l.neighbor_h3 = static_cast<H3Index>(neighbor_h3);
    l.type = LinkType::Land;
    return l;
}
}  // namespace

TEST_CASE("warfare: military power scales with population and how well-fed", "[warfare][tier2]") {
    WarfareConfig cfg{};
    CHECK(WarfareModule::military_power(0, 1.0f, cfg) == 0.0f);  // no people, no army
    CHECK(WarfareModule::military_power(2000, 1.0f, cfg) >
          WarfareModule::military_power(1000, 1.0f, cfg));  // more people -> more power
    const float starving = WarfareModule::military_power(1000, 0.0f, cfg);
    const float fed = WarfareModule::military_power(1000, 1.0f, cfg);
    const float rich = WarfareModule::military_power(1000, 2.0f, cfg);
    CHECK(fed > starving);   // a surplus feeds more soldiers
    CHECK(rich > fed);
    CHECK(starving > 0.0f);  // even a starving polity can levy bodies (the floor)
}

TEST_CASE("warfare: a strong polity attacks a weak reachable neighbour; war kills both",
          "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;  // feudal — a warfare-active regime
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;  // a qualifying attack always happens (deterministic)

    add_polity(w, 100, 0, /*pop=*/100000, /*surplus=*/1.5f);  // A: strong
    add_polity(w, 200, 1, /*pop=*/4000, /*surplus=*/1.0f);    // B: weak, reachable
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    const float wmA = w.provinces[0].cohort_stats->war_mortality;
    const float wmB = w.provinces[1].cohort_stats->war_mortality;
    CHECK(wmA > 1.0f);   // the attacker bleeds too
    CHECK(wmB > 1.0f);   // the defender is at war
    CHECK(wmB > wmA);    // war is worse to lose
    CHECK_THAT(wmA, Catch::Matchers::WithinAbs(1.0f + cfg.attacker_loss, 1e-4f));
    CHECK_THAT(wmB, Catch::Matchers::WithinAbs(1.0f + cfg.defender_loss, 1e-4f));
}

TEST_CASE("warfare: evenly-matched neighbours stay at peace", "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;  // even with certain aggression, parity deters

    add_polity(w, 100, 0, 50000, 1.0f);
    add_polity(w, 200, 1, 50000, 1.0f);  // equal power -> neither clears aggression_ratio
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    CHECK(w.provinces[0].cohort_stats->war_mortality == 1.0f);  // peace
    CHECK(w.provinces[1].cohort_stats->war_mortality == 1.0f);
}

TEST_CASE("warfare: inert in market eras", "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 8;  // modern — warfare here is political_cycle's
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    add_polity(w, 100, 0, 100000, 1.5f);
    add_polity(w, 200, 1, 4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK(d.region_deltas.empty());  // no publication in a market era
}
