#pragma once

// drain_deferred_work — pops all due items from the DeferredWorkQueue and
// dispatches them by WorkType, writing results to a DeltaBuffer.
// Called by TickOrchestrator at the start of each tick (after cross-province
// delta application, before domain modules).
//
// Recurring work is rescheduled automatically:
//   - npc_relationship_decay → +30 ticks
//   - evidence_decay_batch   → +7 ticks

namespace econlife {

struct WorldState;
struct DeltaBuffer;

// Pop all items where due_tick <= current_tick, execute them, and write
// results to delta. Recurring items are pushed back onto the queue.
void drain_deferred_work(WorldState& world, DeltaBuffer& delta);

}  // namespace econlife
