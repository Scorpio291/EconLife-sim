// DeterministicRNG — SplitMix64 implementation.
// All simulation randomness goes through this class.
// Same seed + same call sequence = identical output on any platform.

#include "core/rng/deterministic_rng.h"

namespace econlife {

uint64_t DeterministicRNG::next_u64() {
    state_ += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

double DeterministicRNG::next_double() {
    // Use top 53 bits for a uniform double in [0.0, 1.0).
    // IEEE 754 double has 53 bits of mantissa precision.
    return static_cast<double>(next_u64() >> 11) * (1.0 / static_cast<double>(1ULL << 53));
}

float DeterministicRNG::next_float() {
    // Use top 24 bits for a uniform float in [0.0f, 1.0f).
    // IEEE 754 float has 24 bits of mantissa precision.
    return static_cast<float>(next_u64() >> 40) * (1.0f / static_cast<float>(1U << 24));
}

int32_t DeterministicRNG::next_int(int32_t min, int32_t max) {
    if (min >= max) {
        return min;
    }

    // Range width as uint64_t to avoid overflow.
    const auto range = static_cast<uint64_t>(max) - static_cast<uint64_t>(min) + 1;

    // Unbiased rejection sampling.
    // Reject values that would cause modulo bias.
    const uint64_t limit = (UINT64_MAX / range) * range;
    uint64_t r;
    do {
        r = next_u64();
    } while (r >= limit);

    return static_cast<int32_t>(min + static_cast<int64_t>(r % range));
}

uint32_t DeterministicRNG::next_uint(uint32_t max) {
    if (max <= 1) {
        return 0;
    }

    // Unbiased rejection sampling for [0, max).
    const auto range = static_cast<uint64_t>(max);
    const uint64_t limit = (UINT64_MAX / range) * range;
    uint64_t r;
    do {
        r = next_u64();
    } while (r >= limit);

    return static_cast<uint32_t>(r % range);
}

DeterministicRNG DeterministicRNG::fork(uint32_t context_id) const {
    // Derive a new seed by mixing state_ with context_id,
    // then running one round of SplitMix64.
    uint64_t seed = state_ ^ (static_cast<uint64_t>(context_id) * 0x517cc1b727220a95ULL);

    // One SplitMix64 round to avalanche the bits.
    seed += 0x9e3779b97f4a7c15ULL;
    uint64_t z = seed;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z = z ^ (z >> 31);

    return DeterministicRNG(z);
}

}  // namespace econlife
