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
    // We don't convert to DeferredWorkItem here — instead, the cross-province
    // deltas are held in the buffer and applied directly during step 1 of the
    // next tick (before deferred work queue drain).
    //
    // For now, clear the buffer. The orchestrator will apply pending entries
    // at the start of the next tick during the cross-province application step.
    //
    // TODO: Implement cross-province delta application in TickOrchestrator step 1.
    entries.clear();
}

}  // namespace econlife
