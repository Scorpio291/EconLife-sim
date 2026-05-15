#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/tick/tick_module.h"
#include "persistence_types.h"

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
    // Schema versions:
    //  v2: legacy (good_id values were FNV-1a hashes that did not match the
    //      catalog numeric_id assigned by create_markets) — refused on load
    //      to avoid silently routing deltas to phantom markets.
    //  v3: good_id values are catalog numeric_ids (sequential, stable across
    //      saves of the same goods CSV). The catalog itself is now embedded
    //      in the save so deserialised worlds hold the same id mapping.
    //  v4: per-NPC AddictionState (stage, substance_key, tolerance, craving,
    //      consecutive_use_ticks, clean_ticks, supply_gap_ticks,
    //      relapse_probability) now lives on NPC::addiction_state and is
    //      embedded in each NPC record. Pre-v4 saves are rejected because
    //      the v3 NPC record has no addiction footer — reading one with v4
    //      code would consume the next record's bytes.
    //  v5: demographics consolidation. Population-fraction monitors
    //      (addiction_rate, crime_rate, criminal_dominance_index,
    //      formal_employment_rate) migrated from RegionConditions to
    //      RegionCohortStats; sick_rate, homeless_rate, unemployment_rate
    //      added to RegionCohortStats. The RegionConditions block is 4
    //      floats shorter; the cohort_stats block is 7 floats longer.
    //      Pre-v5 saves are rejected because reading a v4 RegionConditions
    //      with v5 code would consume the trailing v4 fields as if they
    //      were the next struct's bytes.
    //   6: state.currencies (vector<CurrencyRecord>) is now serialized after
    //      route_table; state.facilities (vector<Facility>) follows; then a
    //      GlobalTechnologyState footer covers current_era, era_started_tick,
    //      domain_knowledge[19], active_research_projects, and
    //      active_maturation_projects. era_triggers stays config-loaded
    //      reference data (reloaded on startup; not persisted).
    //      Pre-v6 saves are rejected because the new trailing blocks change
    //      the byte stream layout — a v5 save loaded by v6 code would
    //      short-read at end of file.
    static constexpr uint32_t CURRENT_SCHEMA_VERSION = 6;
    static constexpr uint32_t SNAPSHOT_INTERVAL = 30;    // ticks per snapshot (monthly)
    static constexpr uint32_t WAL_SEGMENT_TICKS = 30;    // ticks per WAL segment
    static constexpr uint32_t MAGIC_BYTES = 0x45434F4E;  // "ECON"
    static constexpr float COMPRESSION_TARGET = 0.60f;   // < 60% of uncompressed
    static constexpr uint32_t HEADER_SIZE = 16;  // magic + schema + uncompressed_size + checksum

   private:
    bool is_restoring_ = false;
    std::vector<SchemaMigration> migrations_;
};

}  // namespace econlife
