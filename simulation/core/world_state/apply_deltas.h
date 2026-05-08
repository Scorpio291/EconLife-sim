#pragma once

#include <cstdint>

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

struct NPC;

// Lookup an NPC by id. Prefers the precomputed npc_index_by_id (O(1));
// falls back to a linear scan of significant_npcs only when the index is
// empty and significant_npcs is not — i.e. in unit tests that construct
// WorldState piecemeal and never call rebuild_npc_indices(). Production
// paths always have the index populated by the tick orchestrator.
const NPC* lookup_npc_by_id(const WorldState& world, uint32_t npc_id);

// Rebuild the computed NPC indices on WorldState from significant_npcs:
//   - npc_index_by_id (id → significant_npcs index) for O(1) per-id lookup.
//   - npc_indices_by_province (province → indices) for the province bucket.
//
// The province bucket's outer vector is resized to provinces.size(); inner
// vectors are cleared and repopulated with significant_npcs indices grouped
// by current_province_id, preserving ascending vector order (which is
// id-ascending after world generation).
//
// O(N) in significant_npcs. Single-threaded; called by:
//  - WorldGenerator after NPC generation completes
//  - PersistenceModule after deserialization
//  - TickOrchestrator after every apply_deltas() call (so the indices reflect
//    the WorldState the next module will see)
//
// NPCs whose current_province_id is out of range relative to provinces.size()
// are skipped from the province bucket; they remain reachable through
// npc_index_by_id.
void rebuild_npc_indices(WorldState& world);

}  // namespace econlife
