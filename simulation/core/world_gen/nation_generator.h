#pragma once

// NationGenerator — forms nations from physics-derived province data.
//
// Stage 9.5: Voronoi growth from seed provinces with terrain-weighted Dijkstra.
// Stage 9.6: Nomadic population from pastoral carrying capacity.
// Stage 9.7: Capital seeding from highest settlement attractiveness per nation.
//
// Split from WorldGenerator to keep world generation focused on physical
// geography (Stages 1–8) and let nation formation be its own concern.

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"

namespace econlife {

class NationGenerator {
   public:
    // Stage 9.5 — Nation formation (WorldGen v0.18).
    // Replaces single placeholder nation with Voronoi-growth multi-nation assignment.
    // Seeds nation territories, language families, border changes, infra_gap.
    // Must run after seed_population_attractiveness() (reads settlement_attractiveness).
    static void form_nations(WorldState& world, DeterministicRNG& rng,
                             const WorldGeneratorConfig& config);

    // Stage 9.6 — Nomadic population (WorldGen v0.18).
    // Seeds pastoral_carrying_capacity and nomadic_population_fraction from climate.
    static void seed_nomadic_population(WorldState& world, DeterministicRNG& rng,
                                        const WorldGeneratorConfig& config);

    // Stage 9.7 — Nation capital seeding (WorldGen v0.18).
    // Selects highest settlement_attractiveness province per nation as capital.
    static void seed_nation_capitals(WorldState& world, const WorldGeneratorConfig& config);
};

}  // namespace econlife
