#pragma once

// apply_deltas — applies accumulated DeltaBuffer changes to WorldState.
// Called by TickOrchestrator after each module (or after all modules in a step).
//
// Semantics:
// - Additive fields: summed and clamped to domain range
// - Replacement fields: last write wins (within a tick step)
// - Append fields: pushed to the back of the target vector
// - Upsert fields: matched by key, updated if exists, inserted if not

namespace econlife {

struct WorldState;
struct DeltaBuffer;
struct SafetyCeilingsConfig;
struct PackageConfig;

// Apply all deltas from buffer to world state, then clear the buffer.
// If no config is provided, uses default safety ceiling values.
// config is used to extract evidence_decay_interval for scheduling initial decay work items.
void apply_deltas(WorldState& world, DeltaBuffer& delta,
                  const SafetyCeilingsConfig* ceilings = nullptr,
                  const PackageConfig* config = nullptr);

// Apply cross-province deltas that were deferred from the previous tick.
void apply_cross_province_deltas(WorldState& world);

// Rebuild WorldState::npc_indices_by_province from significant_npcs.
//
// Outer vector is resized to provinces.size(); inner vectors are cleared and
// repopulated with significant_npcs indices grouped by current_province_id,
// preserving ascending vector order (which is id-ascending after world
// generation).
//
// O(N) in significant_npcs. Single-threaded; called by:
//  - WorldGenerator after NPC generation completes
//  - PersistenceModule after deserialization
//  - TickOrchestrator after every apply_deltas() call (so the index reflects
//    the WorldState the next module will see)
//
// NPCs whose current_province_id is out of range relative to provinces.size()
// are skipped (logged in debug builds).
void rebuild_npc_indices(WorldState& world);

}  // namespace econlife
