#pragma once

#include <memory>
#include <vector>

#include "tick_module.h"

namespace econlife {

// Forward declarations — in econlife namespace to match type definitions
struct WorldState;
class ThreadPool;

// Manages module registration, topological sorting, and tick execution.
// Module list is locked after finalize_registration() — subsequent
// register_module() calls are rejected.
//
// Cycle detection: If Module A declares runs_after(["module_b"]) and Module B
// declares runs_after(["module_a"]), resolve_and_sort() panics at startup
// with a descriptive error naming the cycle.
class TickOrchestrator {
   public:
    // Called internally by PackageManager::load_all() for each compiled module.
    void register_module(std::unique_ptr<ITickModule> module);

    // Runs topological sort (Kahn's algorithm) on modules using runs_after/runs_before.
    // Validates all named dependencies exist. Panics with named cycle if detected.
    // Locks module list after completion.
    void finalize_registration();

    // Main tick entry point. Asserts finalize_registration() was called.
    // Province-parallel modules dispatch to thread_pool; sequential modules
    // run on main thread. Delta buffers merged in ascending province index order.
    void execute_tick(WorldState& state, ThreadPool& thread_pool);

    bool is_finalized() const noexcept { return finalized_; }

    const std::vector<std::unique_ptr<ITickModule>>& modules() const { return modules_; }

   private:
    std::vector<std::unique_ptr<ITickModule>> modules_;
    bool finalized_ = false;

    void resolve_and_sort();
};

}  // namespace econlife
