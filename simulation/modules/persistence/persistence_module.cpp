#include "persistence_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/delta_buffer.h"
#include <algorithm>

namespace econlife {

uint32_t PersistenceModule::compute_checksum(const uint8_t* data, size_t length) {
    // CRC32 implementation (simplified polynomial)
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

bool PersistenceModule::is_schema_compatible(uint32_t saved_version, uint32_t current_version) {
    return saved_version <= current_version;
}

bool PersistenceModule::needs_migration(uint32_t saved_version, uint32_t current_version) {
    return saved_version < current_version;
}

bool PersistenceModule::is_save_allowed(bool cross_province_buffer_empty) {
    return cross_province_buffer_empty;
}

RestoreResult PersistenceModule::check_restore_preconditions(bool is_ironman, bool is_restoring) {
    if (is_ironman) return RestoreResult::locked_ironman_mode;
    if (is_restoring) return RestoreResult::already_restoring;
    return RestoreResult::success;
}

bool PersistenceModule::is_snapshot_tick(uint32_t current_tick) {
    return (current_tick % SNAPSHOT_INTERVAL) == 0;
}

uint8_t PersistenceModule::compute_disruption_tier(uint32_t restoration_count) {
    if (restoration_count == 0) return 0;
    if (restoration_count <= 2) return 1;
    if (restoration_count <= 5) return 2;
    return 3;
}

void PersistenceModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Persistence module does not write to DeltaBuffer during tick.
    // Save/load invoked between ticks by the game loop.

    // Autosave check: snapshot at monthly intervals
    if (is_snapshot_tick(state.current_tick)) {
        // In full implementation: background thread serializes WorldState
        // using Protocol Buffers + LZ4 compression
    }
}

}  // namespace econlife
