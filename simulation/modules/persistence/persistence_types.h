#pragma once

// persistence module types.
// Types for save/load, WAL, schema migration, and timeline restoration.

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace econlife {

enum class RestoreResult : uint8_t {
    success,
    locked_ironman_mode,
    schema_too_new,
    migration_failed,
    checksum_mismatch,
    file_not_found,
    already_restoring,
    io_error
};

struct SnapshotSummary {
    uint32_t tick = 0;
    uint32_t schema_version = 0;
    float player_net_worth = 0.0f;
    float player_exposure = 0.0f;
    std::string date_label;
    std::string event_label;
    uint64_t file_size_bytes = 0;
};

struct WalSegmentHeader {
    uint32_t segment_id = 0;
    uint32_t start_tick = 0;
    uint32_t end_tick = 0;
    uint32_t entry_count = 0;
    uint32_t checksum = 0;
    bool superseded = false;
};

using MigrationFunction = std::function<bool(std::vector<uint8_t>&)>;

struct SchemaMigration {
    uint32_t from_version = 0;
    uint32_t to_version = 0;
    MigrationFunction migrate;
};

}  // namespace econlife
