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

TEST_CASE("warfare: a won war plunders the loser's wealth to the victor (conserved)",
          "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;  // feudal
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;  // the attack happens

    add_polity(w, 100, 0, /*pop=*/100000, /*surplus=*/1.5f);  // A: strong victor
    add_polity(w, 200, 1, /*pop=*/4000, /*surplus=*/1.0f);    // B: weak, wealthy victim
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    // Residents with proto-capital — B's wealth is the prize.
    w.npc_indices_by_home_province.resize(2);
    auto add_npc = [&](uint32_t id, uint32_t prov, float cap) {
        NPC npc{};
        npc.id = id;
        npc.capital = cap;
        const uint32_t idx = static_cast<uint32_t>(w.significant_npcs.size());
        w.significant_npcs.push_back(npc);
        w.npc_indices_by_home_province[prov].push_back(idx);
    };
    add_npc(1, 0, 100.0f);
    add_npc(2, 0, 100.0f);  // A: 200 total
    add_npc(3, 1, 500.0f);
    add_npc(4, 1, 500.0f);  // B: 1000 total (the prize)
    const float before = 1200.0f;

    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    const float a_cap = w.significant_npcs[0].capital + w.significant_npcs[1].capital;
    const float b_cap = w.significant_npcs[2].capital + w.significant_npcs[3].capital;
    CHECK(b_cap < 1000.0f);  // the loser is plundered
    CHECK(a_cap > 200.0f);   // the victor takes the loot
    CHECK_THAT(a_cap + b_cap, Catch::Matchers::WithinAbs(before, 0.5f));  // CONSERVED
    // plunder = 0.2 x 1000 = 200 -> B keeps 800, A gains to 400.
    CHECK_THAT(b_cap, Catch::Matchers::WithinAbs(800.0f, 0.5f));
    CHECK_THAT(a_cap, Catch::Matchers::WithinAbs(400.0f, 0.5f));
}

TEST_CASE("warfare: a war sours the pair's relations", "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;  // the attack happens
    add_polity(w, 100, 0, /*pop=*/100000, /*surplus=*/1.5f);  // strong
    add_polity(w, 200, 1, /*pop=*/4000, /*surplus=*/1.0f);    // weak
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    CHECK(mod.relation(0, 1) == 0.0f);  // strangers
    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK(mod.relation(0, 1) < 0.0f);  // the war soured relations
}

TEST_CASE("warfare: sustained peace warms neighbours into an alliance that deters attack",
          "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    cfg.relation_deter_weight = 1.0f;  // a full alliance fully deters
    add_polity(w, 100, 0, /*pop=*/50000, /*surplus=*/1.0f);
    add_polity(w, 200, 1, /*pop=*/50000, /*surplus=*/1.0f);  // parity -> no war
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    for (uint32_t y = 1; y <= 60; ++y) {  // 60 peaceful years
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
    }
    CHECK(mod.relation(0, 1) > 0.5f);  // sustained peace -> warm (de-facto alliance)

    // A now dwarfs B — but the alliance deters the attack (no war despite the power).
    w.provinces[0].cohort_stats->total_population = 100000000;
    w.current_tick = 61 * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(w.provinces[1].cohort_stats->war_mortality == 1.0f);  // B spared — allied
    CHECK(mod.relation(0, 1) > 0.5f);                            // alliance intact
}
