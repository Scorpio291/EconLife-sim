// Unit tests for DeterministicRNG (SplitMix64).
// All tests tagged with [rng][tier0].

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <vector>

#include "core/rng/deterministic_rng.h"

using econlife::DeterministicRNG;

// ---------------------------------------------------------------------------
// Determinism tests
// ---------------------------------------------------------------------------

TEST_CASE("same_seed_same_output", "[rng][tier0]") {
    DeterministicRNG rng1(42);
    DeterministicRNG rng2(42);

    for (int i = 0; i < 1000; ++i) {
        REQUIRE(rng1.next_u64() == rng2.next_u64());
    }
}

TEST_CASE("different_seeds_different_output", "[rng][tier0]") {
    DeterministicRNG rng1(42);
    DeterministicRNG rng2(99);

    // It is astronomically unlikely that two different seeds produce
    // the same sequence for even a handful of draws.
    bool any_different = false;
    for (int i = 0; i < 100; ++i) {
        if (rng1.next_u64() != rng2.next_u64()) {
            any_different = true;
            break;
        }
    }
    REQUIRE(any_different);
}

// ---------------------------------------------------------------------------
// Fork tests
// ---------------------------------------------------------------------------

TEST_CASE("fork_is_deterministic", "[rng][tier0]") {
    DeterministicRNG base1(12345);
    DeterministicRNG base2(12345);

    auto fork1 = base1.fork(5);
    auto fork2 = base2.fork(5);

    for (int i = 0; i < 100; ++i) {
        REQUIRE(fork1.next_u64() == fork2.next_u64());
    }
}

TEST_CASE("fork_independence", "[rng][tier0]") {
    DeterministicRNG base(12345);

    auto fork1 = base.fork(1);
    auto fork2 = base.fork(2);

    bool any_different = false;
    for (int i = 0; i < 100; ++i) {
        if (fork1.next_u64() != fork2.next_u64()) {
            any_different = true;
            break;
        }
    }
    REQUIRE(any_different);
}

TEST_CASE("fork_does_not_mutate_parent", "[rng][tier0]") {
    DeterministicRNG rng1(42);
    DeterministicRNG rng2(42);

    // Fork from rng1 — parent state should not change (fork is const).
    [[maybe_unused]] auto forked = rng1.fork(7);

    // rng1 and rng2 should still produce identical output.
    for (int i = 0; i < 100; ++i) {
        REQUIRE(rng1.next_u64() == rng2.next_u64());
    }
}

// ---------------------------------------------------------------------------
// Distribution and bounds tests
// ---------------------------------------------------------------------------

TEST_CASE("distribution_uniformity", "[rng][tier0]") {
    DeterministicRNG rng(777);
    constexpr int n = 100'000;
    constexpr int buckets = 10;
    std::vector<int> counts(buckets, 0);

    for (int i = 0; i < n; ++i) {
        double v = rng.next_double();
        REQUIRE(v >= 0.0);
        REQUIRE(v < 1.0);
        auto bucket = static_cast<int>(v * buckets);
        if (bucket >= buckets)
            bucket = buckets - 1;  // guard for v very close to 1.0
        counts[static_cast<size_t>(bucket)]++;
    }

    // Each bucket should have approximately n/buckets = 10,000 draws.
    // Allow +/- 20% tolerance (8,000 to 12,000).
    const double expected = static_cast<double>(n) / buckets;
    for (int i = 0; i < buckets; ++i) {
        const double ratio = static_cast<double>(counts[static_cast<size_t>(i)]) / expected;
        REQUIRE(ratio > 0.8);
        REQUIRE(ratio < 1.2);
    }
}

TEST_CASE("next_float_bounds", "[rng][tier0]") {
    DeterministicRNG rng(555);
    for (int i = 0; i < 10'000; ++i) {
        float v = rng.next_float();
        REQUIRE(v >= 0.0f);
        REQUIRE(v < 1.0f);
    }
}

TEST_CASE("int_range_bounds", "[rng][tier0]") {
    DeterministicRNG rng(333);
    constexpr int32_t lo = 5;
    constexpr int32_t hi = 10;
    bool saw_lo = false;
    bool saw_hi = false;

    for (int i = 0; i < 10'000; ++i) {
        int32_t v = rng.next_int(lo, hi);
        REQUIRE(v >= lo);
        REQUIRE(v <= hi);
        if (v == lo)
            saw_lo = true;
        if (v == hi)
            saw_hi = true;
    }

    // With 10,000 draws over 6 values, we should hit the extremes.
    REQUIRE(saw_lo);
    REQUIRE(saw_hi);
}

TEST_CASE("next_int_min_equals_max", "[rng][tier0]") {
    DeterministicRNG rng(111);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(rng.next_int(7, 7) == 7);
    }
}

TEST_CASE("next_uint_bounds", "[rng][tier0]") {
    DeterministicRNG rng(444);
    for (int i = 0; i < 10'000; ++i) {
        uint32_t v = rng.next_uint(6);
        REQUIRE(v < 6);
    }
}

TEST_CASE("next_uint_max_zero_returns_zero", "[rng][tier0]") {
    DeterministicRNG rng(222);
    REQUIRE(rng.next_uint(0) == 0);
    REQUIRE(rng.next_uint(1) == 0);
}

// ---------------------------------------------------------------------------
// Performance test
// ---------------------------------------------------------------------------

TEST_CASE("performance_next_u64", "[rng][tier0][!benchmark]") {
    DeterministicRNG rng(1);
    constexpr int n = 1'000'000;

    auto start = std::chrono::high_resolution_clock::now();
    volatile uint64_t sink = 0;
    for (int i = 0; i < n; ++i) {
        sink = rng.next_u64();
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ns_per_call = static_cast<double>(ns) / n;

    // Contract: < 10ns per call. Use generous margin for CI variance.
    // We check < 50ns to accommodate debug builds and slow CI machines.
    REQUIRE(ns_per_call < 50.0);

    // Informational output (visible with --output-on-failure or -s).
    WARN("next_u64: " << ns_per_call << " ns/call (" << n << " calls)");
}

TEST_CASE("performance_fork", "[rng][tier0][!benchmark]") {
    DeterministicRNG rng(1);
    constexpr int n = 1'000'000;

    auto start = std::chrono::high_resolution_clock::now();
    volatile uint64_t sink = 0;
    for (int i = 0; i < n; ++i) {
        auto forked = rng.fork(static_cast<uint32_t>(i));
        sink = forked.state();
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ns_per_call = static_cast<double>(ns) / n;

    // Contract: < 50ns per fork. Allow margin for debug builds.
    REQUIRE(ns_per_call < 250.0);

    WARN("fork: " << ns_per_call << " ns/call (" << n << " calls)");
}
