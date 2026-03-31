#pragma once

// drain_deferred_work — pops all due items from the DeferredWorkQueue and
// dispatches them by WorkType, writing results to a DeltaBuffer.
// Called by TickOrchestrator at the start of each tick (after cross-province
// delta application, before domain modules).
//
// Recurring work is rescheduled automatically using config-driven intervals.

#include <cstdint>

namespace econlife {

struct WorldState;
struct DeltaBuffer;

// Configuration for deferred work handlers — extracted from PackageConfig
// at the orchestrator level so drain_deferred_work stays config-aware.
struct DrainConfig {
    uint32_t relationship_decay_interval = 30;
    uint32_t evidence_decay_interval = 7;
    float trust_decay_rate_per_batch = 0.02f;
    float fear_decay_rate_per_batch = 0.03f;
};

// Pop all items where due_tick <= current_tick, execute them, and write
// results to delta. Recurring items are pushed back onto the queue.
void drain_deferred_work(WorldState& world, DeltaBuffer& delta,
                         const DrainConfig& cfg = {});

}  // namespace econlife
