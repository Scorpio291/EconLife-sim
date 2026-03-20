#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "modules/persistence/persistence_module.h"

using namespace econlife;

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
    REQUIRE(PersistenceModule::is_schema_compatible(1, 1) == true);
}

TEST_CASE("Persistence: schema compatible older version", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_schema_compatible(1, 3) == true);
}

TEST_CASE("Persistence: schema incompatible newer version", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::is_schema_compatible(5, 3) == false);
}

TEST_CASE("Persistence: needs migration", "[persistence][tier12]") {
    REQUIRE(PersistenceModule::needs_migration(1, 3) == true);
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
    REQUIRE(PersistenceModule::CURRENT_SCHEMA_VERSION == 1);
    REQUIRE(PersistenceModule::SNAPSHOT_INTERVAL == 30);
    REQUIRE(PersistenceModule::WAL_SEGMENT_TICKS == 30);
}
