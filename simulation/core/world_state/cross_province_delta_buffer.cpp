// CrossProvinceDeltaBuffer — cross-province effects with one-tick propagation delay.
// Province-parallel modules push cross-province deltas here during execution.
// Main thread flushes entries to the deferred work queue at the end of each tick.

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

void CrossProvinceDeltaBuffer::push(CrossProvinceDelta delta) {
    entries.push_back(std::move(delta));
}

void CrossProvinceDeltaBuffer::flush_to_deferred_queue(WorldState& world_state) {
    // Cross-province effects are applied at the start of the next tick.
    // The orchestrator calls apply_cross_province_deltas() at tick start,
    // which processes entries where due_tick <= current_tick, then clears.
    // This function is kept for the explicit flush path (e.g., before save).
    entries.clear();
}

}  // namespace econlife
