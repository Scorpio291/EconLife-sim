#pragma once

// History generation driver (mechanical history generation, P1).
//
// Generates a world and then advances the full simulation orchestrator for a span
// of pre-game in-game years, so the world the player eventually enters is the
// *emergent outcome* of simulated history rather than a hand-seeded snapshot.
//
// With WorldGeneratorConfig::founding_seed_mode the world starts as physical
// substrate + founding population and the economy bootstraps as history runs (once
// entity genesis, P2, lands); with the full seed it evolves the seeded economy
// forward. Deterministic from the world seed.
//
// See docs/design/EconLife_Mechanical_History_Generation_Plan.md.

#include <cstdint>

#include "core/config/package_config.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"

namespace econlife {

// Generate a world from gen_config, then run the orchestrator for history_years
// in-game years (365 ticks/year) before any player agency. Returns the evolved
// world (the "present" to enter). history_years == 0 returns the freshly generated
// world unchanged (equivalent to today's instant-world generation).
WorldState generate_world_with_history(const WorldGeneratorConfig& gen_config,
                                       const PackageConfig& pkg_config, uint32_t history_years,
                                       uint32_t threads = 1);

}  // namespace econlife
