#pragma once

#include <cstdint>

namespace econlife {

// Minimal thread pool for province-parallel dispatch.
// Full implementation will use std::thread + work-stealing queue.
// For now, provides the interface that TickOrchestrator needs.

class ThreadPool {
public:
    explicit ThreadPool(uint32_t num_threads = 1) : num_threads_(num_threads) {}

    uint32_t num_threads() const noexcept { return num_threads_; }

private:
    uint32_t num_threads_;
};

}  // namespace econlife
