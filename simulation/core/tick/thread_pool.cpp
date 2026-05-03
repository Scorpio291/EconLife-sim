// ThreadPool — worker thread management and task dispatch.
// See thread_pool.h for interface documentation.

#include "core/tick/thread_pool.h"

namespace econlife {

ThreadPool::ThreadPool(uint32_t num_threads) : num_threads_(num_threads) {
    // Only spawn worker threads when num_threads > 1.
    // With num_threads == 1, parallel_for runs inline on the caller.
    if (num_threads_ <= 1)
        return;

    workers_.reserve(num_threads_);
    for (uint32_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
}

std::future<void> ThreadPool::submit(std::function<void()> task) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push([p = std::move(promise), t = std::move(task)]() {
            try {
                t();
                p->set_value();
            } catch (...) {
                p->set_exception(std::current_exception());
            }
        });
    }
    cv_.notify_one();
    return future;
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty())
                return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

}  // namespace econlife
