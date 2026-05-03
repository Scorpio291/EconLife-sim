#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace econlife {

// Thread pool for province-parallel dispatch.
// Worker threads wait on a shared task queue. Tasks are submitted via submit()
// and parallel_for(). Determinism is maintained by the caller: province results
// are collected per-index and merged in ascending order after all tasks complete.
//
// When num_threads == 1, parallel_for() runs inline on the calling thread
// (zero overhead; identical to the sequential path).

class ThreadPool {
   public:
    explicit ThreadPool(uint32_t num_threads = 1);
    ~ThreadPool();

    // Non-copyable, non-movable (worker threads hold references to members).
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    uint32_t num_threads() const noexcept { return num_threads_; }

    // Submit a single task. Returns a future that completes when the task finishes.
    // The future propagates exceptions thrown by the task.
    std::future<void> submit(std::function<void()> task);

    // Dispatch [0, count) tasks in parallel, block until all complete.
    // task(uint32_t index) is called once for each index in [0, count).
    // When num_threads_ <= 1, executes sequentially on the calling thread.
    // Any exception from a task is rethrown after all tasks complete.
    template <typename F>
    void parallel_for(uint32_t count, F&& task);

   private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    uint32_t num_threads_;
};

// ---------------------------------------------------------------------------
// Template implementation
// ---------------------------------------------------------------------------

template <typename F>
void ThreadPool::parallel_for(uint32_t count, F&& task) {
    if (count == 0)
        return;

    // Single-threaded fast path: no synchronization overhead.
    if (num_threads_ <= 1) {
        for (uint32_t i = 0; i < count; ++i) {
            task(i);
        }
        return;
    }

    // Multi-threaded path: submit all tasks, collect futures, wait.
    std::vector<std::future<void>> futures;
    futures.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        futures.push_back(submit([&task, i]() { task(i); }));
    }

    // Wait for all tasks and propagate the first exception encountered.
    std::exception_ptr first_error;
    for (auto& f : futures) {
        try {
            f.get();
        } catch (...) {
            if (!first_error) {
                first_error = std::current_exception();
            }
        }
    }
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

}  // namespace econlife
