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

// Apply all deltas from buffer to world state, then clear the buffer.
void apply_deltas(WorldState& world, DeltaBuffer& delta);

// Apply cross-province deltas that were deferred from the previous tick.
void apply_cross_province_deltas(WorldState& world);

}  // namespace econlife
