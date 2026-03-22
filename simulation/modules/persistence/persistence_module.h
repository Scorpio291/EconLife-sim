#pragma once

#include "core/tick/tick_module.h"
#include "persistence_types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

class PersistenceModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "persistence"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    std::vector<std::string_view> runs_after() const override { return {"lod_system"}; }
    bool is_province_parallel() const noexcept override { return false; }
    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Serialization API ---

    // Serialize WorldState to flat binary format with LZ4 compression.
    // Header: MAGIC(4) + schema_version(4) + uncompressed_size(4) + checksum(4)
    // Body: LZ4-compressed flat binary of all WorldState fields.
    // Deterministic: same state always produces identical bytes.
    static std::vector<uint8_t> serialize(const WorldState& state);

    // Deserialize LZ4-compressed flat binary back to WorldState.
    // Validates magic, schema, and checksum. Overwrites all fields in out_state.
    // Returns RestoreResult::success on success, error code otherwise.
    static RestoreResult deserialize(const std::vector<uint8_t>& data, WorldState& out_state);

    // --- Static utilities for testing ---

    // Compute CRC32 checksum of data
    static uint32_t compute_checksum(const uint8_t* data, size_t length);

    // Validate schema version compatibility
    static bool is_schema_compatible(uint32_t saved_version, uint32_t current_version);

    // Check if schema needs migration
    static bool needs_migration(uint32_t saved_version, uint32_t current_version);

    // Check if save is allowed (cross-province buffer empty)
    static bool is_save_allowed(bool cross_province_buffer_empty);

    // Check if restore is allowed given game mode
    static RestoreResult check_restore_preconditions(bool is_ironman, bool is_restoring);

    // Compute snapshot cadence (every 30 ticks)
    static bool is_snapshot_tick(uint32_t current_tick);

    // Compute disruption tier from restoration count
    static uint8_t compute_disruption_tier(uint32_t restoration_count);

    // Constants
    static constexpr uint32_t CURRENT_SCHEMA_VERSION = 1;
    static constexpr uint32_t SNAPSHOT_INTERVAL      = 30;   // ticks per snapshot (monthly)
    static constexpr uint32_t WAL_SEGMENT_TICKS      = 30;   // ticks per WAL segment
    static constexpr uint32_t MAGIC_BYTES            = 0x45434F4E;  // "ECON"
    static constexpr float    COMPRESSION_TARGET     = 0.60f; // < 60% of uncompressed
    static constexpr uint32_t HEADER_SIZE            = 16;    // magic + schema + uncompressed_size + checksum

private:
    bool is_restoring_ = false;
    std::vector<SchemaMigration> migrations_;
};

}  // namespace econlife
