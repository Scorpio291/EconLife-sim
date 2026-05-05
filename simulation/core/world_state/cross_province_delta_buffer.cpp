// CrossProvinceDeltaBuffer — cross-province effects with one-tick propagation delay.
// Province-parallel modules push cross-province deltas here during execution.
//
// Drain contract: the tick orchestrator calls apply_cross_province_deltas()
// (see core/world_state/apply_deltas.cpp) at tick start. That function
// partitions entries by due_tick, applies those that are due, and retains
// the rest. Nothing else mutates `entries`. There is intentionally no
// separate "flush" entry point: any code that needs the buffer drained
// (e.g. before save) must run the orchestrator until the buffer is empty,
// not silently discard pending entries.

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

void CrossProvinceDeltaBuffer::push(CrossProvinceDelta delta) {
    entries.push_back(std::move(delta));
}

}  // namespace econlife
