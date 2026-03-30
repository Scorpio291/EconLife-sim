#pragma once

// FacilityGenerator — facility assignment and technology seeding.
//
// Assigns production facilities to businesses and seeds initial technology.
//
// Split from WorldGenerator to keep world generation focused on physical
// geography and let facility/technology logic be its own concern.

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/facility_type_catalog.h"
#include "core/world_gen/recipe_catalog.h"
#include "core/world_gen/technology_catalog.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/world_state.h"

namespace econlife {

class FacilityGenerator {
   public:
    // Assign 1-3 facilities per business from recipe + facility type catalogs.
    static void create_facilities(WorldState& world, DeterministicRNG& rng,
                                  const RecipeCatalog& recipes,
                                  const FacilityTypeCatalog& facility_types,
                                  const WorldGeneratorConfig& config);

    // Seed initial technology holdings for the world.
    static void seed_technology(WorldState& world, DeterministicRNG& rng,
                                const TechnologyCatalog& tech_catalog,
                                const WorldGeneratorConfig& config);
};

}  // namespace econlife
