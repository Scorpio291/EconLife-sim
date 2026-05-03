// ThreadPool unit tests — verify task dispatch, parallel_for, and shutdown.
// All tests tagged [thread_pool][tier0].

#include "core/tick/thread_pool.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <numeric>
#include <vector>

using namespace econlife;

// --- Basic construction ---

TEST_CASE("thread pool constructs with 1 thread", "[thread_pool][tier0]") {
    ThreadPool pool(1);
    REQUIRE(pool.num_threads() == 1);
}

TEST_CASE("thread pool constructs with multiple threads", "[thread_pool][tier0]") {
    ThreadPool pool(4);
    REQUIRE(pool.num_threads() == 4);
}

TEST_CASE("thread pool default constructs with 1 thread", "[thread_pool][tier0]") {
    ThreadPool pool;
    REQUIRE(pool.num_threads() == 1);
}

// --- submit() ---

TEST_CASE("submit executes a single task", "[thread_pool][tier0]") {
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    auto future = pool.submit([&]() { counter.fetch_add(1); });
    future.get();
    REQUIRE(counter.load() == 1);
}

TEST_CASE("submit propagates exceptions", "[thread_pool][tier0]") {
    ThreadPool pool(2);
    auto future = pool.submit([]() { throw std::runtime_error("test error"); });
    REQUIRE_THROWS_AS(future.get(), std::runtime_error);
}

TEST_CASE("submit handles multiple concurrent tasks", "[thread_pool][tier0]") {
    ThreadPool pool(4);
    constexpr int task_count = 100;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    futures.reserve(task_count);

    for (int i = 0; i < task_count; ++i) {
        futures.push_back(pool.submit([&]() { counter.fetch_add(1); }));
    }
    for (auto& f : futures) {
        f.get();
    }
    REQUIRE(counter.load() == task_count);
}

// --- parallel_for() ---

TEST_CASE("parallel_for with zero count does nothing", "[thread_pool][tier0]") {
    ThreadPool pool(2);
    int called = 0;
    pool.parallel_for(0, [&](uint32_t) { ++called; });
    REQUIRE(called == 0);
}

TEST_CASE("parallel_for single-threaded runs inline", "[thread_pool][tier0]") {
    ThreadPool pool(1);
    std::vector<uint32_t> indices;
    pool.parallel_for(5, [&](uint32_t i) { indices.push_back(i); });
    // Single-threaded path runs sequentially 0,1,2,3,4.
    REQUIRE(indices == std::vector<uint32_t>{0, 1, 2, 3, 4});
}

TEST_CASE("parallel_for multi-threaded visits all indices", "[thread_pool][tier0]") {
    ThreadPool pool(4);
    constexpr uint32_t count = 50;
    std::atomic<uint32_t> sum{0};

    pool.parallel_for(count, [&](uint32_t i) { sum.fetch_add(i); });

    // Sum of 0..49 = 1225
    REQUIRE(sum.load() == (count * (count - 1)) / 2);
}

TEST_CASE("parallel_for propagates first exception", "[thread_pool][tier0]") {
    ThreadPool pool(4);
    REQUIRE_THROWS_AS(pool.parallel_for(10,
                                        [](uint32_t i) {
                                            if (i == 5)
                                                throw std::runtime_error("task 5 failed");
                                        }),
                      std::runtime_error);
}

TEST_CASE("parallel_for results are independent per index", "[thread_pool][tier0]") {
    // Simulate province-parallel pattern: each index writes to its own slot.
    ThreadPool pool(4);
    constexpr uint32_t count = 6;
    std::vector<float> results(count, 0.0f);

    pool.parallel_for(count, [&](uint32_t i) {
        // Each province computes independently. No shared state mutation.
        results[i] = static_cast<float>(i * i);
    });

    for (uint32_t i = 0; i < count; ++i) {
        REQUIRE(results[i] == static_cast<float>(i * i));
    }
}

// --- Determinism ---

TEST_CASE("parallel_for deterministic merge order", "[thread_pool][tier0]") {
    // Verify that results collected in index order produce deterministic output.
    // This mirrors the orchestrator's province merge pattern.
    constexpr uint32_t province_count = 6;

    auto run_once = [](uint32_t num_threads) -> std::vector<float> {
        ThreadPool pool(num_threads);
        std::vector<float> per_province(province_count, 0.0f);

        pool.parallel_for(province_count, [&](uint32_t p) {
            // Deterministic computation per province.
            per_province[p] = static_cast<float>(p) * 1.5f + 0.1f;
        });

        // Merge in ascending index order (same as orchestrator).
        std::vector<float> merged;
        merged.reserve(province_count);
        for (uint32_t p = 0; p < province_count; ++p) {
            merged.push_back(per_province[p]);
        }
        return merged;
    };

    auto result_1thread = run_once(1);
    auto result_4thread = run_once(4);
    auto result_6thread = run_once(6);

    REQUIRE(result_1thread == result_4thread);
    REQUIRE(result_1thread == result_6thread);
}

// --- Shutdown ---

TEST_CASE("thread pool destructor joins all workers", "[thread_pool][tier0]") {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 20; ++i) {
            pool.submit([&]() { counter.fetch_add(1); });
        }
        // Destructor should wait for all tasks to complete.
    }
    REQUIRE(counter.load() == 20);
}
