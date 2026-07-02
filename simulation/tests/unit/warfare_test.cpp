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

TEST_CASE("warfare: a defensive coalition (balance of power) deters a would-be conqueror",
          "[warfare][tier2]") {
    // A can beat B alone, but not B once C allies with it. A-B and B-C adjacent (chain).
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    cfg.absorb_after_wins = 0;  // isolate the relation-coalition mechanic (no annexation)
    add_polity(w, 100, 0, /*pop=*/20000, 1.0f);  // A: beats B alone (26k>20k fails vs B+C)
    add_polity(w, 200, 1, /*pop=*/10000, 1.0f);  // B: weak
    add_polity(w, 300, 2, /*pop=*/10000, 1.0f);  // C: B's ally-to-be
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));
    w.provinces[1].links.push_back(link_to(300));
    w.provinces[2].links.push_back(link_to(200));

    WarfareModule mod(cfg);
    // Year 1: B and C are strangers — A attacks B (coalition = B alone).
    w.current_tick = 365;
    DeltaBuffer d1{};
    mod.execute(w, d1);
    apply_deltas(w, d1);
    CHECK(w.provinces[1].cohort_stats->war_mortality > 1.0f);

    // B and C are equals at peace -> they warm into an alliance over the years.
    for (uint32_t y = 2; y <= 30; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
    }
    CHECK(mod.relation(1, 2) >= cfg.ally_threshold);  // B-C allied

    // The coalition B+C now out-deters A: B is spared.
    w.current_tick = 31 * 365;
    DeltaBuffer d2{};
    mod.execute(w, d2);
    apply_deltas(w, d2);
    CHECK(w.provinces[1].cohort_stats->war_mortality == 1.0f);  // deterred by the coalition
}

TEST_CASE("warfare: betraying an ally brands the betrayer a pariah (reputation economy)",
          "[warfare][tier2]") {
    // A is warm-allied to both B and C. A then betrays B — and its relation with the
    // uninvolved ally C sours too (a known backstabber's word is worthless).
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    cfg.relation_deter_weight = 0.0f;  // isolate the reputation mechanic (no deterrence)
    add_polity(w, 100, 0, /*pop=*/50000, 1.0f);  // A
    add_polity(w, 200, 1, /*pop=*/50000, 1.0f);  // B
    add_polity(w, 300, 2, /*pop=*/50000, 1.0f);  // C
    w.provinces[0].links.push_back(link_to(200));  // A-B
    w.provinces[0].links.push_back(link_to(300));  // A-C
    w.provinces[1].links.push_back(link_to(100));
    w.provinces[2].links.push_back(link_to(100));
    // (B and C are not adjacent.)

    WarfareModule mod(cfg);
    for (uint32_t y = 1; y <= 20; ++y) {  // parity peace -> A-B and A-C warm
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
    }
    CHECK(mod.relation(0, 1) >= cfg.ally_threshold);
    CHECK(mod.relation(0, 2) >= cfg.ally_threshold);
    const float ac_before = mod.relation(0, 2);

    // A becomes a hegemon over B (but not over C, which kept pace) and betrays B.
    w.provinces[0].cohort_stats->total_population = 100000;
    w.provinces[2].cohort_stats->total_population = 100000;  // C stays too strong to attack
    w.current_tick = 21 * 365;
    DeltaBuffer d2{};
    mod.execute(w, d2);
    apply_deltas(w, d2);

    CHECK(w.provinces[1].cohort_stats->war_mortality > 1.0f);   // B was betrayed
    CHECK(w.provinces[2].cohort_stats->war_mortality == 1.0f);  // C was not attacked
    CHECK(mod.relation(0, 2) < ac_before);  // yet A's relation with C soured — the pariah brand
}

TEST_CASE("warfare: annual gate — war decisions fire once per year, not per tick",
          "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    add_polity(w, 100, 0, /*pop=*/100000, /*surplus=*/1.5f);
    add_polity(w, 200, 1, /*pop=*/4000, /*surplus=*/1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));
    w.npc_indices_by_home_province.resize(2);
    NPC a{}, b{};
    a.id = 1; a.capital = 100.0f;
    b.id = 2; b.capital = 1000.0f;
    w.significant_npcs.push_back(a);
    w.significant_npcs.push_back(b);
    w.npc_indices_by_home_province[0].push_back(0);
    w.npc_indices_by_home_province[1].push_back(1);

    WarfareModule mod(cfg);
    // Mid-year ticks (a daily-resolution run): the module must do NOTHING — the same
    // year-seeded war must not re-fire and compound plunder 365x.
    for (uint32_t t = 366; t < 730; ++t) {
        w.current_tick = t;
        DeltaBuffer d{};
        mod.execute(w, d);
        CHECK(d.region_deltas.empty());
        CHECK(d.npc_deltas.empty());
    }
    // The annual tick runs the year's decision pass exactly once.
    w.current_tick = 730;
    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK_FALSE(d.region_deltas.empty());
}

TEST_CASE("warfare: leaving the pre-market arc resets war_mortality (no stale phantom war)",
          "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;  // feudal: war fires
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    add_polity(w, 100, 0, /*pop=*/100000, /*surplus=*/1.5f);
    add_polity(w, 200, 1, /*pop=*/4000, /*surplus=*/1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    DeltaBuffer d1{};
    mod.execute(w, d1);
    apply_deltas(w, d1);
    REQUIRE(w.provinces[1].cohort_stats->war_mortality > 1.0f);  // at war

    // The era advances out of warfare's regimes mid-story: the module must publish a
    // one-time 1.0 reset instead of leaving the spike to be applied forever.
    w.technology.current_era = 8;  // modern
    w.current_tick = 366;          // not even an annual tick — the reset must not wait a year
    DeltaBuffer d2{};
    mod.execute(w, d2);
    apply_deltas(w, d2);
    CHECK(w.provinces[1].cohort_stats->war_mortality == 1.0f);  // reset

    // And only once — subsequent market-era ticks publish nothing.
    w.current_tick = 367;
    DeltaBuffer d3{};
    mod.execute(w, d3);
    CHECK(d3.region_deltas.empty());
}

TEST_CASE("warfare: no plunder when the victor has no residents to receive it (conserved)",
          "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = 365;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    add_polity(w, 100, 0, /*pop=*/100000, /*surplus=*/1.5f);  // A: strong, NO residents
    add_polity(w, 200, 1, /*pop=*/4000, /*surplus=*/1.0f);    // B: weak, wealthy
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));
    w.npc_indices_by_home_province.resize(2);
    NPC npc{};
    npc.id = 3;
    npc.capital = 1000.0f;
    w.significant_npcs.push_back(npc);
    w.npc_indices_by_home_province[1].push_back(0);  // only B has a resident

    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    // The war still happens (casualties), but no wealth is transferred — and none is
    // DESTROYED: B keeps its full 1000 (debit without credit would have leaked it).
    CHECK(w.provinces[1].cohort_stats->war_mortality > 1.0f);
    CHECK(w.significant_npcs[0].capital == 1000.0f);
    CHECK(d.npc_deltas.empty());
}

TEST_CASE("warfare: relations survive a save/load round-trip", "[warfare][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    add_polity(w, 100, 0, 50000, 1.0f);
    add_polity(w, 200, 1, 50000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    for (uint32_t y = 1; y <= 10; ++y) {  // peace warms relations
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
    }
    const float rel = mod.relation(0, 1);
    REQUIRE(rel > 0.0f);

    std::vector<uint8_t> blob;
    mod.serialize_state(blob);
    WarfareModule fresh(cfg);
    CHECK(fresh.relation(0, 1) == 0.0f);
    REQUIRE(fresh.deserialize_state(blob.data(), blob.size()));
    CHECK(fresh.relation(0, 1) == rel);  // diplomacy survives the load
}

TEST_CASE("warfare: repeated decisive wins absorb the loser's polity (empire peace inside)",
          "[warfare][polity][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;  // the qualifying attack fires every year
    add_polity(w, 100, 0, /*pop=*/100000, /*surplus=*/1.5f);  // A: hegemon
    add_polity(w, 200, 1, /*pop=*/4000, /*surplus=*/1.0f);    // B: weak neighbour
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    CHECK(mod.polity_of(0) == 0);  // emergent: every settlement its own polity
    CHECK(mod.polity_of(1) == 1);

    // Years 1-2: wars, but B still independent. Year 3: the third decisive win
    // absorbs B into A's polity (settlement -> kingdom, by conquest).
    for (uint32_t y = 1; y <= 3; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
        if (y < 3)
            CHECK(mod.polity_of(1) == 1);
    }
    CHECK(mod.polity_of(1) == 0);  // absorbed

    // Empire peace: members never war each other, however lopsided the power.
    w.current_tick = 4 * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(w.provinces[1].cohort_stats->war_mortality == 1.0f);
}

TEST_CASE("warfare: polity members pool power — the kingdom deters what a lone province cannot",
          "[warfare][polity][tier2]") {
    // C (30k) can beat B (10k) alone at ratio 1.3, but not once B is a member of A's
    // polity (A 100k pools with B). Same shape as the coalition test, via membership.
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    add_polity(w, 100, 0, /*pop=*/100000, 1.0f);  // A: seat of the kingdom
    add_polity(w, 200, 1, /*pop=*/10000, 1.0f);   // B: member-to-be (border province)
    add_polity(w, 300, 2, /*pop=*/30000, 1.0f);   // C: would-be raider of B
    for (auto& p : w.provinces) {
        p.geography.arable_land_fraction = 0.8f;  // farmland: no cavalry 2-hop reach
        p.geography.forest_coverage = 0.2f;
    }
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));
    w.provinces[1].links.push_back(link_to(300));
    w.provinces[2].links.push_back(link_to(200));

    WarfareConfig cfg_absorb = cfg;
    WarfareModule mod(cfg_absorb);
    // Fold B into A's polity by force (3 wins).
    for (uint32_t y = 1; y <= 3; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
    }
    REQUIRE(mod.polity_of(1) == 0);

    // C would qualify against lone-B (30k >= 1.3 x 10k) — but B now fields the
    // kingdom's pooled power, so C's raid is DETERRED: B suffers no defender loss.
    // (Emergent flip side: the kingdom's pooled power qualifies against C, so B may
    // bear ATTACKER losses as the empire's border spear — that is the mechanic
    // working, not a raid on B. A no longer attacks B either — same polity.)
    w.current_tick = 4 * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(w.provinces[1].cohort_stats->war_mortality < 1.0f + cfg.defender_loss);
    CHECK(w.provinces[2].cohort_stats->war_mortality >= 1.0f + cfg.defender_loss);  // C raided
}

TEST_CASE("warfare: a member that outgrows the polity secedes (the hold problem)",
          "[warfare][polity][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f);  // A: hegemon
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f);    // B: absorbed member
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    for (uint32_t y = 1; y <= 3; ++y) {  // absorb B
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
    }
    REQUIRE(mod.polity_of(1) == 0);

    // The centre collapses (plague/decline): it can no longer overawe the member —
    // the member secedes and the ladder drops a level (successor state).
    w.provinces[0].cohort_stats->total_population = 1000;
    w.current_tick = 4 * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(mod.polity_of(1) == 1);  // seceded

    // And the polity map survives save/load (serialization v2).
    for (uint32_t y = 5; y <= 7; ++y) {  // re-absorb under restored A
        w.provinces[0].cohort_stats->total_population = 100000;
        w.current_tick = y * 365;
        DeltaBuffer d2{};
        mod.execute(w, d2);
        apply_deltas(w, d2);
    }
    REQUIRE(mod.polity_of(1) == 0);
    std::vector<uint8_t> blob;
    mod.serialize_state(blob);
    WarfareModule fresh(cfg);
    CHECK(fresh.polity_of(1) == 1);
    REQUIRE(fresh.deserialize_state(blob.data(), blob.size()));
    CHECK(fresh.polity_of(1) == 0);  // the political map survives the load
}

namespace {
// Build a v3 module-state blob by hand (little-endian), to inject asymmetric
// conqueror state (a leader on one seat, a long-held member) deterministically.
struct BlobBuilder {
    std::vector<uint8_t> bytes;
    void u32(uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16));
        bytes.push_back(static_cast<uint8_t>(v >> 24));
    }
};
std::vector<uint8_t> make_warfare_blob(
    const std::vector<std::pair<uint32_t, uint32_t>>& polities,
    const std::vector<std::pair<uint32_t, uint32_t>>& leaders,
    const std::vector<std::pair<uint32_t, uint32_t>>& members_since) {
    BlobBuilder b;
    b.u32(3u);              // version
    b.bytes.push_back(0u);  // war_state_dirty
    b.u32(0u);              // relations: none
    b.u32(static_cast<uint32_t>(polities.size()));
    for (auto [prov, pid] : polities) {
        b.u32(prov);
        b.u32(pid);
    }
    b.u32(0u);  // win counts: none
    b.u32(static_cast<uint32_t>(leaders.size()));
    for (auto [seat, until] : leaders) {
        b.u32(seat);
        b.u32(until);
    }
    b.u32(static_cast<uint32_t>(members_since.size()));
    for (auto [prov, since] : members_since) {
        b.u32(prov);
        b.u32(since);
    }
    return b.bytes;
}
}  // namespace

TEST_CASE("warfare: the Alexander arc — a great commander conquers; his death fragments it",
          "[warfare][conqueror][tier2]") {
    WorldState w{};
    w.world_seed = 1;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;
    cfg.leadership_rate = 0.0f;   // no new rolls — the injected leader is the only one
    cfg.absorb_after_wins = 1;    // a decisive campaign
    add_polity(w, 100, 0, /*pop=*/40000, 1.0f);  // A: small Macedon
    add_polity(w, 200, 1, /*pop=*/60000, 1.0f);  // B: the bigger neighbour
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    // Raw power could never attack: 40k < 1.3 x 60k. Inject a great commander on A's
    // seat (tenure through year 4): power x2.5 = 100k >= 78k — conquest qualifies.
    auto blob = make_warfare_blob({}, {{0u, 5u}}, {});
    REQUIRE(mod.deserialize_state(blob.data(), blob.size()));

    w.current_tick = 1 * 365;
    DeltaBuffer d1{};
    mod.execute(w, d1);
    apply_deltas(w, d1);
    CHECK(mod.has_leader(0, 1));
    CHECK(mod.polity_of(1) == 0);  // conquered in one campaign — the leadership surge

    // While the commander lives, the inflated centre overawes the member.
    for (uint32_t y = 2; y <= 4; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
        CHECK(mod.polity_of(1) == 0);
    }

    // Year 5: the commander's tenure ends. The centre's power collapses back to 40k;
    // the young conquest (low cohesion) out-powers it and secedes — the Diadochi.
    w.current_tick = 5 * 365;
    DeltaBuffer d5{};
    mod.execute(w, d5);
    apply_deltas(w, d5);
    CHECK_FALSE(mod.has_leader(0, 5));
    CHECK(mod.polity_of(1) == 1);  // the empire fragments on the conqueror's death
}

TEST_CASE("warfare: the Genghis reach — a steppe polity strikes past the grain radius",
          "[warfare][conqueror][tier2]") {
    // Chain A - B - C: A is NOT adjacent to C. A farming polity can only reach B;
    // a STEPPE polity (herd-fed cavalry) projects force to the 2-hop target C.
    auto run = [](float arable) {
        WorldState w{};
        w.world_seed = 1;
        w.current_tick = 365;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;
        WarfareConfig cfg{};
        cfg.base_aggression_prob = 1.0f;
        cfg.leadership_rate = 0.0f;
        add_polity(w, 100, 0, /*pop=*/50000, 1.0f);  // A: the (maybe) cavalry power
        add_polity(w, 200, 1, /*pop=*/45000, 1.0f);  // B: buffer (deterred by C's 36k)
        add_polity(w, 300, 2, /*pop=*/36000, 1.0f);  // C: 2 hops from A; only A's 50k
                                                     // clears 1.3 x 36k — B cannot
        w.provinces[0].geography.arable_land_fraction = arable;  // steppe or farmland
        w.provinces[0].geography.forest_coverage = 0.1f;
        w.provinces[1].geography.arable_land_fraction = 0.8f;  // farmers
        w.provinces[1].geography.forest_coverage = 0.2f;
        w.provinces[2].geography.arable_land_fraction = 0.8f;
        w.provinces[2].geography.forest_coverage = 0.2f;
        w.provinces[0].links.push_back(link_to(200));
        w.provinces[1].links.push_back(link_to(100));
        w.provinces[1].links.push_back(link_to(300));
        w.provinces[2].links.push_back(link_to(200));
        WarfareModule mod(cfg);
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
        return w.provinces[2].cohort_stats->war_mortality;  // was C hit?
    };
    const float farming = run(0.8f);   // A is farmland: C is beyond reach
    const float steppe = run(0.05f);   // A is steppe: cavalry reaches C
    CHECK(farming == 1.0f);
    CHECK(steppe > 1.0f);  // the tyranny of the ox does not bind the herd-fed
}

TEST_CASE("warfare: the Rome hold — an old, integrated conquest endures where a fresh one secedes",
          "[warfare][conqueror][tier2]") {
    auto run = [](bool long_held) {
        WorldState w{};
        w.world_seed = 1;
        w.era_catalog.load_builtin_default();
        w.technology.current_era = 5;
        WarfareConfig cfg{};
        cfg.base_aggression_prob = 0.0f;  // isolate the hold check (no new wars)
        cfg.leadership_rate = 0.0f;
        add_polity(w, 100, 0, /*pop=*/55000, 1.0f);  // A: the imperial centre
        add_polity(w, 200, 1, /*pop=*/50000, 1.0f);  // B: the province in question
        w.provinces[0].links.push_back(link_to(200));
        w.provinces[1].links.push_back(link_to(100));
        WarfareModule mod(cfg);
        // B is a member of A's polity. Fresh conquest: absorbed THIS year (cohesion
        // ~1). Old province: absorbed 150 years ago (cohesion capped at 3x).
        const uint32_t year_now = 200;
        const uint32_t since = long_held ? 50u : year_now;
        auto blob = make_warfare_blob({{1u, 0u}}, {}, {{1u, since}});
        REQUIRE(mod.deserialize_state(blob.data(), blob.size()));
        w.current_tick = year_now * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
        return mod.polity_of(1);
    };
    // Borderline member (50k vs rest 55k; base threshold 0.8x55k = 44k): the fresh
    // conquest breaks away; the province held for generations is integrated and stays.
    CHECK(run(/*long_held=*/false) == 1);  // seceded
    CHECK(run(/*long_held=*/true) == 0);   // Roman — it holds
}
