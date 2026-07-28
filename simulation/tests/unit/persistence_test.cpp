#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_gen/goods_catalog.h"
#include "core/world_state/apply_deltas.h"  // lookup_good_id
#include "core/world_state/delta_buffer.h"
#include "modules/persistence/persistence_module.h"
#include "modules/seasonal_agriculture/seasonal_agriculture_module.h"
#include "tests/test_world_factory.h"

using namespace econlife;
using Catch::Matchers::WithinAbs;

TEST_CASE("Persistence: checksum deterministic", "[persistence][tier12]") {
    uint8_t data[] = {0x45, 0x43, 0x4F, 0x4E, 0x01, 0x00, 0x00, 0x00};
    uint32_t crc1 = PersistenceModule::compute_checksum(data, sizeof(data));
    uint32_t crc2 = PersistenceModule::compute_checksum(data, sizeof(data));
    REQUIRE(crc1 == crc2);
}

TEST_CASE("Persistence: checksum changes with data", "[persistence][tier12]") {
    uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t data2[] = {0x01, 0x02, 0x03, 0x05};
    uint32_t crc1 = PersistenceModule::compute_checksum(data1, sizeof(data1));
    uint32_t crc2 = PersistenceModule::compute_checksum(data2, sizeof(data2));
    REQUIRE(crc1 != crc2);
}

TEST_CASE("Persistence: checksum empty data", "[persistence][tier12]") {
    uint32_t crc = PersistenceModule::compute_checksum(nullptr, 0);
    // Should produce the "empty" CRC32 value
    REQUIRE(crc == 0x00000000);
}

TEST_CASE("Persistence: schema compatible same version", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_schema_compatible(24, 24) == true);
    REQUIRE(PersistenceModule::is_schema_compatible(PersistenceModule::CURRENT_SCHEMA_VERSION,
                                                    PersistenceModule::CURRENT_SCHEMA_VERSION) ==
            true);
}

// The floor is v24, not v7: the era byte was re-based twice (modern era
// 1 -> 5 -> 8) and the SECOND re-base landed inside schema v23 with no version
// bump, so v23 saves exist with two meanings for the same byte and nothing in
// the file tells them apart. Migration is impossible by construction, so every
// pre-renumbering save is rejected outright rather than silently reinterpreted
// (a modern world would load into the Medieval era, or the reverse).
TEST_CASE("Persistence: schema rejects pre-renumbering saves", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::MIN_SUPPORTED_SCHEMA_VERSION == 24);
    for (uint32_t v = 1; v < PersistenceModule::MIN_SUPPORTED_SCHEMA_VERSION; ++v) {
        REQUIRE(PersistenceModule::is_schema_compatible(
                    v, PersistenceModule::CURRENT_SCHEMA_VERSION) == false);
    }
    // v23 in particular: the ambiguous version.
    REQUIRE(PersistenceModule::is_schema_compatible(23, PersistenceModule::CURRENT_SCHEMA_VERSION) ==
            false);
}

TEST_CASE("Persistence: schema accepts v24..current", "[persistence][tier12]") {
    for (uint32_t v = PersistenceModule::MIN_SUPPORTED_SCHEMA_VERSION;
         v <= PersistenceModule::CURRENT_SCHEMA_VERSION; ++v) {
        REQUIRE(PersistenceModule::is_schema_compatible(
                    v, PersistenceModule::CURRENT_SCHEMA_VERSION) == true);
    }
}

TEST_CASE("Persistence: schema incompatible newer version", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_schema_compatible(PersistenceModule::CURRENT_SCHEMA_VERSION + 1,
                                                    PersistenceModule::CURRENT_SCHEMA_VERSION) ==
            false);
}

TEST_CASE("Persistence: needs migration", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::needs_migration(24, 25) == true);
    REQUIRE(PersistenceModule::needs_migration(25, 25) == false);
}

TEST_CASE("Persistence: save allowed when buffer empty", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_save_allowed(true) == true);
    REQUIRE(PersistenceModule::is_save_allowed(false) == false);
}

TEST_CASE("Persistence: restore blocked in ironman", "[persistence][tier12]") {
    auto result = PersistenceModule::check_restore_preconditions(true, false);
    REQUIRE(result == RestoreResult::locked_ironman_mode);
}

TEST_CASE("Persistence: restore blocked when already restoring", "[persistence][tier12]") {
    auto result = PersistenceModule::check_restore_preconditions(false, true);
    REQUIRE(result == RestoreResult::already_restoring);
}

TEST_CASE("Persistence: restore allowed in standard mode", "[persistence][tier12]") {
    auto result = PersistenceModule::check_restore_preconditions(false, false);
    REQUIRE(result == RestoreResult::success);
}

TEST_CASE("Persistence: snapshot tick cadence", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_snapshot_tick(0) == true);
    REQUIRE(PersistenceModule::is_snapshot_tick(30) == true);
    REQUIRE(PersistenceModule::is_snapshot_tick(60) == true);
    REQUIRE(PersistenceModule::is_snapshot_tick(15) == false);
    REQUIRE(PersistenceModule::is_snapshot_tick(1) == false);
}

TEST_CASE("Persistence: disruption tier computation", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::compute_disruption_tier(0) == 0);
    REQUIRE(PersistenceModule::compute_disruption_tier(1) == 1);
    REQUIRE(PersistenceModule::compute_disruption_tier(2) == 1);
    REQUIRE(PersistenceModule::compute_disruption_tier(3) == 2);
    REQUIRE(PersistenceModule::compute_disruption_tier(5) == 2);
    REQUIRE(PersistenceModule::compute_disruption_tier(6) == 3);
    REQUIRE(PersistenceModule::compute_disruption_tier(100) == 3);
}

TEST_CASE("Persistence: constants match spec", "[persistence][tier12]") {
    // Schema v10: pending_transactions Phase 4 mortgage extension
    // (payment_method, down_payment_fraction, interest_rate,
    // loan_maturity_ticks per entry). Earlier bumps (v3..v10) documented
    // in persistence_module.h:CURRENT_SCHEMA_VERSION.
    // v17: consequence queue (GDD §21 delayed-consequence system).
    // v19: cohort_stats->subsistence_surplus_ratio (commons food economy).
    // v20: per-NPC occupation (livelihood).
    // v21: GlobalTechnologyState knowledge_level (knowledge engine).
    // v22: WorldState.hazard_settings (world spectrum).
    // v23: cohort_stats.hardiness (generational adaptation).
    // v24: cohort_stats.food_store (granary / commons food economy).
    // v25: per-province FisheriesProfile, Nation national_legitimacy,
    //      political_cycle module-private state.
    // v26: cohort_stats specialist_fraction, productive_capital, soil_health and
    //      codified_knowledge — the freed stratum, the capital a society has built,
    //      the fertility of its land, and its written corpus.
    REQUIRE(PersistenceModule::CURRENT_SCHEMA_VERSION == 26);
    REQUIRE(PersistenceModule::SNAPSHOT_INTERVAL == 30);
    REQUIRE(PersistenceModule::WAL_SEGMENT_TICKS == 30);
}

// ── Serialization round-trip tests ─────────────────────────────────────────

TEST_CASE("Persistence: serialize produces non-empty output",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 10, 2, 5);
    auto bytes = PersistenceModule::serialize(world);
    REQUIRE(!bytes.empty());
    REQUIRE(bytes.size() >= PersistenceModule::HEADER_SIZE);
}

TEST_CASE("Persistence: serialize is deterministic", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 20, 2, 5);
    auto bytes1 = PersistenceModule::serialize(world);
    auto bytes2 = PersistenceModule::serialize(world);
    REQUIRE(bytes1.size() == bytes2.size());
    REQUIRE(bytes1 == bytes2);
}

TEST_CASE("Persistence: round-trip preserves global scalars",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(99, 10, 2, 5);
    world.current_tick = 42;
    world.ticks_this_session = 7;
    world.network_health_dirty = true;

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.current_tick == 42);
    REQUIRE(restored.world_seed == 99);
    REQUIRE(restored.ticks_this_session == 7);
    REQUIRE(restored.game_mode == GameMode::standard);
    REQUIRE(restored.current_schema_version == 1);
    REQUIRE(restored.network_health_dirty == true);
}

TEST_CASE("Persistence: round-trip preserves cohorts and NPC age (v16)",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(7, 5, 1, 3);
    REQUIRE(world.provinces.size() == 1);
    if (!world.provinces[0].cohort_stats)
        world.provinces[0].cohort_stats = std::make_unique<RegionCohortStats>();
    auto& cs = *world.provinces[0].cohort_stats;
    cs.regional_wage_anchor = 123.0f;
    cs.mean_income = 111.0f;
    cs.gini_coefficient = 0.42f;
    PopulationCohort c;
    c.group = DemographicGroup::working_urban_mid;
    c.size = 4321;
    c.median_income = 200.0f;
    c.education_level = 0.55f;
    c.employment_rate = 0.66f;
    cs.cohorts[c.group] = c;
    cs.total_population = c.size;
    REQUIRE_FALSE(world.significant_npcs.empty());
    world.significant_npcs[0].age_years = 71.5f;

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    REQUIRE(restored.provinces[0].cohort_stats != nullptr);
    const auto& rcs = *restored.provinces[0].cohort_stats;
    REQUIRE_THAT(rcs.regional_wage_anchor, Catch::Matchers::WithinAbs(123.0f, 1e-3f));
    REQUIRE_THAT(rcs.gini_coefficient, Catch::Matchers::WithinAbs(0.42f, 1e-3f));
    REQUIRE(rcs.cohorts.count(DemographicGroup::working_urban_mid) == 1);
    const auto& rc = rcs.cohorts.at(DemographicGroup::working_urban_mid);
    REQUIRE(rc.size == 4321u);
    REQUIRE_THAT(rc.median_income, Catch::Matchers::WithinAbs(200.0f, 1e-3f));
    REQUIRE_THAT(restored.significant_npcs[0].age_years, Catch::Matchers::WithinAbs(71.5f, 1e-3f));
}

TEST_CASE("Persistence: round-trip preserves NPC data", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 20, 2, 5);

    // Add a memory entry and relationship to first NPC
    auto& npc = world.significant_npcs[0];
    MemoryEntry mem{};
    mem.tick_timestamp = 10;
    mem.type = MemoryType::interaction;
    mem.subject_id = 200;
    mem.emotional_weight = 0.75f;
    mem.decay = 0.9f;
    mem.is_actionable = true;
    npc.memory_log.push_back(mem);

    Relationship rel{};
    rel.target_npc_id = 200;
    rel.trust = 0.6f;
    rel.fear = 0.1f;
    rel.obligation_balance = -0.3f;
    rel.last_interaction_tick = 5;
    rel.is_movement_ally = false;
    rel.recovery_ceiling = 0.8f;
    rel.shared_secrets = {1, 2, 3};
    npc.relationships.push_back(rel);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.significant_npcs.size() == world.significant_npcs.size());
    const auto& rnpc = restored.significant_npcs[0];
    REQUIRE(rnpc.id == npc.id);
    REQUIRE_THAT(rnpc.capital, WithinAbs(npc.capital, 0.01));
    REQUIRE(rnpc.memory_log.size() == 1);
    REQUIRE(rnpc.memory_log[0].type == MemoryType::interaction);
    REQUIRE_THAT(rnpc.memory_log[0].emotional_weight, WithinAbs(0.75f, 0.001));
    REQUIRE(rnpc.relationships.size() == 1);
    REQUIRE_THAT(rnpc.relationships[0].trust, WithinAbs(0.6f, 0.001));
    REQUIRE(rnpc.relationships[0].shared_secrets.size() == 3);
}

TEST_CASE("Persistence: round-trip preserves InvestigatorMeter (v18)",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 20, 2, 5);

    auto& npc = world.significant_npcs[0];
    npc.investigator_meter.current_level = 0.72f;
    npc.investigator_meter.status = InvestigatorMeterStatus::formal_inquiry;
    npc.investigator_meter.target_npc_id = 4242;
    npc.investigator_meter.opened_tick = 17;
    npc.investigator_meter.case_escalated = true;

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    const auto& rm = restored.significant_npcs[0].investigator_meter;
    REQUIRE_THAT(rm.current_level, WithinAbs(0.72f, 1e-3f));
    REQUIRE(rm.status == InvestigatorMeterStatus::formal_inquiry);
    REQUIRE(rm.target_npc_id == 4242u);
    REQUIRE(rm.opened_tick == 17u);
    REQUIRE(rm.case_escalated);
}

TEST_CASE("Persistence: round-trip preserves addiction withdrawal fields (v15)",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 20, 2, 5);

    auto& npc = world.significant_npcs[0];
    AddictionState as{};
    as.stage = AddictionStage::dependent;
    as.substance_key = "cocaine";
    as.tolerance = 0.42f;
    as.craving = 0.61f;
    as.consecutive_use_ticks = 123;
    as.clean_ticks = 7;
    as.supply_gap_ticks = 4;
    as.relapse_probability = 0.33f;
    as.withdrawal_health = 0.27f;  // v15 field
    as.terminal_ticks = 11;        // v15 field
    npc.addiction_state = as;

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    const auto& r = restored.significant_npcs[0].addiction_state;
    REQUIRE(r.stage == AddictionStage::dependent);
    REQUIRE(r.substance_key == "cocaine");
    REQUIRE(r.consecutive_use_ticks == 123);
    REQUIRE(r.supply_gap_ticks == 4);
    REQUIRE_THAT(r.withdrawal_health, WithinAbs(0.27f, 1e-4));
    REQUIRE(r.terminal_ticks == 11);
}

TEST_CASE("Persistence: round-trip preserves markets", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 10, 2, 5);
    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.regional_markets.size() == world.regional_markets.size());
    for (size_t i = 0; i < world.regional_markets.size(); ++i) {
        REQUIRE(restored.regional_markets[i].good_id == world.regional_markets[i].good_id);
        REQUIRE_THAT(restored.regional_markets[i].spot_price,
                     WithinAbs(world.regional_markets[i].spot_price, 0.001));
    }
}

TEST_CASE("Persistence: round-trip preserves businesses", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 10, 2, 5);
    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.npc_businesses.size() == world.npc_businesses.size());
    for (size_t i = 0; i < world.npc_businesses.size(); ++i) {
        REQUIRE(restored.npc_businesses[i].id == world.npc_businesses[i].id);
        REQUIRE_THAT(restored.npc_businesses[i].cash,
                     WithinAbs(world.npc_businesses[i].cash, 0.01));
    }
}

TEST_CASE("Persistence: round-trip preserves provinces", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 10, 3, 5);
    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.provinces.size() == 3);
    for (size_t i = 0; i < world.provinces.size(); ++i) {
        REQUIRE(restored.provinces[i].id == world.provinces[i].id);
        REQUIRE_THAT(restored.provinces[i].conditions.stability_score,
                     WithinAbs(world.provinces[i].conditions.stability_score, 0.001));
        REQUIRE_THAT(restored.provinces[i].cohort_stats->crime_rate,
                     WithinAbs(world.provinces[i].cohort_stats->crime_rate, 0.001));
        REQUIRE(restored.provinces[i].links.size() == world.provinces[i].links.size());
    }
}

TEST_CASE("Persistence: round-trip preserves evidence pool",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 10, 1, 5);
    EvidenceToken token{};
    token.id = 99;
    token.type = EvidenceType::financial;
    token.source_npc_id = 100;
    token.target_npc_id = 101;
    token.actionability = 0.8f;
    token.decay_rate = 0.01f;
    token.created_tick = 5;
    token.province_id = 0;
    token.is_active = true;
    world.evidence_pool.push_back(token);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.evidence_pool.size() == 1);
    REQUIRE(restored.evidence_pool[0].id == 99);
    REQUIRE_THAT(restored.evidence_pool[0].actionability, WithinAbs(0.8f, 0.001));
}

TEST_CASE("Persistence: round-trip preserves obligations", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 10, 1, 5);
    ObligationNode ob{};
    ob.id = 1;
    ob.creditor_npc_id = 100;
    ob.debtor_npc_id = 101;
    ob.favor_type = FavorType::financial_loan;
    ob.weight = 0.5f;
    ob.created_tick = 3;
    ob.is_active = true;
    world.obligation_network.push_back(ob);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.obligation_network.size() == 1);
    REQUIRE(restored.obligation_network[0].id == 1);
    REQUIRE_THAT(restored.obligation_network[0].weight, WithinAbs(0.5f, 0.001));
}

TEST_CASE("Persistence: LZ4 compression reduces size", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 100, 3, 10);
    auto bytes = PersistenceModule::serialize(world);

    // Header contains uncompressed size at offset 8
    uint32_t uncompressed_size =
        static_cast<uint32_t>(bytes[8]) | (static_cast<uint32_t>(bytes[9]) << 8) |
        (static_cast<uint32_t>(bytes[10]) << 16) | (static_cast<uint32_t>(bytes[11]) << 24);

    // Compressed size should be smaller than uncompressed
    REQUIRE(bytes.size() < uncompressed_size + PersistenceModule::HEADER_SIZE);
}

TEST_CASE("Persistence: corrupted data fails checksum", "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 10, 1, 5);
    auto bytes = PersistenceModule::serialize(world);

    // Flip a byte in the compressed payload
    if (bytes.size() > PersistenceModule::HEADER_SIZE + 1) {
        bytes[PersistenceModule::HEADER_SIZE + 1] ^= 0xFF;
    }

    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    // Either checksum_mismatch or io_error (LZ4 may fail to decompress)
    REQUIRE(result != RestoreResult::success);
}

TEST_CASE("Persistence: serialize-deserialize-serialize is byte-identical",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(42, 20, 2, 5);

    // Add some state
    EvidenceToken tok{};
    tok.id = 1;
    tok.type = EvidenceType::testimonial;
    tok.source_npc_id = 100;
    tok.target_npc_id = 101;
    tok.actionability = 0.5f;
    tok.decay_rate = 0.01f;
    tok.created_tick = 0;
    tok.province_id = 0;
    tok.is_active = true;
    world.evidence_pool.push_back(tok);

    auto bytes1 = PersistenceModule::serialize(world);

    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes1, restored);
    REQUIRE(result == RestoreResult::success);

    auto bytes2 = PersistenceModule::serialize(restored);

    // serialize(deserialize(serialize(state))) == serialize(state)
    REQUIRE(bytes1.size() == bytes2.size());
    REQUIRE(bytes1 == bytes2);
}

// --- Goods catalog round-trip (schema v3) ----------------------------------

namespace {

GoodDefinition make_good(uint32_t id, const std::string& key, float price, uint8_t tier,
                         const std::string& category, bool perishable, bool illegal, uint8_t era) {
    GoodDefinition g{};
    g.numeric_id = id;
    g.good_id = key;
    g.display_name = key + " (display)";
    g.tier = tier;
    g.unit = "tonne";
    g.category = category;
    g.base_price = price;
    g.perishable = perishable;
    g.illegal = illegal;
    g.era_available = era;
    return g;
}

}  // namespace

TEST_CASE("Persistence: empty catalog round-trips as empty",
          "[persistence][tier12][goods_catalog]") {
    WorldState world{};
    world.current_tick = 0;
    world.world_seed = 1;
    world.game_mode = GameMode::standard;
    REQUIRE(!world.goods_catalog);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    // Either nullptr or an empty catalog is acceptable; both mean "no
    // catalog", and lookup_good_id() behaves identically (FNV fallback).
    if (restored.goods_catalog) {
        REQUIRE(restored.goods_catalog->size() == 0);
    }
    REQUIRE(lookup_good_id(restored, "steel") == lookup_good_id(world, "steel"));
}

TEST_CASE("Persistence: empty-catalog save overwrites a stale catalog on the target",
          "[persistence][tier12][goods_catalog]") {
    // Deserialize is documented as a full state restore. If the target
    // WorldState already carries a non-empty catalog (e.g. a reused
    // container, or a world loaded from a previous save), restoring a
    // save whose goods section is empty must clear it — otherwise
    // lookup_good_id() resolves against IDs that no longer mean anything
    // in the restored world.
    WorldState src{};
    src.current_tick = 0;
    src.world_seed = 1;
    src.game_mode = GameMode::standard;
    REQUIRE(!src.goods_catalog);
    auto bytes = PersistenceModule::serialize(src);

    WorldState target{};
    target.current_tick = 99;
    target.world_seed = 42;
    target.game_mode = GameMode::standard;
    auto stale = std::make_unique<GoodsCatalog>();
    stale->push_back_loaded(make_good(11, "stale_steel", 80.0f, 2, "metals", false, false, 2));
    stale->push_back_loaded(make_good(22, "stale_ore", 15.0f, 0, "geological", false, false, 1));
    target.goods_catalog = std::move(stale);
    REQUIRE(target.goods_catalog);
    REQUIRE(target.goods_catalog->size() == 2);

    auto result = PersistenceModule::deserialize(bytes, target);
    REQUIRE(result == RestoreResult::success);

    // Catalog state must match the source: src had no catalog, so target
    // must end with no catalog. A non-null catalog here means the prior
    // contents leaked through.
    REQUIRE(!target.goods_catalog);
    // And behaviorally: looking up the stale entries' string keys must
    // not return their stale numeric_ids (11, 22). Without a catalog,
    // lookup_good_id falls back to the FNV-1a hash, which is unrelated
    // to the prior catalog's numeric_ids — so the stale IDs are gone.
    REQUIRE(lookup_good_id(target, "stale_steel") != 11u);
    REQUIRE(lookup_good_id(target, "stale_ore") != 22u);
}

TEST_CASE("Persistence: populated catalog round-trips field-by-field",
          "[persistence][tier12][goods_catalog]") {
    WorldState world{};
    world.current_tick = 42;
    world.world_seed = 99;
    world.game_mode = GameMode::standard;

    auto cat = std::make_unique<GoodsCatalog>();
    // Mix of fields covering every member of GoodDefinition.
    cat->push_back_loaded(make_good(0, "wheat", 25.0f, /*tier=*/0, "biological", /*perish=*/true,
                                    /*illegal=*/false, /*era=*/1));
    cat->push_back_loaded(make_good(7, "steel", 80.0f, /*tier=*/2, "metals", /*perish=*/false,
                                    /*illegal=*/false, /*era=*/2));
    cat->push_back_loaded(make_good(42, "coca_leaf", 200.0f, /*tier=*/1, "biological",
                                    /*perish=*/true, /*illegal=*/true, /*era=*/3));
    world.goods_catalog = std::move(cat);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.goods_catalog);
    REQUIRE(restored.goods_catalog->size() == 3);

    const auto& orig = world.goods_catalog->goods();
    const auto& copy = restored.goods_catalog->goods();
    for (size_t i = 0; i < orig.size(); ++i) {
        CAPTURE(i);
        REQUIRE(copy[i].numeric_id == orig[i].numeric_id);
        REQUIRE(copy[i].good_id == orig[i].good_id);
        REQUIRE(copy[i].display_name == orig[i].display_name);
        REQUIRE(copy[i].tier == orig[i].tier);
        REQUIRE(copy[i].unit == orig[i].unit);
        REQUIRE(copy[i].category == orig[i].category);
        REQUIRE_THAT(copy[i].base_price, WithinAbs(orig[i].base_price, 0.0001f));
        REQUIRE(copy[i].perishable == orig[i].perishable);
        REQUIRE(copy[i].illegal == orig[i].illegal);
        REQUIRE(copy[i].era_available == orig[i].era_available);
    }
}

TEST_CASE("Persistence: catalog round-trip preserves lookup_good_id results",
          "[persistence][tier12][goods_catalog]") {
    // The whole point of embedding the catalog is so deserialised worlds
    // resolve string good_ids to the same numeric_id the original world used.
    // If serialize/deserialize swapped a field or read in wrong order, the
    // ids would still come back as some uint32 — but they would not match.
    WorldState world{};
    world.current_tick = 0;
    world.world_seed = 1;
    world.game_mode = GameMode::standard;

    auto cat = std::make_unique<GoodsCatalog>();
    cat->push_back_loaded(make_good(11, "steel", 80.0f, 2, "metals", false, false, 2));
    cat->push_back_loaded(make_good(22, "iron_ore", 15.0f, 0, "geological", false, false, 1));
    world.goods_catalog = std::move(cat);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    REQUIRE(lookup_good_id(restored, "steel") == 11u);
    REQUIRE(lookup_good_id(restored, "iron_ore") == 22u);
    REQUIRE(lookup_good_id(restored, "steel") == lookup_good_id(world, "steel"));
    REQUIRE(lookup_good_id(restored, "iron_ore") == lookup_good_id(world, "iron_ore"));
}

TEST_CASE("Persistence: catalog round-trip is bit-identical",
          "[persistence][tier12][goods_catalog]") {
    // serialize → deserialize → serialize must be byte-equal to the first
    // serialise. Catches any non-canonical ordering or leftover state
    // (e.g. next_numeric_id_ drift after replay) that survives the field-
    // level checks above.
    WorldState world{};
    world.current_tick = 7;
    world.world_seed = 3;
    world.game_mode = GameMode::standard;

    auto cat = std::make_unique<GoodsCatalog>();
    cat->push_back_loaded(make_good(0, "wheat", 25.0f, 0, "biological", true, false, 1));
    cat->push_back_loaded(make_good(1, "iron_ore", 15.0f, 0, "geological", false, false, 1));
    world.goods_catalog = std::move(cat);

    auto bytes1 = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes1, restored) == RestoreResult::success);
    auto bytes2 = PersistenceModule::serialize(restored);
    REQUIRE(bytes1 == bytes2);
}

// ── v6: currencies, facilities, technology round-trip ──────────────────────

TEST_CASE("Persistence: round-trip preserves currencies",
          "[persistence][tier12][v6][serialization]") {
    WorldState world{};
    world.current_tick = 1;
    world.world_seed = 1;
    world.game_mode = GameMode::standard;

    CurrencyRecord usd{};
    usd.nation_id = 1;
    usd.iso_code = "USD";
    usd.usd_rate = 1.0f;
    usd.usd_rate_baseline = 1.0f;
    usd.volatility = 0.01f;
    usd.foreign_reserves = 0.9f;
    usd.pegged = false;
    usd.peg_rate = 1.0f;
    world.currencies.push_back(usd);

    CurrencyRecord eur{};
    eur.nation_id = 2;
    eur.iso_code = "EUR";
    eur.usd_rate = 0.92f;
    eur.usd_rate_baseline = 0.95f;
    eur.volatility = 0.02f;
    eur.foreign_reserves = 0.75f;
    eur.pegged = true;
    eur.peg_rate = 0.93f;
    world.currencies.push_back(eur);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    REQUIRE(restored.currencies.size() == 2);
    REQUIRE(restored.currencies[0].iso_code == "USD");
    REQUIRE(restored.currencies[0].usd_rate == 1.0f);
    REQUIRE(restored.currencies[1].iso_code == "EUR");
    REQUIRE(restored.currencies[1].pegged == true);
    REQUIRE(restored.currencies[1].peg_rate == 0.93f);
}

TEST_CASE("Persistence: round-trip preserves facilities",
          "[persistence][tier12][v6][serialization]") {
    WorldState world{};
    world.current_tick = 1;
    world.world_seed = 1;
    world.game_mode = GameMode::standard;

    Facility f1{};
    f1.id = 100;
    f1.business_id = 50;
    f1.province_id = 0;
    f1.recipe_id = "wheat_to_flour";
    f1.tech_tier = 1;
    f1.output_rate_modifier = 1.2f;
    f1.soil_health = 0.85f;
    f1.worker_count = 12;
    f1.is_operational = true;
    world.facilities.push_back(f1);

    Facility f2{};
    f2.id = 101;
    f2.business_id = 51;
    f2.province_id = 1;
    f2.recipe_id = "iron_ore_smelt";
    f2.tech_tier = 2;
    f2.output_rate_modifier = 0.95f;
    f2.soil_health = 1.0f;
    f2.worker_count = 8;
    f2.is_operational = false;
    world.facilities.push_back(f2);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    REQUIRE(restored.facilities.size() == 2);
    REQUIRE(restored.facilities[0].id == 100);
    REQUIRE(restored.facilities[0].recipe_id == "wheat_to_flour");
    REQUIRE(restored.facilities[0].worker_count == 12);
    REQUIRE(restored.facilities[0].soil_health == 0.85f);
    REQUIRE(restored.facilities[1].id == 101);
    REQUIRE(restored.facilities[1].is_operational == false);
    REQUIRE(restored.facilities[1].tech_tier == 2);
}

TEST_CASE("Persistence: round-trip preserves GlobalTechnologyState",
          "[persistence][tier12][v6][serialization]") {
    WorldState world{};
    world.current_tick = 1;
    world.world_seed = 1;
    world.game_mode = GameMode::standard;

    world.technology.current_era = /*acceleration=*/7;
    world.technology.era_started_tick = 4380;  // ~ year 12
    for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i)
        world.technology.domain_knowledge[i] = 0.1f * static_cast<float>(i + 1);

    ResearchProject rp{};
    rp.project_key = "fusion_v1";
    rp.business_id = 7;
    rp.facility_id = 12;
    rp.domain = "energy";
    rp.target_node_key = "fusion_node";
    rp.difficulty = 1.8f;
    rp.progress = 0.45f;
    rp.researchers_assigned = 3;
    rp.funding_per_tick = 250.0f;
    rp.success_probability = 0.4f;
    rp.started_tick = 100;
    rp.is_secret = true;
    world.technology.active_research_projects.push_back(rp);

    MaturationProject mp{};
    mp.node_key = "ai_optimization";
    mp.business_id = 9;
    mp.facility_id = 0;
    mp.researchers_assigned = 2;
    mp.funding_per_tick = 100.0f;
    mp.progress = 0.62f;
    world.technology.active_maturation_projects.push_back(mp);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    REQUIRE(restored.technology.current_era == /*acceleration=*/7);
    REQUIRE(restored.technology.era_started_tick == 4380);
    for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i) {
        REQUIRE(restored.technology.domain_knowledge[i] == 0.1f * static_cast<float>(i + 1));
    }
    REQUIRE(restored.technology.active_research_projects.size() == 1);
    REQUIRE(restored.technology.active_research_projects[0].project_key == "fusion_v1");
    REQUIRE(restored.technology.active_research_projects[0].is_secret == true);
    REQUIRE(restored.technology.active_research_projects[0].progress == 0.45f);
    REQUIRE(restored.technology.active_maturation_projects.size() == 1);
    REQUIRE(restored.technology.active_maturation_projects[0].node_key == "ai_optimization");
    REQUIRE(restored.technology.active_maturation_projects[0].progress == 0.62f);
}

// ── Schema v8: pending_random_event_triggers round-trip ────────────────────

TEST_CASE("Persistence: round-trip preserves pending_random_event_triggers (v8)",
          "[persistence][tier12][serialization][v8]") {
    auto world = test::create_test_world(99, 10, 2, 5);

    RandomEventTriggerDelta t1{};
    t1.template_key = "currency_crisis";
    t1.province_id = 2;
    t1.severity = 0.72f;
    world.pending_random_event_triggers.push_back(t1);

    RandomEventTriggerDelta t2{};
    t2.template_key = "market_shock";
    t2.province_id = 4;
    t2.severity = 0.31f;
    world.pending_random_event_triggers.push_back(t2);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    auto result = PersistenceModule::deserialize(bytes, restored);
    REQUIRE(result == RestoreResult::success);

    REQUIRE(restored.pending_random_event_triggers.size() == 2);
    REQUIRE(restored.pending_random_event_triggers[0].template_key == "currency_crisis");
    REQUIRE(restored.pending_random_event_triggers[0].province_id == 2);
    REQUIRE_THAT(restored.pending_random_event_triggers[0].severity, WithinAbs(0.72f, 0.001f));
    REQUIRE(restored.pending_random_event_triggers[1].template_key == "market_shock");
    REQUIRE(restored.pending_random_event_triggers[1].province_id == 4);
    REQUIRE_THAT(restored.pending_random_event_triggers[1].severity, WithinAbs(0.31f, 0.001f));
}

TEST_CASE("Persistence: empty trigger queue round-trips cleanly (v8)",
          "[persistence][tier12][serialization][v8]") {
    auto world = test::create_test_world(99, 10, 2, 5);
    REQUIRE(world.pending_random_event_triggers.empty());

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);
    REQUIRE(restored.pending_random_event_triggers.empty());
}

TEST_CASE("Persistence: pending_legal_case_seeds defensively cleared on load (v8)",
          "[persistence][tier12][serialization][v8]") {
    auto world = test::create_test_world(99, 10, 2, 5);
    // The producer→consumer pair runs within one tick so this queue is
    // documented as "must be empty at save time". Serializer does not
    // write it; loader defensively resets it.
    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    // Pre-populate to verify loader clears.
    LegalCaseSeedDelta stale{};
    stale.defendant_npc_id = 999;
    restored.pending_legal_case_seeds.push_back(stale);
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);
    REQUIRE(restored.pending_legal_case_seeds.empty());
}

TEST_CASE("Persistence: round-trip preserves the consequence queue (v17)",
          "[persistence][tier12][serialization]") {
    auto world = test::create_test_world(11, 5, 1, 3);
    ConsequenceEntry e{};
    e.id = 42;
    e.category = ConsequenceCategory::legal_proceeding;
    e.source_npc_id = 7;
    e.target_id = 9;
    e.province_id = 0;
    e.scheduled_tick = 5000;  // pending
    world.consequence_queue.push_back(e);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    REQUIRE(restored.consequence_queue.size() == 1);
    const auto& r = restored.consequence_queue[0];
    REQUIRE(r.id == 42);
    REQUIRE(r.category == ConsequenceCategory::legal_proceeding);
    REQUIRE(r.target_id == 9);
    REQUIRE(r.scheduled_tick == 5000u);
}

// ── Schema v25: fisheries, national legitimacy, and hostile-length rejection ──

namespace {

// Patch a little-endian u32 in the (non-checksum-covered) save header.
void patch_header_le32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    REQUIRE(bytes.size() >= offset + 4);
    bytes[offset] = static_cast<uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

// A coastal province as world generation leaves it, with a deliberately
// non-default value in every FisheriesProfile field so a dropped field shows up
// as a mismatch rather than coinciding with the struct default.
void seed_upwelling_fishery(Province& p) {
    p.geography.is_landlocked = false;
    p.geography.coastal_length_km = 250.0f;
    p.fisheries.access_type = FishingAccessType::Upwelling;
    p.fisheries.carrying_capacity = 0.62f;
    p.fisheries.current_stock = 0.124f;  // 0.2 * K — below the MSY stock
    p.fisheries.max_sustainable_yield = 0.186f;
    p.fisheries.intrinsic_growth_rate = 0.60f;
    p.fisheries.seasonal_closure = 0.07f;
    p.fisheries.is_migratory = true;
}

}  // namespace

TEST_CASE("Persistence: round-trip preserves the full FisheriesProfile (v25)",
          "[persistence][tier12][serialization][v25][fisheries]") {
    auto world = test::create_test_world(42, 10, 2, 5);
    REQUIRE(world.provinces.size() == 2);
    seed_upwelling_fishery(world.provinces[0]);
    // Province 1 stays landlocked, so the NoAccess case round-trips too.
    REQUIRE(world.provinces[1].fisheries.access_type == FishingAccessType::NoAccess);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    const auto& f = restored.provinces[0].fisheries;
    // access_type and carrying_capacity are the load-bearing pair: with either
    // one lost, seasonal_agriculture's process_fisheries early-returns forever.
    REQUIRE(f.access_type == FishingAccessType::Upwelling);
    REQUIRE_THAT(f.carrying_capacity, WithinAbs(0.62f, 1e-6f));
    REQUIRE_THAT(f.current_stock, WithinAbs(0.124f, 1e-6f));
    REQUIRE_THAT(f.max_sustainable_yield, WithinAbs(0.186f, 1e-6f));
    REQUIRE_THAT(f.intrinsic_growth_rate, WithinAbs(0.60f, 1e-6f));
    REQUIRE_THAT(f.seasonal_closure, WithinAbs(0.07f, 1e-6f));
    REQUIRE(f.is_migratory == true);

    const auto& landlocked = restored.provinces[1].fisheries;
    REQUIRE(landlocked.access_type == FishingAccessType::NoAccess);
    REQUIRE(landlocked.carrying_capacity == 0.0f);
}

TEST_CASE("Persistence: a loaded coastal province still grows its fish stock (v25)",
          "[persistence][tier12][serialization][v25][fisheries]") {
    // The failure this pins down was not a cosmetic field loss: a province that
    // loaded with the FisheriesProfile defaults (NoAccess, K = 0) hit the
    // early-return in process_fisheries, so its stock could never grow again and
    // it landed nothing — a coastal province turned permanently landlocked.
    auto world = test::create_test_world(42, 10, 2, 5);
    world.current_tick = 1500;  // 1500 % 365 = 40 -> past the 0.07 closure window
    seed_upwelling_fishery(world.provinces[0]);

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    SeasonalAgricultureModule ag;
    DeltaBuffer delta{};
    ag.execute_province(0, restored, delta);

    // The fishery is live and, below the MSY stock, growing: logistic growth
    // (r/365 * N * (1 - N/K)) exceeds the harvest (F/365 * N).
    REQUIRE(delta.fisheries_deltas.size() == 1);
    REQUIRE(delta.fisheries_deltas[0].province_id == 0u);
    REQUIRE(delta.fisheries_deltas[0].stock_delta > 0.0f);

    // Control: the landlocked province produces no fisheries delta either way,
    // which is exactly what every province looked like before v25.
    DeltaBuffer landlocked_delta{};
    ag.execute_province(1, restored, landlocked_delta);
    REQUIRE(landlocked_delta.fisheries_deltas.empty());
}

TEST_CASE("Persistence: round-trip preserves national_legitimacy (v25)",
          "[persistence][tier12][serialization][v25][legitimacy]") {
    // national_legitimacy is an EMA over its own prior value, so it is genuine
    // cross-tick state. Dropping it reset a nation in legitimacy crisis back to
    // the 0.5 default on load, skipping dozens of monthly crackdown /
    // concession / collapse steps.
    auto world = test::create_test_world(42, 10, 2, 5);
    REQUIRE(world.nations.size() == 1);
    world.nations[0].political_cycle.national_legitimacy = 0.17f;  // deep crisis
    world.nations[0].political_cycle.national_approval = 0.23f;
    world.nations[0].political_cycle.current_administration_tick = 900;
    world.nations[0].political_cycle.next_election_tick = 2920;
    world.nations[0].political_cycle.election_campaign_active = true;

    auto bytes = PersistenceModule::serialize(world);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(bytes, restored) == RestoreResult::success);

    const auto& pc = restored.nations[0].political_cycle;
    REQUIRE_THAT(pc.national_legitimacy, WithinAbs(0.17f, 1e-6f));
    // Still in crisis (< the 0.30 threshold), not silently restored to 0.5.
    REQUIRE(pc.national_legitimacy < 0.30f);
    // The neighbouring fields in the same block are unshifted.
    REQUIRE_THAT(pc.national_approval, WithinAbs(0.23f, 1e-6f));
    REQUIRE(pc.current_administration_tick == 900u);
    REQUIRE(pc.next_election_tick == 2920u);
    REQUIRE(pc.election_campaign_active == true);
}

TEST_CASE("Persistence: an impossible declared uncompressed size is rejected, not allocated",
          "[persistence][tier12][serialization][v25][robustness]") {
    // The header's uncompressed size is NOT checksum-covered — the checksum can
    // only be verified after decompressing into a buffer of that size — so one
    // corrupt header byte used to reach a ~4 GiB allocation with no try/catch
    // anywhere in the load path.
    auto world = test::create_test_world(42, 10, 2, 5);
    auto original = PersistenceModule::serialize(world);

    for (uint32_t bogus_size : {0xFFFFFFFFu, 0x40000000u, 64u * 1024u * 1024u}) {
        auto bytes = original;
        patch_header_le32(bytes, 8, bogus_size);

        WorldState restored{};
        restored.current_tick = 7;  // sentinel: must survive a rejected load
        auto result = PersistenceModule::deserialize(bytes, restored);
        REQUIRE(result == RestoreResult::io_error);
        REQUIRE(restored.current_tick == 7u);
        REQUIRE(restored.provinces.empty());
    }

    // The unpatched save still loads: the bound rejects only sizes the payload
    // could not possibly expand to.
    WorldState good{};
    REQUIRE(PersistenceModule::deserialize(original, good) == RestoreResult::success);
}

TEST_CASE("Persistence: pre-renumbering schema versions are rejected with an error",
          "[persistence][tier12][serialization][v25][robustness]") {
    // Decision on record: pre-renumbering saves are REJECTED, not migrated. The
    // era byte was re-based twice and the second re-base landed inside v23 with
    // no bump, so a v23 era value has two possible meanings and no version-keyed
    // remap can disambiguate them.
    auto world = test::create_test_world(42, 10, 2, 5);
    auto original = PersistenceModule::serialize(world);

    for (uint32_t old_version : {1u, 7u, 18u, 23u}) {
        auto bytes = original;
        patch_header_le32(bytes, 4, old_version);

        WorldState restored{};
        restored.current_tick = 11;  // sentinel: no state change on rejection
        auto result = PersistenceModule::deserialize(bytes, restored);
        REQUIRE(result == RestoreResult::migration_failed);
        REQUIRE(restored.current_tick == 11u);
        REQUIRE(restored.nations.empty());
    }

    // A newer-than-current version is still a distinct, separately reported case.
    auto too_new = original;
    patch_header_le32(too_new, 4, PersistenceModule::CURRENT_SCHEMA_VERSION + 1);
    WorldState restored{};
    REQUIRE(PersistenceModule::deserialize(too_new, restored) == RestoreResult::schema_too_new);
}
