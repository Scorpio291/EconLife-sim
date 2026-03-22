// Full-year integration tests — verify cross-system emergent behavior
// over 365 ticks (one in-game year) using direct delta application.
//
// These tests validate that multiple subsystems interacting over time
// produce expected emergent outcomes defined in the GDD.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/world_state.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/player.h"
#include "core/tick/drain_deferred_work.h"
#include "core/tick/tick_orchestrator.h"
#include "core/tick/thread_pool.h"
#include "modules/persistence/persistence_module.h"
#include "modules/register_base_game_modules.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using namespace econlife::test;
using Catch::Matchers::WithinAbs;

// ── Economy equilibrium ─────────────────────────────────────────────────────

TEST_CASE("economy reaches equilibrium over 365 ticks", "[integration][economy]") {
    auto world = create_test_world(42, 100, 3, 10);

    float initial_total_capital = 0.0f;
    for (const auto& npc : world.significant_npcs) {
        initial_total_capital += npc.capital;
    }

    // Simulate one year: each tick, NPCs earn income and spend
    DeterministicRNG rng(world.world_seed);
    for (uint32_t tick = 0; tick < 365; ++tick) {
        DeltaBuffer delta{};

        // Income from businesses
        for (const auto& biz : world.npc_businesses) {
            NPCDelta nd{};
            nd.npc_id = biz.owner_id;
            nd.capital_delta = (biz.revenue_per_tick - biz.cost_per_tick) * 0.5f;
            delta.npc_deltas.push_back(nd);

            BusinessDelta bd{};
            bd.business_id = biz.id;
            bd.cash_delta = biz.revenue_per_tick - biz.cost_per_tick;
            delta.business_deltas.push_back(bd);
        }

        // NPC spending (consumption deduction)
        for (const auto& npc : world.significant_npcs) {
            NPCDelta nd{};
            nd.npc_id = npc.id;
            nd.capital_delta = -(rng.next_float() * 20.0f + 5.0f);
            delta.npc_deltas.push_back(nd);
        }

        // Market supply/demand adjustments toward equilibrium
        for (size_t i = 0; i < world.regional_markets.size(); ++i) {
            const auto& m = world.regional_markets[i];
            float excess = m.supply - m.demand_buffer;
            MarketDelta md{};
            md.good_id = m.good_id;
            md.region_id = m.province_id;
            md.supply_delta = -excess * 0.05f; // Markets correct 5% per tick
            md.demand_buffer_delta = excess * 0.02f;
            delta.market_deltas.push_back(md);
        }

        apply_deltas(world, delta);
        world.current_tick = tick + 1;
    }

    REQUIRE(world.current_tick == 365);

    // Markets should have converged: supply/demand gap narrows
    float max_gap = 0.0f;
    for (const auto& m : world.regional_markets) {
        float gap = std::abs(m.supply - m.demand_buffer);
        if (gap > max_gap) max_gap = gap;
    }
    // After 365 ticks of 5% correction, gaps should be small
    REQUIRE(max_gap < 50.0f);

    // Businesses should have accumulated profit
    for (const auto& biz : world.npc_businesses) {
        // Each business earns ~(500-400)*365 = ~36,500 profit over the year
        // Initial cash was 50k-250k; should have grown
        REQUIRE(biz.cash > 0.0f);
    }
}

// ── Criminal economy cycle ──────────────────────────────────────────────────

TEST_CASE("crime-evidence-enforcement cycle over 365 ticks", "[integration][criminal]") {
    auto world = create_test_world(42, 50, 2, 5);
    float initial_crime = world.provinces[0].conditions.crime_rate;

    // Simulate: criminal operations generate evidence, evidence leads to
    // enforcement, enforcement reduces crime
    for (uint32_t tick = 0; tick < 365; ++tick) {
        DeltaBuffer delta{};
        float crime_rate = world.provinces[0].conditions.crime_rate;

        // Phase 1: Crime rises (ticks 0-120)
        if (tick < 120) {
            RegionDelta rd{};
            rd.region_id = 0;
            rd.crime_rate_delta = 0.002f;
            rd.criminal_dominance_delta = 0.001f;
            delta.region_deltas.push_back(rd);

            // Criminal operations generate evidence
            if (tick % 10 == 0) {
                EvidenceDelta ed{};
                EvidenceToken tok{};
                tok.id = tick + 1;
                tok.type = EvidenceType::financial;
                tok.source_npc_id = 100;
                tok.target_npc_id = 101;
                tok.actionability = 0.5f;
                tok.decay_rate = 0.005f;
                tok.created_tick = tick;
                tok.province_id = 0;
                tok.is_active = true;
                ed.new_token = tok;
                delta.evidence_deltas.push_back(ed);
            }
        }

        // Phase 2: Enforcement kicks in (ticks 120-365)
        if (tick >= 120 && crime_rate > initial_crime) {
            RegionDelta rd{};
            rd.region_id = 0;
            // Enforcement proportional to evidence and crime level
            float enforcement = std::min(0.003f, crime_rate * 0.01f);
            rd.crime_rate_delta = -enforcement;
            rd.criminal_dominance_delta = -enforcement * 0.5f;
            delta.region_deltas.push_back(rd);

            // Evidence decays
            for (auto& tok : world.evidence_pool) {
                if (tok.is_active) {
                    tok.actionability -= tok.decay_rate;
                    if (tok.actionability <= 0.0f) tok.is_active = false;
                }
            }
        }

        apply_deltas(world, delta);
        world.current_tick = tick + 1;
    }

    // Crime rose and then fell — should be lower than peak
    float final_crime = world.provinces[0].conditions.crime_rate;
    // Crime rate should have been reduced by enforcement
    REQUIRE(final_crime < 0.35f); // Should be below peak
    // Some evidence tokens should exist
    REQUIRE(!world.evidence_pool.empty());
}

// ── Community response escalation ───────────────────────────────────────────

TEST_CASE("community response escalation cycle", "[integration][social]") {
    auto world = create_test_world(42, 50, 1, 5);
    float initial_grievance = world.provinces[0].community.grievance_level;

    // Apply grievance-inducing conditions for 180 ticks, then stabilize
    for (uint32_t tick = 0; tick < 365; ++tick) {
        DeltaBuffer delta{};
        RegionDelta rd{};
        rd.region_id = 0;

        if (tick < 180) {
            // Rising inequality and crime drive grievance
            rd.inequality_delta = 0.001f;
            rd.crime_rate_delta = 0.001f;
            rd.grievance_delta = 0.003f;
            rd.cohesion_delta = -0.001f;
        } else {
            // Stabilization: improvements reduce grievance
            rd.stability_delta = 0.001f;
            rd.grievance_delta = -0.002f;
            rd.cohesion_delta = 0.001f;
        }
        delta.region_deltas.push_back(rd);
        apply_deltas(world, delta);
        world.current_tick = tick + 1;
    }

    float final_grievance = world.provinces[0].community.grievance_level;
    // Grievance should have risen during first half and partially recovered
    // After 180 ticks of +0.003 and 185 ticks of -0.002:
    // Net: 180*0.003 - 185*0.002 = 0.54 - 0.37 = +0.17
    REQUIRE(final_grievance > initial_grievance);
    // But should have recovered somewhat from peak
    float peak_grievance = initial_grievance + 180 * 0.003f;
    REQUIRE(final_grievance < peak_grievance);
}

// ── NPC memory and relationship evolution ───────────────────────────────────

TEST_CASE("NPC memory accumulation and cap enforcement", "[integration][npc]") {
    auto world = create_test_world(42, 10, 1, 5);
    auto& npc = world.significant_npcs[0];

    // Add memories until we hit the cap and beyond
    for (uint32_t tick = 0; tick < 600; ++tick) {
        DeltaBuffer delta{};
        NPCDelta nd{};
        nd.npc_id = npc.id;
        MemoryEntry mem{};
        mem.tick_timestamp = tick;
        mem.type = MemoryType::interaction;
        mem.subject_id = 200 + (tick % 50);
        mem.emotional_weight = 0.1f + (tick % 10) * 0.05f;
        mem.decay = 1.0f;
        mem.is_actionable = false;
        nd.new_memory_entry = mem;
        delta.npc_deltas.push_back(nd);
        apply_deltas(world, delta);
    }

    // Memory log should be capped at MAX_MEMORY_ENTRIES
    REQUIRE(npc.memory_log.size() <= MAX_MEMORY_ENTRIES);
    REQUIRE(npc.memory_log.size() == MAX_MEMORY_ENTRIES);
}

// ── Persistence round-trip after full year ───────────────────────────────────

TEST_CASE("persistence round-trip after 365 ticks of mutations", "[integration][persistence]") {
    auto world = create_test_world(42, 50, 2, 5);

    // Apply various deltas for 365 ticks
    DeterministicRNG rng(world.world_seed);
    for (uint32_t tick = 0; tick < 365; ++tick) {
        DeltaBuffer delta{};

        for (uint32_t i = 0; i < 5; ++i) {
            NPCDelta nd{};
            nd.npc_id = 100 + rng.next_uint(50);
            nd.capital_delta = rng.next_float() * 100.0f - 50.0f;
            delta.npc_deltas.push_back(nd);
        }

        for (uint32_t p = 0; p < 2; ++p) {
            RegionDelta rd{};
            rd.region_id = p;
            rd.stability_delta = rng.next_float() * 0.01f - 0.005f;
            rd.crime_rate_delta = rng.next_float() * 0.005f - 0.0025f;
            delta.region_deltas.push_back(rd);
        }

        if (tick % 30 == 0) {
            EvidenceDelta ed{};
            EvidenceToken tok{};
            tok.id = tick / 30 + 1;
            tok.type = EvidenceType::testimonial;
            tok.source_npc_id = 100 + rng.next_uint(50);
            tok.target_npc_id = 100 + rng.next_uint(50);
            tok.actionability = rng.next_float();
            tok.decay_rate = 0.01f;
            tok.created_tick = tick;
            tok.province_id = 0;
            tok.is_active = true;
            ed.new_token = tok;
            delta.evidence_deltas.push_back(ed);
        }

        apply_deltas(world, delta);
        world.current_tick = tick + 1;
    }

    REQUIRE(world.current_tick == 365);
    REQUIRE(!world.evidence_pool.empty());

    // Serialize and deserialize
    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    // Verify key state preserved
    REQUIRE(restored.current_tick == 365);
    REQUIRE(restored.significant_npcs.size() == world.significant_npcs.size());
    REQUIRE(restored.evidence_pool.size() == world.evidence_pool.size());

    for (size_t i = 0; i < world.significant_npcs.size(); ++i) {
        REQUIRE_THAT(restored.significant_npcs[i].capital,
                     WithinAbs(world.significant_npcs[i].capital, 0.01));
    }

    for (size_t i = 0; i < world.provinces.size(); ++i) {
        REQUIRE_THAT(restored.provinces[i].conditions.crime_rate,
                     WithinAbs(world.provinces[i].conditions.crime_rate, 0.001));
    }

    // Serialize again — must be byte-identical (invariant 1)
    auto bytes2 = PersistenceModule::serialize(restored);
    REQUIRE(bytes == bytes2);
}

// ── NaN safety over long runs ───────────────────────────────────────────────

TEST_CASE("no NaN values after 365 ticks of accumulated deltas", "[integration][safety]") {
    auto world = create_test_world(42, 50, 2, 5);
    DeterministicRNG rng(99);

    for (uint32_t tick = 0; tick < 365; ++tick) {
        DeltaBuffer delta{};

        // Apply random deltas that could cause accumulation issues
        for (uint32_t i = 0; i < 10; ++i) {
            NPCDelta nd{};
            nd.npc_id = 100 + rng.next_uint(50);
            // Occasionally produce large values that test clamping
            float val = rng.next_float() * 1000.0f - 500.0f;
            nd.capital_delta = val;
            delta.npc_deltas.push_back(nd);
        }

        for (uint32_t p = 0; p < 2; ++p) {
            RegionDelta rd{};
            rd.region_id = p;
            rd.stability_delta = rng.next_float() * 0.1f - 0.05f;
            rd.crime_rate_delta = rng.next_float() * 0.05f - 0.025f;
            rd.inequality_delta = rng.next_float() * 0.05f - 0.025f;
            rd.addiction_rate_delta = rng.next_float() * 0.02f - 0.01f;
            delta.region_deltas.push_back(rd);
        }

        for (uint32_t g = 0; g < 5; ++g) {
            for (uint32_t p = 0; p < 2; ++p) {
                MarketDelta md{};
                md.good_id = g;
                md.region_id = p;
                md.supply_delta = rng.next_float() * 50.0f - 25.0f;
                md.demand_buffer_delta = rng.next_float() * 30.0f - 15.0f;
                delta.market_deltas.push_back(md);
            }
        }

        apply_deltas(world, delta);
        world.current_tick = tick + 1;
    }

    // Verify no NaN values in any critical field
    for (const auto& npc : world.significant_npcs) {
        REQUIRE_FALSE(std::isnan(npc.capital));
        for (float w : npc.motivations.weights) {
            REQUIRE_FALSE(std::isnan(w));
        }
    }

    for (const auto& m : world.regional_markets) {
        REQUIRE_FALSE(std::isnan(m.spot_price));
        REQUIRE_FALSE(std::isnan(m.supply));
        REQUIRE_FALSE(std::isnan(m.demand_buffer));
        REQUIRE_FALSE(std::isnan(m.equilibrium_price));
    }

    for (const auto& prov : world.provinces) {
        REQUIRE_FALSE(std::isnan(prov.conditions.stability_score));
        REQUIRE_FALSE(std::isnan(prov.conditions.crime_rate));
        REQUIRE_FALSE(std::isnan(prov.conditions.inequality_index));
        REQUIRE_FALSE(std::isnan(prov.conditions.addiction_rate));
        REQUIRE_FALSE(std::isnan(prov.community.grievance_level));
        REQUIRE_FALSE(std::isnan(prov.community.cohesion));
    }

    for (const auto& biz : world.npc_businesses) {
        REQUIRE_FALSE(std::isnan(biz.cash));
        REQUIRE_FALSE(std::isnan(biz.revenue_per_tick));
        REQUIRE_FALSE(std::isnan(biz.cost_per_tick));
    }
}

// ── Region conditions clamping ──────────────────────────────────────────────

TEST_CASE("region conditions stay within valid ranges", "[integration][safety]") {
    auto world = create_test_world(42, 20, 2, 5);

    // Apply extreme deltas that try to push values out of range
    for (uint32_t tick = 0; tick < 100; ++tick) {
        DeltaBuffer delta{};
        for (uint32_t p = 0; p < 2; ++p) {
            RegionDelta rd{};
            rd.region_id = p;
            // Alternate between extreme positive and negative
            float sign = (tick % 2 == 0) ? 1.0f : -1.0f;
            rd.stability_delta = sign * 0.5f;
            rd.crime_rate_delta = sign * 0.3f;
            rd.inequality_delta = sign * 0.2f;
            rd.addiction_rate_delta = sign * 0.1f;
            rd.criminal_dominance_delta = sign * 0.15f;
            rd.grievance_delta = sign * 0.2f;
            rd.cohesion_delta = sign * 0.15f;
            rd.institutional_trust_delta = sign * 0.2f;
            delta.region_deltas.push_back(rd);
        }
        apply_deltas(world, delta);
    }

    // All region conditions should be clamped to valid ranges
    for (const auto& prov : world.provinces) {
        REQUIRE(prov.conditions.stability_score >= 0.0f);
        REQUIRE(prov.conditions.stability_score <= 1.0f);
        REQUIRE(prov.conditions.crime_rate >= 0.0f);
        REQUIRE(prov.conditions.crime_rate <= 1.0f);
        REQUIRE(prov.conditions.inequality_index >= 0.0f);
        REQUIRE(prov.conditions.inequality_index <= 1.0f);
        REQUIRE(prov.conditions.addiction_rate >= 0.0f);
        REQUIRE(prov.conditions.addiction_rate <= 1.0f);
        REQUIRE(prov.conditions.criminal_dominance_index >= 0.0f);
        REQUIRE(prov.conditions.criminal_dominance_index <= 1.0f);
        REQUIRE(prov.community.grievance_level >= 0.0f);
        REQUIRE(prov.community.grievance_level <= 1.0f);
        REQUIRE(prov.community.cohesion >= 0.0f);
        REQUIRE(prov.community.cohesion <= 1.0f);
        REQUIRE(prov.community.institutional_trust >= 0.0f);
        REQUIRE(prov.community.institutional_trust <= 1.0f);
    }
}

// ── Cross-province delta propagation ────────────────────────────────────────

TEST_CASE("cross-province delta buffer one-tick delay", "[integration][cross_province]") {
    auto world = create_test_world(42, 20, 3, 5);

    // Push a cross-province delta
    CrossProvinceDelta cpd{};
    cpd.source_province_id = 0;
    cpd.target_province_id = 1;
    cpd.due_tick = world.current_tick + 1;
    NPCDelta nd{};
    // Pick an NPC in province 1
    nd.npc_id = world.provinces[1].significant_npc_ids[0];
    nd.capital_delta = 5000.0f;
    cpd.npc_delta = nd;
    world.cross_province_delta_buffer.entries.push_back(cpd);

    float capital_before = 0.0f;
    for (const auto& npc : world.significant_npcs) {
        if (npc.id == nd.npc_id) {
            capital_before = npc.capital;
            break;
        }
    }

    // Tick 0: Cross-province buffer has entries
    REQUIRE(!world.cross_province_delta_buffer.entries.empty());

    // Flush cross-province buffer (simulating next tick start)
    // The buffer entries get converted to regular deltas
    DeltaBuffer flush_delta{};
    for (const auto& entry : world.cross_province_delta_buffer.entries) {
        if (entry.npc_delta.has_value()) {
            flush_delta.npc_deltas.push_back(*entry.npc_delta);
        }
    }
    world.cross_province_delta_buffer.entries.clear();
    apply_deltas(world, flush_delta);

    // NPC should now have increased capital
    float capital_after = 0.0f;
    for (const auto& npc : world.significant_npcs) {
        if (npc.id == nd.npc_id) {
            capital_after = npc.capital;
            break;
        }
    }

    REQUIRE_THAT(capital_after - capital_before, WithinAbs(5000.0f, 0.01f));
    REQUIRE(world.cross_province_delta_buffer.entries.empty());
}

// ── Full Orchestrator Integration ───────────────────────────────────────────

TEST_CASE("43 modules register and finalize without cycle", "[integration][orchestrator]") {
    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator);
    REQUIRE(orchestrator.modules().size() == 43);
    orchestrator.finalize_registration();
    REQUIRE(orchestrator.is_finalized());
}

TEST_CASE("full orchestrator tick executes all modules", "[integration][orchestrator]") {
    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator);
    orchestrator.finalize_registration();

    auto world = create_test_world(42, 50, 2, 5);
    ThreadPool pool(1);

    uint32_t tick_before = world.current_tick;
    orchestrator.execute_tick(world, pool);

    REQUIRE(world.current_tick == tick_before + 1);
}

TEST_CASE("orchestrator 30-tick run produces state changes", "[integration][orchestrator]") {
    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator);
    orchestrator.finalize_registration();

    auto world = create_test_world(42, 50, 2, 5);
    ThreadPool pool(1);

    // Snapshot initial state
    float initial_stability = world.provinces[0].conditions.stability_score;
    uint32_t initial_tick = world.current_tick;

    for (int i = 0; i < 30; ++i) {
        orchestrator.execute_tick(world, pool);
    }

    REQUIRE(world.current_tick == initial_tick + 30);

    // After 30 ticks, something should have changed.
    // At minimum, calendar advanced and random events may have fired.
    // We can't predict exact values, but the world should not be identical.
    bool any_change = false;

    // Check stability changed
    if (world.provinces[0].conditions.stability_score != initial_stability) {
        any_change = true;
    }

    // Check if any market prices moved
    for (const auto& m : world.regional_markets) {
        if (m.spot_price != m.equilibrium_price) {
            any_change = true;
            break;
        }
    }

    // Check if calendar entries were added
    if (!world.calendar.empty()) {
        any_change = true;
    }

    REQUIRE(any_change);
}

TEST_CASE("orchestrator determinism: same seed same output", "[integration][orchestrator][determinism]") {
    auto run_30_ticks = [](uint64_t seed) -> WorldState {
        TickOrchestrator orchestrator;
        register_base_game_modules(orchestrator);
        orchestrator.finalize_registration();

        auto world = create_test_world(seed, 50, 2, 5);
        ThreadPool pool(1);

        for (int i = 0; i < 30; ++i) {
            orchestrator.execute_tick(world, pool);
        }
        return world;
    };

    auto state_a = run_30_ticks(12345);
    auto state_b = run_30_ticks(12345);

    REQUIRE(state_a.current_tick == state_b.current_tick);

    // Compare NPC capital values
    REQUIRE(state_a.significant_npcs.size() == state_b.significant_npcs.size());
    for (size_t i = 0; i < state_a.significant_npcs.size(); ++i) {
        REQUIRE(state_a.significant_npcs[i].capital == state_b.significant_npcs[i].capital);
    }

    // Compare market prices
    REQUIRE(state_a.regional_markets.size() == state_b.regional_markets.size());
    for (size_t i = 0; i < state_a.regional_markets.size(); ++i) {
        REQUIRE(state_a.regional_markets[i].spot_price == state_b.regional_markets[i].spot_price);
    }

    // Compare province stability
    for (size_t i = 0; i < state_a.provinces.size(); ++i) {
        REQUIRE(state_a.provinces[i].conditions.stability_score ==
                state_b.provinces[i].conditions.stability_score);
    }
}
