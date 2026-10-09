#pragma once

// Nation seed placement (WorldGen §9.5.1).
//
// Seeds are drawn in attractiveness-weighted random order and kept when no
// kept seed lies within `separation_hops` link hops. The draw is the spec's
// weighted_sample_without_replacement; the separation rule is never relaxed
// and no seed is forced to meet the requested count.
//
// What this adds to the spec is a guard against an unlucky order. The weighted
// draw is the priority; the separation rule is the constraint. A plain greedy
// pass can stop short of what the geography admits, purely because of the order
// it met the candidates in. So, for up to kExactCandidateLimit candidates:
//   - the exact maximum number of separated seeds is computed (maximum
//     independent set of the conflict graph), and
//   - seeds are taken in priority order, passing a candidate over only when
//     taking it would make that maximum (or the request, if smaller)
//     unreachable. When the greedy pass would succeed this IS the greedy pass.
// A shortfall is then always the geography's, and is reported as such.
//
// Above that size, or when the search spends its node budget, the exact search is too expensive for
// world generation (a documented scaleback): greedy plus 1-for-2 exchanges, bounded above by a ball
// cover (balls of radius floor(s/2) hops each hold at most one seed). The
// report says "undetermined" when the bound is not tight enough to decide.

#include <cstdint>
#include <vector>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/geography.h"

namespace econlife {

struct NationSeedPlacementInput {
    // Undirected adjacency over ALL provinces (indices into the province
    // array). Distances are counted through every province, habitable or not.
    std::vector<std::vector<uint32_t>> adjacency;
    // Candidate province indices (habitable), ascending.
    std::vector<uint32_t> candidates;
    // Selection weight per candidate, parallel to `candidates` (spec:
    // settlement_attractiveness squared). Non-positive weights draw last.
    std::vector<double> weights;
    uint32_t requested = 0;
    uint32_t separation_hops = 0;  // seeds must be MORE than this many hops apart
};

struct NationSeedPlacementResult {
    std::vector<uint32_t> seeds;  // province indices, in selection-priority order
    NationSeedReport report;
};

// Candidate count up to which the exact maximum is attempted, and the number of
// search nodes it may spend (all placement searches together) before giving up
// and falling back to the bounded heuristic. Both are performance scalebacks,
// not model constants: they decide only whether a shortfall can be PROVEN to be
// the geography's, never how many seeds are placed by rule.
inline constexpr uint32_t kExactCandidateLimit = 256;
inline constexpr uint64_t kExactSearchNodeBudget = 200'000;

NationSeedPlacementResult place_nation_seeds(const NationSeedPlacementInput& input,
                                             DeterministicRNG& rng);

}  // namespace econlife
