#pragma once

// SettlementGenerator — population attractiveness, NPC seeding, and business creation.
//
// Stage 9:   Settlement attractiveness from geography/climate.
// Post-9:    NPC population and business creation from demographics.
//
// Split from WorldGenerator to keep world generation focused on physical
// geography and let settlement/population logic be its own concern.

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"

namespace econlife {

class SettlementGenerator {
   public:
    // Stage 9 — Population attractiveness (WorldGen v0.18).
    // Adjusts total_population from geology/climate score (bounded ±40%).
    // Must run after detect_special_features() so permafrost/island flags are set.
    static void seed_population_attractiveness(WorldState& world, DeterministicRNG& rng,
                                                const WorldGeneratorConfig& config);

    // NPC population seeding from demographics.
    static void create_npcs(WorldState& world, DeterministicRNG& rng,
                            const WorldGeneratorConfig& config);

    // Business creation from NPC population.
    static void create_businesses(WorldState& world, DeterministicRNG& rng,
                                  const WorldGeneratorConfig& config);
};

}  // namespace econlife
