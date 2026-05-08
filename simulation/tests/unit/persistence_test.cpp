#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/world_gen/goods_catalog.h"
#include "core/world_state/apply_deltas.h"  // lookup_good_id
#include "modules/persistence/persistence_module.h"
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
    REQUIRE(PersistenceModule::is_schema_compatible(3, 3) == true);
}

// v2 saves are explicitly rejected by the v3 build because good_id semantics
// changed (FNV-1a hash → catalog numeric_id). Loading a v2 save with v3
// code would silently route market deltas to phantom markets.
TEST_CASE("Persistence: schema rejects pre-v3 saves", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_schema_compatible(1, 3) == false);
    REQUIRE(PersistenceModule::is_schema_compatible(2, 3) == false);
}

TEST_CASE("Persistence: schema incompatible newer version", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_schema_compatible(5, 3) == false);
}

TEST_CASE("Persistence: needs migration", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::needs_migration(3, 4) == true);
    REQUIRE(PersistenceModule::needs_migration(3, 3) == false);
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
    // Schema v2: added next_action_sequence persistence (issue #11). See
    // docs/design/EconLife_PlayerDelta_Semantics_v1.md.
    REQUIRE(PersistenceModule::CURRENT_SCHEMA_VERSION == 3);
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
        REQUIRE_THAT(restored.provinces[i].conditions.crime_rate,
                     WithinAbs(world.provinces[i].conditions.crime_rate, 0.001));
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
