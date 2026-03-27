#pragma once

// WorldGenerator — creates a complete, game-ready WorldState from seed + CSV data.
//
// This is the V1 world generator. It reads goods definitions from CSV files,
// creates provinces with varied geography/climate/resources, seeds NPC populations
// and businesses, and wires all markets. The output is a WorldState ready for
// tick execution.
//
// The full 11-stage geological pipeline (WorldGen v0.18) is future scope.
// This generator produces economically diverse, playable worlds using
// DeterministicRNG for full reproducibility.

#include <string>

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/facility_type_catalog.h"
#include "core/world_gen/goods_catalog.h"
#include "core/world_gen/recipe_catalog.h"
#include "core/world_gen/technology_catalog.h"
#include "core/world_state/geography.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// WorldGeneratorConfig — tuning knobs for world creation
// ---------------------------------------------------------------------------
struct WorldGeneratorConfig {
    uint64_t seed = 42;
    uint32_t province_count = 6;       // V1 target: 6
    uint32_t npc_count = 2000;         // V1 target: 2,000 significant NPCs
    uint8_t starting_era = 1;          // era 1 = year 2000
    uint8_t max_good_tier = 1;         // tier 0-1 goods available at start
    float resource_richness = 1.0f;    // 0.5-2.0 multiplier on deposit quantities
    float corruption_baseline = 0.2f;  // 0.0-1.0 starting corruption
    float criminal_baseline = 0.05f;   // 0.0-1.0 starting criminal dominance
    std::string goods_directory;       // path to packages/base_game/goods/
    std::string recipes_directory;     // path to packages/base_game/recipes/
    std::string
        facility_types_filepath;  // path to packages/base_game/facility_types/facility_types.csv
    std::string technology_directory;  // path to packages/base_game/technology/
};

// ---------------------------------------------------------------------------
// WorldGenerator — produces complete WorldState
// ---------------------------------------------------------------------------
class WorldGenerator {
   public:
    // Generate a complete WorldState from config + CSV data.
    // Returns a fully wired WorldState ready for tick execution.
    // The PlayerCharacter must be wired separately (world.player = &player).
    static WorldState generate(const WorldGeneratorConfig& config);

    // Generate and also create a default PlayerCharacter.
    // Returns {WorldState, PlayerCharacter} pair.
    struct WorldWithPlayer {
        WorldState world;
        PlayerCharacter player;
    };
    static WorldWithPlayer generate_with_player(const WorldGeneratorConfig& config);

   private:
    // Province archetypes for economic diversity.
    enum class ProvinceArchetype : uint8_t {
        industrial_hub = 0,    // manufacturing, technology, high infrastructure
        agricultural = 1,      // farming, food processing, high arable land
        resource_rich = 2,     // mining, energy extraction, geological deposits
        coastal_trade = 3,     // port city, trade hub, maritime connections
        financial_center = 4,  // banking, services, high education
        mixed_economy = 5,     // balanced, no strong specialization
    };

    static void create_nation(WorldState& world, uint32_t province_count);
    static void create_provinces(WorldState& world, DeterministicRNG& rng,
                                 const WorldGeneratorConfig& config);
    static void create_province_links(WorldState& world);
    static void create_markets(WorldState& world, DeterministicRNG& rng,
                               const GoodsCatalog& catalog, const WorldGeneratorConfig& config);
    static void create_npcs(WorldState& world, DeterministicRNG& rng,
                            const WorldGeneratorConfig& config);
    static void create_businesses(WorldState& world, DeterministicRNG& rng,
                                  const WorldGeneratorConfig& config);
    static void create_facilities(WorldState& world, DeterministicRNG& rng,
                                  const RecipeCatalog& recipes,
                                  const FacilityTypeCatalog& facility_types,
                                  const WorldGeneratorConfig& config);
    static ProvinceArchetype assign_archetype(DeterministicRNG& rng, uint32_t province_idx,
                                              uint32_t province_count);
    static void apply_archetype(Province& province, ProvinceArchetype archetype,
                                DeterministicRNG& rng, const WorldGeneratorConfig& config);
    static void seed_resource_deposits(Province& province, ProvinceArchetype archetype,
                                       DeterministicRNG& rng, float richness);
    static void seed_technology(WorldState& world, DeterministicRNG& rng,
                                const TechnologyCatalog& tech_catalog,
                                const WorldGeneratorConfig& config);

    // Stage 1 — Tectonic context (WorldGen v0.18).
    // Generates a small set of tectonic plates, assigns each province a tectonic
    // context based on plate boundary proximity, and derives rock_type / geology_type /
    // tectonic_stress / plate_age. Must run after create_provinces() so province
    // geography fields (lat/lng, elevation) are available.
    static void generate_plates(WorldState& world, DeterministicRNG& rng,
                                const WorldGeneratorConfig& config);

    // Called from generate_plates(). Seeds additional resource deposits driven by
    // tectonic context (copper/gold from subduction, coal/oil from sedimentary basin,
    // etc.), layered on top of the archetype-driven deposits from create_provinces().
    static void seed_tectonic_deposits(Province& province, DeterministicRNG& rng,
                                       float richness);

    // Stage 2 derived — Terrain flag detection (WorldGen v0.18).
    // Detects mountain passes (high-terrain chokepoints) and island isolation.
    // Must run after create_province_links() so ProvinceLink vectors are populated.
    static void detect_terrain_flags(WorldState& world);

    // Stage 10 — World commentary (WorldGen v0.18).
    // Generates province_lore strings from tectonic context, climate, and archetype.
    // Must run after generate_plates() and create_provinces() (reads all province fields).
    static void generate_commentary(WorldState& world, DeterministicRNG& rng);
};

}  // namespace econlife
