#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <memory>
#include <vector>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/geography.h"
#include "core/world_state/world_state.h"
#include "modules/warfare/warfare_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
void add_polity(WorldState& w, uint64_t h3, uint32_t region_id, uint32_t population,
                float surplus, float food_store = 0.0f) {
    Province p{};
    p.id = region_id;
    p.region_id = region_id;
    p.h3_index = static_cast<H3Index>(h3);
    p.geography.arable_land_fraction = 0.8f;  // farmland unless a test says otherwise
    p.geography.forest_coverage = 0.2f;
    p.cohort_stats = std::make_unique<RegionCohortStats>();
    p.cohort_stats->total_population = population;
    p.cohort_stats->subsistence_surplus_ratio = surplus;
    p.cohort_stats->food_store = food_store;
    w.provinces.push_back(std::move(p));
}
ProvinceLink link_to(uint64_t neighbor_h3) {
    ProvinceLink l{};
    l.neighbor_h3 = static_cast<H3Index>(neighbor_h3);
    l.type = LinkType::Land;
    return l;
}
WorldState dawn_world(uint32_t tick = 365) {
    WorldState w{};
    w.world_seed = 1;
    w.current_tick = tick;
    w.era_catalog.load_builtin_default();
    w.technology.current_era = 5;  // feudal — a warfare-active regime
    return w;
}
// Precision tests pin the stochastic dials so outcomes follow from the mechanism.
WarfareConfig sure_cfg() {
    WarfareConfig cfg{};
    cfg.base_aggression_prob = 1.0f;  // the qualifying opportunity is always taken
    cfg.leadership_rate = 0.0f;       // no surprise commanders
    return cfg;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Battle mathematics (pure)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("warfare: Lanchester square-law contest", "[warfare][tier2]") {
    CHECK(WarfareModule::p_attacker_wins(0.0f, 0.0f) == 0.0f);  // no armies, no victory
    CHECK(WarfareModule::p_attacker_wins(0.0f, 100.0f) == 0.0f);
    CHECK_THAT(WarfareModule::p_attacker_wins(100.0f, 100.0f), WithinAbs(0.5f, 1e-5f));
    // 3:1 superiority -> 9:1 odds (the square law).
    CHECK_THAT(WarfareModule::p_attacker_wins(300.0f, 100.0f), WithinAbs(0.9f, 1e-5f));
    // Monotonic in strength.
    CHECK(WarfareModule::p_attacker_wins(200.0f, 100.0f) >
          WarfareModule::p_attacker_wins(150.0f, 100.0f));
}

TEST_CASE("warfare: an army fights at forage strength with an empty commissary",
          "[warfare][tier2]") {
    WarfareConfig cfg{};
    CHECK(WarfareModule::campaign_fed_factor(0.0f, 0.0f, cfg) == 1.0f);  // no rations needed
    // Fully provisioned: full strength (store need = (1 - forage_share) x rations).
    CHECK_THAT(WarfareModule::campaign_fed_factor(1000.0f, 500.0f, cfg), WithinAbs(1.0f, 1e-5f));
    // Empty granary: the army lives off the land at forage_share strength.
    CHECK_THAT(WarfareModule::campaign_fed_factor(1000.0f, 0.0f, cfg),
               WithinAbs(cfg.forage_share, 1e-5f));
    // Half of the store need drawn: halfway between forage and full.
    CHECK_THAT(WarfareModule::campaign_fed_factor(1000.0f, 250.0f, cfg),
               WithinAbs(cfg.forage_share + 0.25f, 1e-5f));
}

// ─────────────────────────────────────────────────────────────────────────────
// The grounded war: casualties in real units, rations from granaries
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("warfare: a lopsided war kills in proportion to enemy strength (real units)",
          "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f);  // A: hegemon (levy 10,000)
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f);    // B: weak neighbour (levy 400)
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    // No granaries: both fight at forage strength. S_a = 10000*0.5, S_b = 400*0.5.
    const float S_a = 10000.0f * cfg.forage_share;
    const float S_b = 400.0f * cfg.forage_share;
    const float dead_a_frac = cfg.battle_lethality * S_b / 100000.0f;
    const float dead_b_frac = cfg.battle_lethality * S_a / 4000.0f;
    CHECK_THAT(w.provinces[0].cohort_stats->war_death_fraction, WithinRel(dead_a_frac, 0.01f));
    CHECK_THAT(w.provinces[1].cohort_stats->war_death_fraction, WithinRel(dead_b_frac, 0.01f));
    // The weak side bleeds catastrophically MORE — from asymmetry, not a constant.
    CHECK(w.provinces[1].cohort_stats->war_death_fraction >
          10.0f * w.provinces[0].cohort_stats->war_death_fraction);
}

TEST_CASE("warfare: campaigns eat granaries and a sack burns what it cannot carry (conserved)",
          "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f, /*store=*/5.0e6f);  // A: hegemon
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f, /*store=*/1.0e6f);    // B: weak, full barns
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    const double total_before = 5.0e6 + 1.0e6;
    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    const double store_a = w.provinces[0].cohort_stats->food_store;
    const double store_b = w.provinces[1].cohort_stats->food_store;
    const double total_after = store_a + store_b;

    // The loser's granary was eaten from (defence rations) and sacked.
    CHECK(store_b < 1.0e6);
    // War destroys grain ONLY through named sinks: rations eaten + sack burned.
    const double army_a = 10000.0, army_b = 400.0;
    const double drawn_a =
        army_a * cfg.campaign_days * cfg.soldier_ration_mult * (1.0 - cfg.forage_share);
    const double drawn_b =
        army_b * cfg.defense_days * cfg.soldier_ration_mult * (1.0 - cfg.forage_share);
    const double store_b_after_rations = 1.0e6 - drawn_b;
    const double sack = cfg.sack_fraction * store_b_after_rations;
    const double carry = army_a * cfg.carry_per_soldier;
    const double delivered = std::min(sack, carry) * 1.0;  // border war: path 1
    const double expected_total = total_before - drawn_a - drawn_b - (sack - delivered);
    CHECK_THAT(total_after, WithinRel(expected_total, 0.001));
    // And the victor hauled real grain home.
    CHECK(store_a > 5.0e6 - drawn_a);
}

TEST_CASE("warfare: evenly-matched neighbours stay at peace", "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, 50000, 1.0f);
    add_polity(w, 200, 1, 50000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(w.provinces[0].cohort_stats->war_death_fraction == 0.0f);
    CHECK(w.provinces[1].cohort_stats->war_death_fraction == 0.0f);
}

TEST_CASE("warfare: annual gate — decisions fire once per year, not per tick",
          "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, 100000, 1.5f);
    add_polity(w, 200, 1, 4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    for (uint32_t t = 366; t < 730; ++t) {
        w.current_tick = t;
        DeltaBuffer d{};
        mod.execute(w, d);
        CHECK(d.region_deltas.empty());
        CHECK(d.npc_deltas.empty());
    }
    w.current_tick = 730;
    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK_FALSE(d.region_deltas.empty());
}

TEST_CASE("warfare: inert in market eras", "[warfare][tier2]") {
    WorldState w = dawn_world();
    w.technology.current_era = 8;  // modern — warfare here is political_cycle's
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, 100000, 1.5f);
    add_polity(w, 200, 1, 4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK(d.region_deltas.empty());
}

TEST_CASE("warfare: leaving the pre-market arc resets war deaths (no stale phantom war)",
          "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, 100000, 1.5f);
    add_polity(w, 200, 1, 4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    DeltaBuffer d1{};
    mod.execute(w, d1);
    apply_deltas(w, d1);
    REQUIRE(w.provinces[1].cohort_stats->war_death_fraction > 0.0f);  // at war

    w.technology.current_era = 8;
    w.current_tick = 366;  // the reset must not wait for an annual tick
    DeltaBuffer d2{};
    mod.execute(w, d2);
    apply_deltas(w, d2);
    CHECK(w.provinces[1].cohort_stats->war_death_fraction == 0.0f);

    w.current_tick = 367;
    DeltaBuffer d3{};
    mod.execute(w, d3);
    CHECK(d3.region_deltas.empty());  // only once
}

TEST_CASE("warfare: coin plunder moves wealth to the victor (conserved)", "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f);
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));
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
    add_npc(2, 0, 100.0f);
    add_npc(3, 1, 500.0f);
    add_npc(4, 1, 500.0f);
    const float before = 1200.0f;

    WarfareModule mod(cfg);
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);

    const float a_cap = w.significant_npcs[0].capital + w.significant_npcs[1].capital;
    const float b_cap = w.significant_npcs[2].capital + w.significant_npcs[3].capital;
    CHECK(b_cap < 1000.0f);  // the loser is plundered
    CHECK(a_cap > 200.0f);   // the victor takes the loot
    CHECK_THAT(a_cap + b_cap, WithinAbs(before, 0.5f));  // conserved exactly
}

TEST_CASE("warfare: no plunder when the victor has no residents to receive it (conserved)",
          "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f);  // A: strong, NO residents
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f);    // B: weak, wealthy
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
    CHECK(w.provinces[1].cohort_stats->war_death_fraction > 0.0f);  // the war happened
    CHECK(w.significant_npcs[0].capital == 1000.0f);                // nothing destroyed
    CHECK(d.npc_deltas.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Diplomacy: relations, alliances, betrayal
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("warfare: a war sours the pair's relations", "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f);
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    CHECK(mod.relation(0, 1) == 0.0f);
    DeltaBuffer d{};
    mod.execute(w, d);
    CHECK(mod.relation(0, 1) < 0.0f);
}

TEST_CASE("warfare: sustained peace warms neighbours into an alliance that deters attack",
          "[warfare][tier2]") {
    WorldState w = dawn_world();
    WarfareConfig cfg = sure_cfg();
    cfg.relation_deter_weight = 1.0f;
    add_polity(w, 100, 0, 50000, 1.0f);
    add_polity(w, 200, 1, 50000, 1.0f);  // parity: peace
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    for (uint32_t y = 1; y <= 60; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
    }
    CHECK(mod.relation(0, 1) > 0.5f);

    // A now dwarfs B — but the alliance deters the attack.
    w.provinces[0].cohort_stats->total_population = 100000000;
    w.current_tick = 61 * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(w.provinces[1].cohort_stats->war_death_fraction == 0.0f);
    CHECK(mod.relation(0, 1) > 0.5f);
}

TEST_CASE("warfare: a defensive coalition (balance of power) deters a would-be conqueror",
          "[warfare][tier2]") {
    WorldState w = dawn_world(0);
    WarfareConfig cfg = sure_cfg();
    cfg.absorb_after_wins = 0;  // isolate the relation-coalition mechanic
    add_polity(w, 100, 0, /*pop=*/20000, 1.0f);
    add_polity(w, 200, 1, /*pop=*/10000, 1.0f);
    add_polity(w, 300, 2, /*pop=*/10000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));
    w.provinces[1].links.push_back(link_to(300));
    w.provinces[2].links.push_back(link_to(200));

    WarfareModule mod(cfg);
    w.current_tick = 365;
    DeltaBuffer d1{};
    mod.execute(w, d1);
    apply_deltas(w, d1);
    CHECK(w.provinces[1].cohort_stats->war_death_fraction > 0.0f);  // year 1: B alone, raided

    for (uint32_t y = 2; y <= 30; ++y) {  // B and C warm into allies
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
    }
    CHECK(mod.relation(1, 2) >= cfg.ally_threshold);

    w.current_tick = 31 * 365;
    DeltaBuffer d2{};
    mod.execute(w, d2);
    apply_deltas(w, d2);
    CHECK(w.provinces[1].cohort_stats->war_death_fraction == 0.0f);  // the coalition deters
}

TEST_CASE("warfare: betraying an ally brands the betrayer a pariah (reputation economy)",
          "[warfare][tier2]") {
    WorldState w = dawn_world(0);
    WarfareConfig cfg = sure_cfg();
    cfg.relation_deter_weight = 0.0f;  // isolate the reputation mechanic
    add_polity(w, 100, 0, /*pop=*/50000, 1.0f);
    add_polity(w, 200, 1, /*pop=*/50000, 1.0f);
    add_polity(w, 300, 2, /*pop=*/50000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[0].links.push_back(link_to(300));
    w.provinces[1].links.push_back(link_to(100));
    w.provinces[2].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    for (uint32_t y = 1; y <= 20; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
    }
    CHECK(mod.relation(0, 1) >= cfg.ally_threshold);
    CHECK(mod.relation(0, 2) >= cfg.ally_threshold);
    const float ac_before = mod.relation(0, 2);

    w.provinces[0].cohort_stats->total_population = 100000;
    w.provinces[2].cohort_stats->total_population = 100000;  // C stays unattackable
    w.current_tick = 21 * 365;
    DeltaBuffer d2{};
    mod.execute(w, d2);
    apply_deltas(w, d2);
    CHECK(w.provinces[1].cohort_stats->war_death_fraction > 0.0f);   // B betrayed
    CHECK(w.provinces[2].cohort_stats->war_death_fraction == 0.0f);  // C untouched
    CHECK(mod.relation(0, 2) < ac_before);  // the pariah brand
}

// ─────────────────────────────────────────────────────────────────────────────
// Polities: absorption, pooling, secession, conquerors
// ─────────────────────────────────────────────────────────────────────────────
namespace {
// Run annual passes until the member province joins the seat's polity (battle
// outcomes are stochastic per seed; absorption needs absorb_after_wins VICTORIES).
uint32_t years_to_absorb(WarfareModule& mod, WorldState& w, uint32_t member, uint32_t seat,
                         uint32_t start_year, uint32_t max_year) {
    for (uint32_t y = start_year; y <= max_year; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
        if (mod.polity_of(member) == seat)
            return y;
    }
    return 0;
}
}  // namespace

TEST_CASE("warfare: repeated victories absorb the loser's polity (empire peace inside)",
          "[warfare][polity][tier2]") {
    WorldState w = dawn_world(0);
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f);
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    CHECK(mod.polity_of(0) == 0);
    CHECK(mod.polity_of(1) == 1);
    const uint32_t absorbed_year = years_to_absorb(mod, w, 1, 0, 1, 10);
    REQUIRE(absorbed_year > 0);
    REQUIRE(absorbed_year >= cfg.absorb_after_wins);  // needs that many victories

    // Empire peace: members never war each other, however lopsided the power.
    w.current_tick = (absorbed_year + 1) * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(w.provinces[1].cohort_stats->war_death_fraction == 0.0f);
}

TEST_CASE("warfare: polity members pool power — the kingdom deters what a lone province cannot",
          "[warfare][polity][tier2]") {
    WorldState w = dawn_world(0);
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.0f);  // A: seat
    add_polity(w, 200, 1, /*pop=*/10000, 1.0f);   // B: member-to-be (border)
    add_polity(w, 300, 2, /*pop=*/30000, 1.0f);   // C: would-be raider of B
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));
    w.provinces[1].links.push_back(link_to(300));
    w.provinces[2].links.push_back(link_to(200));

    WarfareModule mod(cfg);
    REQUIRE(years_to_absorb(mod, w, 1, 0, 1, 10) > 0);

    // C (levy 3000) can no longer touch B: B fields the KINGDOM's pooled strength
    // (levy 11,000 x forage = 5,500), so C's raid fails the gate — and the same
    // pooled strength is what strikes C through the border member B. C's losses
    // match the POOLED-army prediction exactly: numbers only a kingdom can field.
    w.current_tick = 12 * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    const float S_kingdom = (10000.0f + 1000.0f) * cfg.forage_share;
    const float dead_c_frac = cfg.battle_lethality * S_kingdom / 30000.0f;
    CHECK_THAT(w.provinces[2].cohort_stats->war_death_fraction, WithinRel(dead_c_frac, 0.02f));
    // And B, the border member, bears only attacker-scale losses from C's defence —
    // it is the empire's spear, not C's victim.
    const float S_c_def = 3000.0f * cfg.forage_share;
    const float dead_b_frac = cfg.battle_lethality * S_c_def / 10000.0f;
    CHECK_THAT(w.provinces[1].cohort_stats->war_death_fraction, WithinRel(dead_b_frac, 0.02f));
}

TEST_CASE("warfare: a member that outgrows the polity secedes (the hold problem)",
          "[warfare][polity][tier2]") {
    WorldState w = dawn_world(0);
    WarfareConfig cfg = sure_cfg();
    add_polity(w, 100, 0, /*pop=*/100000, 1.5f);
    add_polity(w, 200, 1, /*pop=*/4000, 1.0f);
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    const uint32_t absorbed_year = years_to_absorb(mod, w, 1, 0, 1, 10);
    REQUIRE(absorbed_year > 0);

    // The centre collapses: it can no longer overawe the member — secession.
    w.provinces[0].cohort_stats->total_population = 1000;
    w.current_tick = (absorbed_year + 1) * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK(mod.polity_of(1) == 1);

    // And the polity map + diplomacy survive save/load.
    w.provinces[0].cohort_stats->total_population = 100000;
    const uint32_t reabsorbed =
        years_to_absorb(mod, w, 1, 0, absorbed_year + 2, absorbed_year + 14);
    REQUIRE(reabsorbed > 0);
    std::vector<uint8_t> blob;
    mod.serialize_state(blob);
    WarfareModule fresh(cfg);
    CHECK(fresh.polity_of(1) == 1);
    REQUIRE(fresh.deserialize_state(blob.data(), blob.size()));
    CHECK(fresh.polity_of(1) == 0);
}

namespace {
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
    b.u32(3u);
    b.bytes.push_back(0u);
    b.u32(0u);  // relations: none
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
    WorldState w = dawn_world(0);
    WarfareConfig cfg = sure_cfg();
    cfg.absorb_after_wins = 1;  // a decisive campaign
    add_polity(w, 100, 0, /*pop=*/40000, 1.0f);  // A: small Macedon (levy 4000)
    add_polity(w, 200, 1, /*pop=*/48000, 1.0f);  // B: the bigger neighbour (levy 4800)
    w.provinces[0].links.push_back(link_to(200));
    w.provinces[1].links.push_back(link_to(100));

    WarfareModule mod(cfg);
    // Raw strength could never attack: 2000 < 1.3 x 2400. Inject a great commander
    // on A's seat (tenure through year 12): x2.5 -> the conquest qualifies.
    auto blob = make_warfare_blob({}, {{0u, 12u}}, {});
    REQUIRE(mod.deserialize_state(blob.data(), blob.size()));

    const uint32_t conquered = years_to_absorb(mod, w, 1, 0, 1, 8);
    REQUIRE(conquered > 0);  // the leadership surge conquers
    CHECK(mod.has_leader(0, conquered));

    // While the commander lives, the inflated centre overawes the member.
    for (uint32_t y = conquered + 1; y <= 11; ++y) {
        w.current_tick = y * 365;
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
        CHECK(mod.polity_of(1) == 0);
    }

    // Year 12: the tenure ends; the centre's strength collapses back; the young
    // conquest (low cohesion) out-powers it and secedes — the Diadochi.
    w.current_tick = 12 * 365;
    DeltaBuffer d{};
    mod.execute(w, d);
    apply_deltas(w, d);
    CHECK_FALSE(mod.has_leader(0, 12));
    CHECK(mod.polity_of(1) == 1);
}

TEST_CASE("warfare: the Genghis reach — a steppe polity strikes past the grain radius",
          "[warfare][conqueror][tier2]") {
    // Chain A - B - C: a farming A's supply line to the 2-hop target C pays the ox
    // law on both legs (path ~0.25) and arrives too weak; a STEPPE A (herd-fed
    // cavalry) pays nothing and strikes.
    auto run = [](float arable) {
        WorldState w = dawn_world();
        WarfareConfig cfg = sure_cfg();
        add_polity(w, 100, 0, /*pop=*/50000, 1.0f);
        add_polity(w, 200, 1, /*pop=*/45000, 1.0f);  // buffer: deterred by C's size
        add_polity(w, 300, 2, /*pop=*/36000, 1.0f);  // only A's 50k clears 1.3 x 36k
        w.provinces[0].geography.arable_land_fraction = arable;
        w.provinces[0].geography.forest_coverage = 0.1f;
        w.provinces[0].links.push_back(link_to(200));
        w.provinces[1].links.push_back(link_to(100));
        w.provinces[1].links.push_back(link_to(300));
        w.provinces[2].links.push_back(link_to(200));
        WarfareModule mod(cfg);
        DeltaBuffer d{};
        mod.execute(w, d);
        apply_deltas(w, d);
        return w.provinces[2].cohort_stats->war_death_fraction;
    };
    CHECK(run(0.8f) == 0.0f);  // farmland: C is beyond a supplyable strike
    CHECK(run(0.05f) > 0.0f);  // steppe cavalry: the ox does not bind the herd-fed
}

TEST_CASE("warfare: the Rome hold — integration needs tenure AND a route for administration",
          "[warfare][conqueror][tier2]") {
    auto run = [](bool long_held, bool with_route) {
        WorldState w = dawn_world(0);
        WarfareConfig cfg = sure_cfg();
        cfg.base_aggression_prob = 0.0f;  // isolate the hold check
        add_polity(w, 100, 0, /*pop=*/55000, 1.0f);
        add_polity(w, 200, 1, /*pop=*/50000, 1.0f);
        if (with_route) {
            // A river link: administration (like grain) moves near-losslessly.
            ProvinceLink l = link_to(200);
            l.type = LinkType::River;
            w.provinces[0].links.push_back(l);
            ProvinceLink r = link_to(100);
            r.type = LinkType::River;
            w.provinces[1].links.push_back(r);
        }
        WarfareModule mod(cfg);
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
    // Borderline member (levy 5000 vs rest 5500): a fresh conquest secedes; the
    // province held 150 years WITH a route is integrated and stays; the same tenure
    // WITHOUT any route to its polity never integrated — no road to Rome, no Rome.
    CHECK(run(false, true) == 1);  // fresh: secedes
    CHECK(run(true, true) == 0);   // old + river route: holds
    CHECK(run(true, false) == 1);  // old but unreachable: never integrated, secedes
}
