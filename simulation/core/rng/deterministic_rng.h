#pragma once

#include <cstdint>

namespace econlife {

// Deterministic random number generator.
// All random draws in the simulation MUST go through this class.
// Never use std::rand, system time, or thread ID as entropy sources.
// Same seed + same call sequence = same outputs.
//
// Implementation: SplitMix64 or similar fast, deterministic PRNG.
// Seed is derived from WorldState.world_seed + per-call context.
class DeterministicRNG {
   public:
    explicit DeterministicRNG(uint64_t seed) : state_(seed) {}

    // Returns a uniformly distributed uint64_t.
    uint64_t next_u64();

    // Returns a uniformly distributed double in [0.0, 1.0).
    double next_double();

    // Returns a uniformly distributed float in [0.0, 1.0).
    float next_float();

    // Returns a uniformly distributed integer in [min, max] (inclusive).
    int32_t next_int(int32_t min, int32_t max);

    // Returns a uniformly distributed uint32_t in [0, max) (exclusive).
    uint32_t next_uint(uint32_t max);

    // Fork a new RNG with a derived seed for province-parallel work.
    // Each province gets a deterministic sub-stream.
    DeterministicRNG fork(uint32_t context_id) const;

    uint64_t state() const noexcept { return state_; }

   private:
    uint64_t state_;
};

}  // namespace econlife
