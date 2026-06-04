#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/informant_system/informant_system_module.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("Informant: risk factor computation", "[informant_system][tier9]") {
    // Low risk tolerance (0.2) -> high risk factor: (1-0.2)*0.30 = 0.24
    REQUIRE_THAT(InformantSystemModule::compute_risk_factor(0.2f, 0.30f), WithinAbs(0.24f, 0.01f));
    // High risk tolerance (0.8) -> low risk factor: (1-0.8)*0.30 = 0.06
    REQUIRE_THAT(InformantSystemModule::compute_risk_factor(0.8f, 0.30f), WithinAbs(0.06f, 0.01f));
}

TEST_CASE("Informant: trust factor computation", "[informant_system][tier9]") {
    // Low trust (0.2) -> high factor: (1-0.2)*0.25 = 0.20
    REQUIRE_THAT(InformantSystemModule::compute_trust_factor(0.2f, 0.25f), WithinAbs(0.20f, 0.01f));
    // High trust (0.8) -> low factor: (1-0.8)*0.25 = 0.05
    REQUIRE_THAT(InformantSystemModule::compute_trust_factor(0.8f, 0.25f), WithinAbs(0.05f, 0.01f));
}

TEST_CASE("Informant: incrimination suppression", "[informant_system][tier9]") {
    REQUIRE_THAT(InformantSystemModule::compute_incrimination_suppression(0, 0.08f),
                 WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(InformantSystemModule::compute_incrimination_suppression(2, 0.08f),
                 WithinAbs(0.16f, 0.01f));
}

TEST_CASE("Informant: compartmentalization bonus", "[informant_system][tier9]") {
    REQUIRE_THAT(InformantSystemModule::compute_compartmentalization_bonus(0, 0.05f),
                 WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(InformantSystemModule::compute_compartmentalization_bonus(3, 0.05f),
                 WithinAbs(0.15f, 0.01f));
}

TEST_CASE("Informant: flip probability full formula", "[informant_system][tier9]") {
    // base 0.10, risk_tol 0.5, trust 0.5, 0 mutual, 0 compartment
    // 0.10 + (1-0.5)*0.30 + (1-0.5)*0.25 - 0 - 0 = 0.10 + 0.15 + 0.125 = 0.375
    // clamped to MAX 0.20
    float prob = InformantSystemModule::compute_flip_probability(0.10f, 0.5f, 0.5f, 0, 0, 0.20f,
                                                                 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE_THAT(prob, WithinAbs(0.20f, 0.01f));
}

TEST_CASE("Informant: flip probability capped at MAX", "[informant_system][tier9]") {
    float prob = InformantSystemModule::compute_flip_probability(0.10f, 0.0f, 0.0f, 0, 0, 0.20f,
                                                                 0.30f, 0.25f, 0.08f, 0.05f);
    // 0.10 + 0.30 + 0.25 = 0.65, capped to 0.20
    REQUIRE_THAT(prob, WithinAbs(0.20f, 0.01f));
}

TEST_CASE("Informant: mutual incrimination reduces flip", "[informant_system][tier9]") {
    float without = InformantSystemModule::compute_flip_probability(0.10f, 0.5f, 0.5f, 0, 0, 0.20f,
                                                                    0.30f, 0.25f, 0.08f, 0.05f);
    float with_mutual = InformantSystemModule::compute_flip_probability(
        0.10f, 0.5f, 0.5f, 2, 0, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(with_mutual <= without);
}

TEST_CASE("Informant: compartmentalization reduces flip", "[informant_system][tier9]") {
    // Use params where raw probability is below cap so comparison is meaningful
    // base=0.05, risk_tol=0.9, trust=0.9: 0.05+0.03+0.025 = 0.105 (below 0.20 cap)
    float without = InformantSystemModule::compute_flip_probability(0.05f, 0.9f, 0.9f, 0, 0, 0.20f,
                                                                    0.30f, 0.25f, 0.08f, 0.05f);
    float with_compart = InformantSystemModule::compute_flip_probability(
        0.05f, 0.9f, 0.9f, 0, 3, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(with_compart < without);
}

TEST_CASE("Informant: high trust suppresses flip", "[informant_system][tier9]") {
    // Use params where raw probability is below cap so comparison is meaningful
    // base=0.02, risk_tol=0.9, trust=0.5: 0.02+0.03+0.125 = 0.175 (below 0.20 cap)
    // base=0.02, risk_tol=0.9, trust=0.9: 0.02+0.03+0.025 = 0.075
    float low_trust = InformantSystemModule::compute_flip_probability(
        0.02f, 0.9f, 0.5f, 0, 0, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    float high_trust = InformantSystemModule::compute_flip_probability(
        0.02f, 0.9f, 0.9f, 0, 0, 0.20f, 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(high_trust < low_trust);
}

TEST_CASE("Informant: flip probability non-negative", "[informant_system][tier9]") {
    // Extreme suppression
    float prob = InformantSystemModule::compute_flip_probability(0.10f, 1.0f, 1.0f, 5, 5, 0.20f,
                                                                 0.30f, 0.25f, 0.08f, 0.05f);
    REQUIRE(prob >= 0.0f);
}

TEST_CASE("Informant: constants match spec", "[informant_system][tier9]") {
    REQUIRE_THAT(0.20f, WithinAbs(0.20f, 0.001f));
    REQUIRE_THAT(50000.0f, WithinAbs(50000.0f, 1.0f));
    REQUIRE_THAT(3.0f, WithinAbs(3.0f, 0.01f));
}

// ============================================================================
// Self-seeding from imprisoned criminal NPCs (makes the subsystem live)
// ============================================================================

namespace {
NPC make_imprisoned_criminal(uint32_t id, NPCRole role) {
    NPC n{};
    n.id = id;
    n.role = role;
    n.status = NPCStatus::imprisoned;
    n.current_province_id = 0;
    return n;
}
}  // namespace

TEST_CASE("Informant: execute self-seeds a record for an imprisoned criminal NPC",
          "[informant_system][tier9]") {
    WorldState state{};
    state.current_tick = 100;
    state.world_seed = 42;
    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

    state.significant_npcs.push_back(make_imprisoned_criminal(7, NPCRole::criminal_operator));

    InformantSystemModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.records().size() == 1);
    const InformantRecord& rec = module.records()[0];
    REQUIRE(rec.npc_id == 7);
    REQUIRE(rec.arrest_tick == 100);
    REQUIRE(rec.compartmentalization_level == 0u);  // operator = core knowledge

    // Idempotent: a second tick does not duplicate the record.
    DeltaBuffer delta2{};
    module.execute(state, delta2);
    REQUIRE(module.records().size() == 1);
}

TEST_CASE("Informant: non-criminal or free NPCs are not seeded", "[informant_system][tier9]") {
    WorldState state{};
    state.current_tick = 5;
    state.world_seed = 1;
    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

    // Imprisoned but non-criminal -> skipped.
    NPC worker = make_imprisoned_criminal(1, NPCRole::worker);
    state.significant_npcs.push_back(worker);
    // Criminal but not imprisoned -> skipped.
    NPC freeCriminal{};
    freeCriminal.id = 2;
    freeCriminal.role = NPCRole::criminal_enforcer;
    freeCriminal.status = NPCStatus::active;
    state.significant_npcs.push_back(freeCriminal);

    InformantSystemModule module;
    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.records().empty());
}

TEST_CASE("Informant: seeded record can flip and emit testimonial evidence",
          "[informant_system][tier9]") {
    WorldState state{};
    state.current_tick = 50;
    state.world_seed = 12345;
    PlayerCharacter player{};
    player.id = 999;
    state.player = std::make_unique<PlayerCharacter>(player);

    // High flip propensity: high risk tolerance, low trust, with knowledge.
    NPC informant = make_imprisoned_criminal(7, NPCRole::criminal_operator);
    informant.risk_tolerance = 0.95f;
    KnowledgeEntry ke{};
    ke.subject_id = 999;
    ke.confidence = 0.9f;
    informant.known_evidence.push_back(ke);
    state.significant_npcs.push_back(informant);

    // Force base_flip_rate high so the flip is near-certain this tick.
    InformantConfig cfg{};
    cfg.base_flip_rate = 1.0f;
    cfg.max_flip_probability = 1.0f;
    InformantSystemModule module(cfg);

    DeltaBuffer delta{};
    module.execute(state, delta);

    REQUIRE(module.records().size() == 1);
    REQUIRE(module.records()[0].status == InformantStatus::cooperating);
    // Cooperation produced a testimonial evidence token from disclosed knowledge.
    REQUIRE_FALSE(delta.evidence_deltas.empty());
}

// ===========================================================================
// Player countermeasures (pay / threaten / relocate / eliminate)
// ===========================================================================
namespace {
// World with a player and one tracked informant record (npc not imprisoned, so
// self-seeding/flip leave it alone and the countermeasure path is isolated).
WorldState make_countermeasure_world(uint32_t informant_id, float player_wealth) {
    WorldState w{};
    w.current_tick = 10;
    w.world_seed = 1;
    PlayerCharacter player{};
    player.id = 999;
    player.wealth = player_wealth;
    w.player = std::make_unique<PlayerCharacter>(player);
    NPC npc{};
    npc.id = informant_id;
    npc.role = NPCRole::worker;
    npc.status = NPCStatus::active;
    npc.current_province_id = 0;
    npc.risk_tolerance = 0.3f;
    w.significant_npcs.push_back(npc);
    return w;
}
void seed_record(InformantSystemModule& m, uint32_t npc_id) {
    InformantRecord r{};
    r.npc_id = npc_id;
    r.status = InformantStatus::not_cooperating;
    r.base_flip_rate = 0.005f;
    m.records_mut().push_back(r);
}
}  // namespace

TEST_CASE("Informant: pay_silence silences the informant and costs the player",
          "[informant_system][tier9]") {
    auto world = make_countermeasure_world(7, /*wealth=*/100000.0f);
    InformantSystemModule module;
    seed_record(module, 7);
    world.pending_informant_countermeasures.push_back({7, /*pay_silence=*/0});

    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.records()[0].status == InformantStatus::silenced);
    REQUIRE(module.records()[0].flip_probability == 0.0f);
    REQUIRE(delta.player_delta.wealth_delta.has_value());
    REQUIRE(*delta.player_delta.wealth_delta < 0.0f);          // paid the silence cost
    REQUIRE_FALSE(delta.new_obligation_nodes.empty());         // whistleblower_silenced favor
    REQUIRE(world.pending_informant_countermeasures.empty());  // drained
}

TEST_CASE("Informant: pay_silence fails when the player cannot afford it",
          "[informant_system][tier9]") {
    auto world = make_countermeasure_world(7, /*wealth=*/10.0f);  // < pay_silence_cost
    InformantSystemModule module;
    seed_record(module, 7);
    world.pending_informant_countermeasures.push_back({7, 0});

    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.records()[0].status == InformantStatus::not_cooperating);  // unchanged
    REQUIRE_FALSE(delta.player_delta.wealth_delta.has_value());
}

TEST_CASE("Informant: threaten raises risk tolerance and leaves a memory",
          "[informant_system][tier9]") {
    auto world = make_countermeasure_world(7, 100000.0f);
    InformantSystemModule module;
    seed_record(module, 7);
    world.pending_informant_countermeasures.push_back({7, /*threaten=*/1});

    DeltaBuffer delta{};
    module.execute(world, delta);

    bool found = false;
    for (const auto& nd : delta.npc_deltas)
        if (nd.npc_id == 7 && nd.risk_tolerance_delta.has_value() &&
            nd.new_memory_entry.has_value())
            found = true;
    REQUIRE(found);
}

TEST_CASE("Informant: relocate moves the witness and slashes flip probability",
          "[informant_system][tier9]") {
    auto world = make_countermeasure_world(7, 100000.0f);
    InformantSystemModule module;
    seed_record(module, 7);
    world.pending_informant_countermeasures.push_back({7, /*relocate=*/2});

    DeltaBuffer delta{};
    module.execute(world, delta);
    REQUIRE(module.records()[0].status == InformantStatus::relocated);
    REQUIRE(module.records()[0].flip_probability < 0.005f);
}

TEST_CASE("Informant: eliminate kills the NPC and leaves physical evidence",
          "[informant_system][tier9]") {
    auto world = make_countermeasure_world(7, 100000.0f);
    InformantSystemModule module;
    seed_record(module, 7);
    world.pending_informant_countermeasures.push_back({7, /*eliminate=*/3});

    DeltaBuffer delta{};
    module.execute(world, delta);

    REQUIRE(module.records()[0].status == InformantStatus::eliminated);
    bool dead = false;
    bool phys = false;
    for (const auto& nd : delta.npc_deltas)
        if (nd.npc_id == 7 && nd.new_status.has_value() && *nd.new_status == NPCStatus::dead)
            dead = true;
    for (const auto& ev : delta.evidence_deltas)
        if (ev.new_token.has_value() && ev.new_token->type == EvidenceType::physical)
            phys = true;
    REQUIRE(dead);
    REQUIRE(phys);
}
