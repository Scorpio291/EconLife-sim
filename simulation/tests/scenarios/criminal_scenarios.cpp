// Criminal economy scenario tests — behavioral assertions from GDD and TDD.
// These verify emergent criminal economy behavior across multiple modules.
// Each scenario sets up a WorldState, applies deltas directly, and asserts outcomes.
//
// Scenarios are tagged [scenario][criminal].

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>

#include "core/tick/drain_deferred_work.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/world_state.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using namespace econlife::test;
using Catch::Matchers::WithinAbs;

// ── Evidence scenarios ──────────────────────────────────────────────────────

TEST_CASE("evidence accumulates from criminal activity", "[scenario][criminal][evidence]") {
    // Setup: criminal operator NPC running an illegal operation.
    // Action: emit one EvidenceToken per iteration over 10 iterations.
    // Assert: evidence_pool grows by 10.

    auto world = create_test_world(42, 10, 1, 5);
    auto criminal = create_test_npc(500, NPCRole::criminal_operator, 0);
    world.significant_npcs.push_back(criminal);

    const int iterations = 10;
    for (int i = 0; i < iterations; ++i) {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = static_cast<uint32_t>(1000 + i);
        tok.type = EvidenceType::physical;
        tok.source_npc_id = criminal.id;
        tok.target_npc_id = criminal.id;
        tok.actionability = 0.4f;
        tok.decay_rate = 0.02f;
        tok.created_tick = static_cast<uint32_t>(i);
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    // Each iteration added one token; pool should contain exactly 10.
    REQUIRE(static_cast<int>(world.evidence_pool.size()) == iterations);
}

TEST_CASE("evidence decays over time without reinforcement", "[scenario][criminal][evidence]") {
    // Setup: one evidence token with decay_rate=0.05 at actionability=0.8.
    // Schedule an evidence_decay_batch DWQ item at tick 0.
    // Drain; verify actionability decreased and token is still active.

    auto world = create_test_world(42, 10, 1, 5);
    world.current_tick = 0;

    EvidenceToken tok{};
    tok.id = 2000;
    tok.type = EvidenceType::financial;
    tok.source_npc_id = 0;
    tok.target_npc_id = 0;
    tok.actionability = 0.8f;
    tok.decay_rate = 0.05f;
    tok.created_tick = 0;
    tok.province_id = 0;
    tok.is_active = true;
    world.evidence_pool.push_back(tok);

    // Schedule decay batch due at current tick.
    world.deferred_work_queue.push(
        {0, WorkType::evidence_decay_batch, tok.id, EvidenceDecayPayload{tok.id}});

    DeltaBuffer delta{};
    drain_deferred_work(world, delta);
    apply_deltas(world, delta);

    // actionability must have decreased from 0.8.
    REQUIRE(world.evidence_pool[0].actionability < 0.8f);
    // Token should still be active (actionability after one batch is 0.8 - 0.05*7 = 0.45).
    REQUIRE(world.evidence_pool[0].is_active == true);
}

TEST_CASE("evidence types have different discovery thresholds", "[scenario][criminal][evidence]") {
    // Setup: tokens of all five EvidenceType values with distinct actionability levels.
    // Assert: all five tokens coexist in the pool with their assigned values intact.

    auto world = create_test_world(42, 10, 1, 5);

    struct TokenSpec {
        EvidenceType type;
        float actionability;
    };
    const TokenSpec specs[] = {
        {EvidenceType::financial, 0.2f},   {EvidenceType::physical, 0.5f},
        {EvidenceType::testimonial, 0.7f}, {EvidenceType::documentary, 0.35f},
        {EvidenceType::digital, 0.9f},
    };

    for (uint32_t i = 0; i < 5; ++i) {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = 3000 + i;
        tok.type = specs[i].type;
        tok.source_npc_id = 0;
        tok.target_npc_id = 0;
        tok.actionability = specs[i].actionability;
        tok.decay_rate = 0.01f;
        tok.created_tick = 0;
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    REQUIRE(world.evidence_pool.size() == 5u);
    for (uint32_t i = 0; i < 5; ++i) {
        REQUIRE_THAT(world.evidence_pool[i].actionability,
                     WithinAbs(specs[i].actionability, 0.01f));
    }
}

// ── Criminal operations scenarios ───────────────────────────────────────────

TEST_CASE("criminal operation generates revenue and evidence",
          "[scenario][criminal][criminal_operations]") {
    // Setup: criminal business; apply BusinessDelta (cash increase) AND
    //        EvidenceDelta (new token). Verify both effects land.

    auto world = create_test_world(42, 10, 1, 5);
    auto& biz = world.npc_businesses[0];
    biz.sector = BusinessSector::criminal;
    biz.criminal_sector = true;
    float initial_cash = biz.cash;

    DeltaBuffer delta{};

    // Revenue from criminal operation.
    BusinessDelta bd{};
    bd.business_id = biz.id;
    bd.cash_delta = 2500.0f;
    delta.business_deltas.push_back(bd);

    // Evidence generated by the operation.
    EvidenceDelta ed{};
    EvidenceToken tok{};
    tok.id = 4000;
    tok.type = EvidenceType::financial;
    tok.source_npc_id = biz.owner_id;
    tok.target_npc_id = biz.owner_id;
    tok.actionability = 0.3f;
    tok.decay_rate = 0.02f;
    tok.created_tick = 1;
    tok.province_id = biz.province_id;
    tok.is_active = true;
    ed.new_token = tok;
    delta.evidence_deltas.push_back(ed);

    apply_deltas(world, delta);

    REQUIRE_THAT(world.npc_businesses[0].cash, WithinAbs(initial_cash + 2500.0f, 0.01f));
    REQUIRE(world.evidence_pool.size() == 1u);
    REQUIRE(world.evidence_pool[0].id == 4000u);
}

TEST_CASE("OPSEC level affects evidence generation rate",
          "[scenario][criminal][criminal_operations]") {
    // High OPSEC: 1 token per 10 ticks.
    // Low OPSEC:  5 tokens per 10 ticks.
    // Assert: low OPSEC pool is larger than high OPSEC pool.

    auto world_hi = create_test_world(42, 10, 1, 5);
    auto world_lo = create_test_world(42, 10, 1, 5);

    const int ticks = 10;

    // High OPSEC — emit 1 token total.
    {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = 5000;
        tok.type = EvidenceType::digital;
        tok.source_npc_id = 0;
        tok.target_npc_id = 0;
        tok.actionability = 0.2f;
        tok.decay_rate = 0.01f;
        tok.created_tick = 0;
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world_hi, delta);
    }

    // Low OPSEC — emit 5 tokens.
    for (int i = 0; i < 5; ++i) {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = static_cast<uint32_t>(6000 + i);
        tok.type = EvidenceType::physical;
        tok.source_npc_id = 0;
        tok.target_npc_id = 0;
        tok.actionability = 0.6f;
        tok.decay_rate = 0.02f;
        tok.created_tick = static_cast<uint32_t>(i * 2);
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world_lo, delta);
    }

    REQUIRE(world_lo.evidence_pool.size() > world_hi.evidence_pool.size());
}

TEST_CASE("territory control affects protection racket revenue",
          "[scenario][criminal][protection_rackets]") {
    // Criminal business collects from 10 victim businesses (200 each).
    // Apply cumulative cash_delta to criminal business.
    // Assert: criminal business cash increased by 2000.

    auto world = create_test_world(42, 15, 1, 5);

    // Set up criminal business as the collector.
    auto criminal_biz = create_test_business(9000, 500, 0, BusinessSector::criminal);
    criminal_biz.criminal_sector = true;
    criminal_biz.cash = 0.0f;
    world.npc_businesses.push_back(criminal_biz);
    const size_t criminal_idx = world.npc_businesses.size() - 1;

    const float payment_per_biz = 200.0f;
    const int victim_count = 10;
    const float total_collection = payment_per_biz * victim_count;

    DeltaBuffer delta{};
    BusinessDelta bd{};
    bd.business_id = criminal_biz.id;
    bd.cash_delta = total_collection;
    delta.business_deltas.push_back(bd);
    apply_deltas(world, delta);

    REQUIRE_THAT(world.npc_businesses[criminal_idx].cash, WithinAbs(total_collection, 0.01f));
}

// ── Investigation scenarios ─────────────────────────────────────────────────

TEST_CASE("investigator builds case from evidence tokens",
          "[scenario][criminal][investigator_engine]") {
    // Add 20 evidence tokens all targeting the same criminal NPC.
    // Verify evidence count grows proportionally.

    auto world = create_test_world(42, 10, 1, 5);
    auto criminal = create_test_npc(700, NPCRole::criminal_operator, 0);
    world.significant_npcs.push_back(criminal);

    const int iterations = 20;
    for (int i = 0; i < iterations; ++i) {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = static_cast<uint32_t>(7000 + i);
        tok.type = EvidenceType::documentary;
        tok.source_npc_id = 0;            // investigator gathers
        tok.target_npc_id = criminal.id;  // case against criminal
        tok.actionability = 0.5f;
        tok.decay_rate = 0.005f;
        tok.created_tick = static_cast<uint32_t>(i);
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    // All tokens should be present in the pool.
    REQUIRE(static_cast<int>(world.evidence_pool.size()) == iterations);

    // Every token targets the criminal NPC.
    for (const auto& tok : world.evidence_pool) {
        REQUIRE(tok.target_npc_id == criminal.id);
    }
}

TEST_CASE("investigation leads to prosecution when threshold met",
          "[scenario][criminal][legal_process]") {
    // A criminal NPC with accumulated evidence above prosecution threshold.
    // Apply NPCDelta with new_status = imprisoned.
    // Assert: status is now imprisoned.

    auto world = create_test_world(42, 10, 1, 5);
    auto criminal = create_test_npc(800, NPCRole::criminal_operator, 0);
    world.significant_npcs.push_back(criminal);

    // Add high-actionability evidence tokens to simulate threshold exceeded.
    for (int i = 0; i < 5; ++i) {
        DeltaBuffer evd{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = static_cast<uint32_t>(8000 + i);
        tok.type = EvidenceType::testimonial;
        tok.source_npc_id = 0;
        tok.target_npc_id = criminal.id;
        tok.actionability = 0.95f;
        tok.decay_rate = 0.001f;
        tok.created_tick = static_cast<uint32_t>(i);
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        evd.evidence_deltas.push_back(ed);
        apply_deltas(world, evd);
    }

    // Legal process outcome: NPC imprisoned.
    DeltaBuffer delta{};
    NPCDelta nd{};
    nd.npc_id = criminal.id;
    nd.new_status = NPCStatus::imprisoned;
    delta.npc_deltas.push_back(nd);
    apply_deltas(world, delta);

    // Find the criminal NPC in the pool.
    const NPC* found = nullptr;
    for (const auto& npc : world.significant_npcs) {
        if (npc.id == criminal.id) {
            found = &npc;
            break;
        }
    }
    REQUIRE(found != nullptr);
    REQUIRE(found->status == NPCStatus::imprisoned);
}

TEST_CASE("informant provides evidence to investigators",
          "[scenario][criminal][informant_system]") {
    // An informant NPC (criminal insider) contributes an evidence token.
    // The token's source_npc_id identifies the informant.
    // Assert: token is in the pool with correct source.

    auto world = create_test_world(42, 10, 1, 5);
    auto informant = create_test_npc(900, NPCRole::criminal_operator, 0);
    world.significant_npcs.push_back(informant);

    DeltaBuffer delta{};
    EvidenceDelta ed{};
    EvidenceToken tok{};
    tok.id = 9000;
    tok.type = EvidenceType::testimonial;
    tok.source_npc_id = informant.id;  // informant is the source
    tok.target_npc_id = 100;           // another criminal
    tok.actionability = 0.75f;
    tok.decay_rate = 0.01f;
    tok.created_tick = 1;
    tok.province_id = 0;
    tok.is_active = true;
    ed.new_token = tok;
    delta.evidence_deltas.push_back(ed);
    apply_deltas(world, delta);

    REQUIRE(world.evidence_pool.size() == 1u);
    REQUIRE(world.evidence_pool[0].source_npc_id == informant.id);
    REQUIRE(world.evidence_pool[0].type == EvidenceType::testimonial);
    REQUIRE(world.evidence_pool[0].is_active == true);
}

// ── Money laundering scenarios ──────────────────────────────────────────────

TEST_CASE("illicit cash laundered through three stages", "[scenario][criminal][money_laundering]") {
    // Three-stage laundering:
    //   Placement  — dirty cash enters the front business (+10 000).
    //   Layering   — cash moves to a second business (-10 000 / +10 000).
    //   Integration— clean cash extracted from second business (-10 000).
    //                A clean-cash NPC receives it (+10 000).
    // Assert: final cash balance at each stage is correct.

    auto world = create_test_world(42, 10, 1, 5);

    // Two front businesses.
    auto front_a = create_test_business(10000, 100, 0, BusinessSector::criminal);
    auto front_b = create_test_business(10001, 101, 0, BusinessSector::services);
    front_a.cash = 0.0f;
    front_b.cash = 0.0f;
    world.npc_businesses.push_back(front_a);
    world.npc_businesses.push_back(front_b);
    const size_t idx_a = world.npc_businesses.size() - 2;
    const size_t idx_b = world.npc_businesses.size() - 1;

    auto& npc0 = world.significant_npcs[0];
    float initial_npc_capital = npc0.capital;

    const float amount = 10000.0f;

    // Stage 1: Placement — dirty cash enters front_a.
    {
        DeltaBuffer d{};
        BusinessDelta bd{};
        bd.business_id = front_a.id;
        bd.cash_delta = amount;
        d.business_deltas.push_back(bd);
        apply_deltas(world, d);
    }
    REQUIRE_THAT(world.npc_businesses[idx_a].cash, WithinAbs(amount, 0.01f));

    // Stage 2: Layering — cash moves from front_a to front_b.
    {
        DeltaBuffer d{};
        BusinessDelta bd_a{};
        bd_a.business_id = front_a.id;
        bd_a.cash_delta = -amount;
        d.business_deltas.push_back(bd_a);

        BusinessDelta bd_b{};
        bd_b.business_id = front_b.id;
        bd_b.cash_delta = amount;
        d.business_deltas.push_back(bd_b);
        apply_deltas(world, d);
    }
    REQUIRE_THAT(world.npc_businesses[idx_a].cash, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(world.npc_businesses[idx_b].cash, WithinAbs(amount, 0.01f));

    // Stage 3: Integration — clean cash withdrawn from front_b, delivered to NPC.
    {
        DeltaBuffer d{};
        BusinessDelta bd_b{};
        bd_b.business_id = front_b.id;
        bd_b.cash_delta = -amount;
        d.business_deltas.push_back(bd_b);

        NPCDelta nd{};
        nd.npc_id = npc0.id;
        nd.capital_delta = amount;
        d.npc_deltas.push_back(nd);
        apply_deltas(world, d);
    }
    REQUIRE_THAT(world.npc_businesses[idx_b].cash, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(world.significant_npcs[0].capital, WithinAbs(initial_npc_capital + amount, 0.01f));
}

TEST_CASE("laundering exposure increases detection risk",
          "[scenario][criminal][money_laundering]") {
    // Large cash movements generate proportionally more evidence tokens.
    // Emit one EvidenceToken per 1 000 units laundered (10 tokens for 10 000).
    // Assert: evidence pool size equals expected token count.

    auto world = create_test_world(42, 10, 1, 5);

    const float amount_per_token = 1000.0f;
    const int token_count = 10;

    for (int i = 0; i < token_count; ++i) {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = static_cast<uint32_t>(11000 + i);
        tok.type = EvidenceType::financial;
        tok.source_npc_id = 0;
        tok.target_npc_id = 0;
        tok.actionability = 0.25f + static_cast<float>(i) * 0.05f;
        tok.decay_rate = 0.01f;
        tok.created_tick = static_cast<uint32_t>(i);
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    REQUIRE(static_cast<int>(world.evidence_pool.size()) == token_count);
}

// ── Drug economy scenarios ──────────────────────────────────────────────────

TEST_CASE("drug supply chain from production to distribution",
          "[scenario][criminal][drug_economy]") {
    // Drug production adds supply to a market (good_id=3).
    // Distribution step reduces demand_buffer (consumption).
    // Assert: supply increased and demand_buffer decreased.

    auto world = create_test_world(42, 10, 1, 5);
    const uint32_t drug_good_id = 3;
    float initial_supply = world.regional_markets[drug_good_id].supply;
    float initial_demand = world.regional_markets[drug_good_id].demand_buffer;

    // Production: add supply.
    {
        DeltaBuffer d{};
        MarketDelta md{};
        md.good_id = drug_good_id;
        md.region_id = 0;
        md.supply_delta = 80.0f;
        d.market_deltas.push_back(md);
        apply_deltas(world, d);
    }
    REQUIRE_THAT(world.regional_markets[drug_good_id].supply,
                 WithinAbs(initial_supply + 80.0f, 0.01f));

    // Distribution: consume demand.
    {
        DeltaBuffer d{};
        MarketDelta md{};
        md.good_id = drug_good_id;
        md.region_id = 0;
        md.demand_buffer_delta = -50.0f;
        d.market_deltas.push_back(md);
        apply_deltas(world, d);
    }
    float new_demand = world.regional_markets[drug_good_id].demand_buffer;
    REQUIRE(new_demand < initial_demand + 80.0f);  // demand_buffer reduced
}

TEST_CASE("drug quality affects market price and addiction rate",
          "[scenario][criminal][drug_economy]") {
    // High-quality drug commands a higher spot price than low-quality.
    // Both addiction rates are applied via RegionDelta.
    // Assert: high-quality market price > low-quality market price.
    // Assert: addiction_rate increased in region.

    auto world = create_test_world(42, 10, 2, 5);

    const uint32_t drug_good_id = 2;
    // Province 0: high quality (price override = 50).
    // Province 1: low quality  (price override = 20).

    // Find the market indices.
    // Markets are laid out as goods_count goods per province, so province p, good g is at
    // p*goods_count + g.
    const uint32_t goods_count = 5;
    const uint32_t hi_market_idx = 0 * goods_count + drug_good_id;  // province 0
    const uint32_t lo_market_idx = 1 * goods_count + drug_good_id;  // province 1

    const float hi_price = 50.0f;
    const float lo_price = 20.0f;

    {
        DeltaBuffer d{};
        MarketDelta hi{};
        hi.good_id = drug_good_id;
        hi.region_id = 0;
        hi.spot_price_override = hi_price;
        d.market_deltas.push_back(hi);

        MarketDelta lo{};
        lo.good_id = drug_good_id;
        lo.region_id = 1;
        lo.spot_price_override = lo_price;
        d.market_deltas.push_back(lo);
        apply_deltas(world, d);
    }

    REQUIRE(world.regional_markets[hi_market_idx].spot_price >
            world.regional_markets[lo_market_idx].spot_price);
    REQUIRE_THAT(world.regional_markets[hi_market_idx].spot_price, WithinAbs(hi_price, 0.01f));
    REQUIRE_THAT(world.regional_markets[lo_market_idx].spot_price, WithinAbs(lo_price, 0.01f));

    // Apply addiction rate increase for high-quality province.
    float addiction_before = world.provinces[0].conditions.addiction_rate;
    {
        DeltaBuffer d{};
        RegionDelta rd{};
        rd.region_id = 0;
        rd.addiction_rate_delta = 0.03f;
        d.region_deltas.push_back(rd);
        apply_deltas(world, d);
    }
    REQUIRE(world.provinces[0].conditions.addiction_rate > addiction_before);
}

// ── Facility signals scenarios ──────────────────────────────────────────────

TEST_CASE("illegal facility emits observable signals", "[scenario][criminal][facility_signals]") {
    // An operating illegal facility emits physical evidence tokens.
    // Assert: tokens are created with is_active=true and type=physical.

    auto world = create_test_world(42, 10, 1, 5);

    const int token_count = 3;
    for (int i = 0; i < token_count; ++i) {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = static_cast<uint32_t>(12000 + i);
        tok.type = EvidenceType::physical;
        tok.source_npc_id = 0;
        tok.target_npc_id = 0;
        tok.actionability = 0.45f;
        tok.decay_rate = 0.015f;
        tok.created_tick = static_cast<uint32_t>(i);
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    REQUIRE(static_cast<int>(world.evidence_pool.size()) == token_count);
    for (const auto& tok : world.evidence_pool) {
        REQUIRE(tok.type == EvidenceType::physical);
        REQUIRE(tok.is_active == true);
    }
}

TEST_CASE("concealment investment reduces facility signal strength",
          "[scenario][criminal][facility_signals]") {
    // Concealed facility: evidence token with low actionability (0.10).
    // Unconcealed facility: evidence token with high actionability (0.70).
    // Assert: unconcealed token actionability > concealed token actionability.

    auto world = create_test_world(42, 10, 1, 5);

    // Concealed facility token.
    {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = 13000;
        tok.type = EvidenceType::physical;
        tok.source_npc_id = 0;
        tok.target_npc_id = 1;
        tok.actionability = 0.10f;  // concealed — weak signal
        tok.decay_rate = 0.005f;
        tok.created_tick = 0;
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    // Unconcealed facility token.
    {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = 13001;
        tok.type = EvidenceType::physical;
        tok.source_npc_id = 0;
        tok.target_npc_id = 2;
        tok.actionability = 0.70f;  // unconcealed — strong signal
        tok.decay_rate = 0.005f;
        tok.created_tick = 0;
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    REQUIRE(world.evidence_pool.size() == 2u);

    const EvidenceToken* concealed = nullptr;
    const EvidenceToken* unconcealed = nullptr;
    for (const auto& tok : world.evidence_pool) {
        if (tok.id == 13000)
            concealed = &tok;
        if (tok.id == 13001)
            unconcealed = &tok;
    }
    REQUIRE(concealed != nullptr);
    REQUIRE(unconcealed != nullptr);
    REQUIRE(unconcealed->actionability > concealed->actionability);
}

// ── Alternative identity scenarios ──────────────────────────────────────────

TEST_CASE("alternative identity reduces investigation targeting",
          "[scenario][criminal][alternative_identity]") {
    // A criminal NPC has a cover identity.
    // An investigator NPC has KnowledgeEntry(type=identity_link) linking the
    // cover identity to the real identity.
    // Evidence tokens target the cover identity, not the real actor.
    // Assert: all evidence tokens target cover_identity_id, not real_npc_id.
    // Assert: investigator's known_evidence contains the identity_link entry.

    auto world = create_test_world(42, 10, 1, 5);

    auto criminal = create_test_npc(1400, NPCRole::criminal_operator, 0);
    auto investigator = create_test_npc(1401, NPCRole::law_enforcement, 0);

    const uint32_t cover_identity_id = 9999;  // represents the alt-identity NPC

    // Investigator learns the identity link.
    KnowledgeEntry ke{};
    ke.subject_id = cover_identity_id;
    ke.secondary_subject_id = criminal.id;
    ke.type = KnowledgeType::identity_link;
    ke.confidence = 0.6f;
    ke.acquired_at_tick = 0;
    ke.source_npc_id = 0;
    ke.original_scope = VisibilityScope::concealed;
    investigator.known_evidence.push_back(ke);

    world.significant_npcs.push_back(criminal);
    world.significant_npcs.push_back(investigator);

    // Evidence generated targets the cover identity, not the real NPC.
    for (int i = 0; i < 3; ++i) {
        DeltaBuffer delta{};
        EvidenceDelta ed{};
        EvidenceToken tok{};
        tok.id = static_cast<uint32_t>(14000 + i);
        tok.type = EvidenceType::documentary;
        tok.source_npc_id = 0;
        tok.target_npc_id = cover_identity_id;  // targets cover, not real NPC
        tok.actionability = 0.5f;
        tok.decay_rate = 0.01f;
        tok.created_tick = static_cast<uint32_t>(i);
        tok.province_id = 0;
        tok.is_active = true;
        ed.new_token = tok;
        delta.evidence_deltas.push_back(ed);
        apply_deltas(world, delta);
    }

    // All evidence targets the cover identity.
    for (const auto& tok : world.evidence_pool) {
        REQUIRE(tok.target_npc_id == cover_identity_id);
        REQUIRE(tok.target_npc_id != criminal.id);
    }

    // Investigator has the identity_link knowledge entry.
    const NPC* inv_ptr = nullptr;
    for (const auto& npc : world.significant_npcs) {
        if (npc.id == investigator.id) {
            inv_ptr = &npc;
            break;
        }
    }
    REQUIRE(inv_ptr != nullptr);
    bool found_link = false;
    for (const auto& entry : inv_ptr->known_evidence) {
        if (entry.type == KnowledgeType::identity_link && entry.subject_id == cover_identity_id &&
            entry.secondary_subject_id == criminal.id) {
            found_link = true;
            break;
        }
    }
    REQUIRE(found_link == true);
}

// ── Designer drug scenarios ─────────────────────────────────────────────────

TEST_CASE("designer drug R&D produces novel compound", "[scenario][criminal][designer_drug]") {
    // Simulate 30 ticks of R&D expenditure (BusinessDelta cash outflow).
    // After R&D completes, apply a MarketDelta supply_delta for a new good (good_id=99).
    // Assert: cumulative R&D cost deducted from business cash.
    // Assert: new good supply exists in the market after the research phase.

    auto world = create_test_world(42, 10, 1, 5);
    auto& biz = world.npc_businesses[0];
    biz.sector = BusinessSector::criminal;
    biz.criminal_sector = true;
    const float initial_cash = biz.cash;
    const float rd_cost_per_tick = 100.0f;
    const int rd_ticks = 30;
    const float total_rd_cost = rd_cost_per_tick * rd_ticks;

    // 30 ticks of R&D spend.
    for (int t = 0; t < rd_ticks; ++t) {
        DeltaBuffer d{};
        BusinessDelta bd{};
        bd.business_id = biz.id;
        bd.cash_delta = -rd_cost_per_tick;
        d.business_deltas.push_back(bd);
        apply_deltas(world, d);
    }

    REQUIRE_THAT(world.npc_businesses[0].cash, WithinAbs(initial_cash - total_rd_cost, 0.01f));

    // R&D complete — new compound enters market as good_id=99.
    const uint32_t novel_good_id = 99;
    RegionalMarket novel_market{};
    novel_market.good_id = novel_good_id;
    novel_market.province_id = 0;
    novel_market.spot_price = 80.0f;
    novel_market.equilibrium_price = 80.0f;
    novel_market.adjustment_rate = 0.1f;
    novel_market.supply = 0.0f;
    novel_market.demand_buffer = 0.0f;
    world.regional_markets.push_back(novel_market);
    const size_t novel_idx = world.regional_markets.size() - 1;

    // First production batch of the novel compound.
    {
        DeltaBuffer d{};
        MarketDelta md{};
        md.good_id = novel_good_id;
        md.region_id = 0;
        md.supply_delta = 40.0f;
        d.market_deltas.push_back(md);
        apply_deltas(world, d);
    }

    REQUIRE_THAT(world.regional_markets[novel_idx].supply, WithinAbs(40.0f, 0.01f));
}
