#include "core/world_state/apply_deltas.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

// Helper: create a minimal WorldState with one NPC and one market
static WorldState make_minimal_world() {
    WorldState w{};
    w.current_tick = 10;
    w.world_seed = 42;
    w.game_mode = GameMode::standard;
    w.lod2_price_index.reset();
    w.player.reset();

    Province p{};
    p.id = 0;
    p.region_id = 0;
    p.conditions = {0.7f, 0.3f, 0.1f, 0.05f, 0.05f, 0.7f, 0.8f, 1.0f, 1.0f};
    p.community = {0.6f, 0.2f, 0.6f, 0.5f, 0};
    w.provinces.push_back(p);

    NPC npc{};
    npc.id = 100;
    npc.status = NPCStatus::active;
    npc.capital = 10000.0f;
    npc.risk_tolerance = 0.5f;
    npc.motivations.weights = {0.2f, 0.15f, 0.15f, 0.05f, 0.05f, 0.1f, 0.2f, 0.1f};
    w.significant_npcs.push_back(npc);

    RegionalMarket m{};
    m.good_id = 0;
    m.province_id = 0;
    m.spot_price = 10.0f;
    m.equilibrium_price = 10.0f;
    m.supply = 100.0f;
    m.demand_buffer = 80.0f;
    w.regional_markets.push_back(m);

    return w;
}

TEST_CASE("apply_deltas: NPC capital additive", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    nd.capital_delta = 500.0f;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.significant_npcs[0].capital, WithinAbs(10500.0f, 0.01));
}

TEST_CASE("apply_deltas: NPC capital cannot go negative", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    nd.capital_delta = -20000.0f;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);
    REQUIRE(w.significant_npcs[0].capital == 0.0f);
}

TEST_CASE("apply_deltas: NPC status replacement", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    nd.new_status = NPCStatus::imprisoned;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);
    REQUIRE(w.significant_npcs[0].status == NPCStatus::imprisoned);
}

TEST_CASE("apply_deltas: NPC memory append", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    MemoryEntry mem{};
    mem.tick_timestamp = 10;
    mem.type = MemoryType::event;
    mem.subject_id = 200;
    mem.emotional_weight = -0.5f;
    mem.decay = 1.0f;
    mem.is_actionable = false;
    nd.new_memory_entry = mem;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);
    REQUIRE(w.significant_npcs[0].memory_log.size() == 1);
    REQUIRE(w.significant_npcs[0].memory_log[0].subject_id == 200);
}

TEST_CASE("apply_deltas: NPC memory overflow evicts weakest", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    // Fill memory to capacity
    for (uint32_t i = 0; i < MAX_MEMORY_ENTRIES; ++i) {
        MemoryEntry mem{};
        mem.tick_timestamp = i;
        mem.type = MemoryType::event;
        mem.subject_id = i;
        mem.emotional_weight = 0.1f;
        mem.decay = 0.5f + static_cast<float>(i) * 0.001f;  // increasing decay
        mem.is_actionable = false;
        w.significant_npcs[0].memory_log.push_back(mem);
    }
    REQUIRE(w.significant_npcs[0].memory_log.size() == MAX_MEMORY_ENTRIES);

    // Add one more — should evict the entry with lowest decay (first one, decay=0.5)
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    MemoryEntry new_mem{};
    new_mem.tick_timestamp = 999;
    new_mem.type = MemoryType::interaction;
    new_mem.subject_id = 999;
    new_mem.emotional_weight = 1.0f;
    new_mem.decay = 1.0f;
    new_mem.is_actionable = true;
    nd.new_memory_entry = new_mem;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);
    REQUIRE(w.significant_npcs[0].memory_log.size() == MAX_MEMORY_ENTRIES);
    // New entry should be at the back
    REQUIRE(w.significant_npcs[0].memory_log.back().subject_id == 999);
}

TEST_CASE("apply_deltas: NPC relationship upsert", "[apply_deltas][core]") {
    auto w = make_minimal_world();

    // First: insert a new relationship
    DeltaBuffer delta1{};
    NPCDelta nd1{};
    nd1.npc_id = 100;
    Relationship rel{};
    rel.target_npc_id = 200;
    rel.trust = 0.5f;
    rel.fear = 0.1f;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 10;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 1.0f;
    nd1.updated_relationship = rel;
    delta1.npc_deltas.push_back(nd1);
    apply_deltas(w, delta1);

    REQUIRE(w.significant_npcs[0].relationships.size() == 1);
    REQUIRE_THAT(w.significant_npcs[0].relationships[0].trust, WithinAbs(0.5, 0.01));

    // Second: update existing relationship (trust additive)
    DeltaBuffer delta2{};
    NPCDelta nd2{};
    nd2.npc_id = 100;
    Relationship rel2{};
    rel2.target_npc_id = 200;
    rel2.trust = 0.3f;  // additive
    rel2.fear = 0.0f;
    rel2.obligation_balance = 0.0f;
    rel2.last_interaction_tick = 11;
    rel2.is_movement_ally = false;
    rel2.recovery_ceiling = 1.0f;
    nd2.updated_relationship = rel2;
    delta2.npc_deltas.push_back(nd2);
    apply_deltas(w, delta2);

    REQUIRE(w.significant_npcs[0].relationships.size() == 1);
    REQUIRE_THAT(w.significant_npcs[0].relationships[0].trust, WithinAbs(0.8, 0.01));
}

TEST_CASE("apply_deltas: relationship trust clamped by recovery ceiling", "[apply_deltas][core]") {
    auto w = make_minimal_world();

    // Insert relationship with low recovery ceiling
    Relationship initial{};
    initial.target_npc_id = 200;
    initial.trust = 0.1f;
    initial.fear = 0.0f;
    initial.obligation_balance = 0.0f;
    initial.last_interaction_tick = 5;
    initial.is_movement_ally = false;
    initial.recovery_ceiling = 0.3f;
    w.significant_npcs[0].relationships.push_back(initial);

    // Try to push trust above ceiling
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    Relationship rel{};
    rel.target_npc_id = 200;
    rel.trust = 0.5f;  // would push to 0.6, but ceiling is 0.3
    rel.fear = 0.0f;
    rel.obligation_balance = 0.0f;
    rel.last_interaction_tick = 10;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 0.3f;
    nd.updated_relationship = rel;
    delta.npc_deltas.push_back(nd);
    apply_deltas(w, delta);

    REQUIRE_THAT(w.significant_npcs[0].relationships[0].trust, WithinAbs(0.3, 0.01));
}

TEST_CASE("apply_deltas: market supply additive", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;  // matches province_id for market lookup
    md.supply_delta = 50.0f;
    delta.market_deltas.push_back(md);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.regional_markets[0].supply, WithinAbs(150.0, 0.01));
}

TEST_CASE("apply_deltas: market spot price replacement", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    MarketDelta md{};
    md.good_id = 0;
    md.region_id = 0;
    md.spot_price_override = 25.0f;
    delta.market_deltas.push_back(md);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.regional_markets[0].spot_price, WithinAbs(25.0, 0.01));
}

TEST_CASE("apply_deltas: region stability additive and clamped", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    RegionDelta rd{};
    rd.region_id = 0;
    rd.stability_delta = -0.8f;  // would push 0.7 to -0.1, clamped to 0.0
    delta.region_deltas.push_back(rd);

    apply_deltas(w, delta);
    REQUIRE(w.provinces[0].conditions.stability_score == 0.0f);
}

TEST_CASE("apply_deltas: evidence token append with auto-id", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    EvidenceDelta ed{};
    EvidenceToken token{};
    token.id = 0;  // auto-assign
    token.type = EvidenceType::financial;
    token.actionability = 0.8f;
    token.is_active = true;
    ed.new_token = token;
    delta.evidence_deltas.push_back(ed);

    apply_deltas(w, delta);
    REQUIRE(w.evidence_pool.size() == 1);
    REQUIRE(w.evidence_pool[0].id == 1);  // auto-assigned
    REQUIRE(w.evidence_pool[0].is_active == true);
}

TEST_CASE("apply_deltas: evidence token retired", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    EvidenceToken existing{};
    existing.id = 5;
    existing.is_active = true;
    w.evidence_pool.push_back(existing);

    DeltaBuffer delta{};
    EvidenceDelta ed{};
    ed.retired_token_id = 5;
    delta.evidence_deltas.push_back(ed);

    apply_deltas(w, delta);
    REQUIRE(w.evidence_pool[0].is_active == false);
}

TEST_CASE("apply_deltas: NaN delta treated as zero", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    nd.capital_delta = std::numeric_limits<float>::quiet_NaN();
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.significant_npcs[0].capital, WithinAbs(10000.0, 0.01));
}

TEST_CASE("apply_deltas: delta buffer cleared after application", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = 100;
    nd.capital_delta = 100.0f;
    delta.npc_deltas.push_back(nd);

    apply_deltas(w, delta);
    REQUIRE(delta.npc_deltas.empty());
    REQUIRE(delta.market_deltas.empty());
    REQUIRE(delta.evidence_deltas.empty());
    REQUIRE(delta.region_deltas.empty());
}

TEST_CASE("apply_deltas: obligation node append with auto-id", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};
    ObligationNode node{};
    node.id = 0;
    node.creditor_npc_id = 100;
    node.debtor_npc_id = 200;
    node.favor_type = FavorType::financial_loan;
    node.weight = 0.5f;
    node.created_tick = 10;
    node.is_active = true;
    delta.new_obligation_nodes.push_back(node);

    apply_deltas(w, delta);
    REQUIRE(w.obligation_network.size() == 1);
    REQUIRE(w.obligation_network[0].id == 1);
}

TEST_CASE("apply_deltas: business output_quality update", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    NPCBusiness biz{};
    biz.id = 1;
    biz.cash = 5000.0f;
    biz.output_quality = 0.5f;
    w.npc_businesses.push_back(biz);

    DeltaBuffer delta{};
    BusinessDelta bd{};
    bd.business_id = 1;
    bd.output_quality_update = 0.85f;
    delta.business_deltas.push_back(bd);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.npc_businesses[0].output_quality, WithinAbs(0.85, 0.01));
}

TEST_CASE("apply_deltas: business output_quality clamped to 0-1", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    NPCBusiness biz{};
    biz.id = 1;
    biz.cash = 5000.0f;
    biz.output_quality = 0.5f;
    w.npc_businesses.push_back(biz);

    DeltaBuffer delta{};
    BusinessDelta bd{};
    bd.business_id = 1;
    bd.output_quality_update = 1.5f;  // exceeds 1.0
    delta.business_deltas.push_back(bd);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.npc_businesses[0].output_quality, WithinAbs(1.0, 0.01));
}

TEST_CASE("apply_deltas: currency rate update", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.usd_rate = 5.0f;
    cur.usd_rate_baseline = 5.0f;
    cur.pegged = false;
    cur.foreign_reserves = 0.8f;
    w.currencies.push_back(cur);

    DeltaBuffer delta{};
    CurrencyDelta cd{};
    cd.nation_id = 1;
    cd.usd_rate_update = 4.8f;
    delta.currency_deltas.push_back(cd);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.currencies[0].usd_rate, WithinAbs(4.8, 0.01));
}

TEST_CASE("apply_deltas: currency peg break", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.usd_rate = 3.5f;
    cur.usd_rate_baseline = 3.5f;
    cur.pegged = true;
    cur.peg_rate = 3.5f;
    cur.foreign_reserves = 0.5f;
    w.currencies.push_back(cur);

    DeltaBuffer delta{};
    CurrencyDelta cd{};
    cd.nation_id = 1;
    cd.pegged_update = false;
    delta.currency_deltas.push_back(cd);

    apply_deltas(w, delta);
    REQUIRE(w.currencies[0].pegged == false);
}

TEST_CASE("apply_deltas: currency reserves additive and clamped", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.usd_rate = 1.0f;
    cur.usd_rate_baseline = 1.0f;
    cur.pegged = true;
    cur.foreign_reserves = 0.2f;
    w.currencies.push_back(cur);

    DeltaBuffer delta{};
    CurrencyDelta cd{};
    cd.nation_id = 1;
    cd.foreign_reserves_delta = -0.3f;  // would push to -0.1
    delta.currency_deltas.push_back(cd);

    apply_deltas(w, delta);
    REQUIRE(w.currencies[0].foreign_reserves == 0.0f);  // clamped to 0
}

TEST_CASE("apply_deltas: currency deltas cleared after application", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    CurrencyRecord cur{};
    cur.nation_id = 1;
    cur.usd_rate = 1.0f;
    cur.usd_rate_baseline = 1.0f;
    w.currencies.push_back(cur);

    DeltaBuffer delta{};
    CurrencyDelta cd{};
    cd.nation_id = 1;
    cd.usd_rate_update = 1.1f;
    delta.currency_deltas.push_back(cd);

    apply_deltas(w, delta);
    REQUIRE(delta.currency_deltas.empty());
}

TEST_CASE("apply_deltas: multiple NPC deltas accumulate", "[apply_deltas][core]") {
    auto w = make_minimal_world();
    DeltaBuffer delta{};

    NPCDelta nd1{};
    nd1.npc_id = 100;
    nd1.capital_delta = 100.0f;
    delta.npc_deltas.push_back(nd1);

    NPCDelta nd2{};
    nd2.npc_id = 100;
    nd2.capital_delta = 200.0f;
    delta.npc_deltas.push_back(nd2);

    apply_deltas(w, delta);
    REQUIRE_THAT(w.significant_npcs[0].capital, WithinAbs(10300.0, 0.01));
}
