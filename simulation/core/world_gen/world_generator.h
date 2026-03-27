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

    // Stage 4a — Province geography refinement (WorldGen v0.18; simplified pass).
    // Applies terrain_roughness → elevation correlation so mountainous provinces have
    // physically consistent altitude. Applies elevation lapse rate to all three
    // temperature fields (6.5 °C / 1 000 m environmental lapse rate).
    // Must run BEFORE detect_terrain_flags() so mountain pass detection uses corrected
    // elevation. No RNG needed — fully deterministic from already-set Province fields.
    static void refine_province_geography(WorldState& world);

    // Stage 4b — Economic geography seeding (WorldGen v0.18; simplified pass).
    // Derives trade_openness from port_capacity + river_access + landlocked/island status,
    // fixing the V1 archetype bug where only coastal_trade sets trade_openness (others 0.0).
    // Refines wildfire_vulnerability from KoppenZone + drought_vulnerability.
    // Adjusts formal_employment_rate for infrastructure richness and corruption.
    // Must run AFTER detect_special_features() so island_isolation and has_permafrost
    // flags are available. No RNG needed.
    static void seed_economic_geography(WorldState& world);

    // Stage 5+6 — Soils and Biomes (WorldGen v0.18; simplified pass).
    // Adjusts agricultural_productivity from geology_type + KoppenZone soil fertility model.
    // Refines forest_coverage, drought_vulnerability, flood_vulnerability from climate zone.
    // Must run after generate_plates() (reads geology_type) and apply_archetype() (reads
    // koppen_zone and initializes agricultural_productivity / forest_coverage baselines).
    static void derive_soils_and_biomes(WorldState& world, DeterministicRNG& rng);

    // Stage 7 — Special terrain features (WorldGen v0.18; complete pass).
    // Sets has_permafrost (latitude > 66.5 or ET/EF koppen) and has_fjord (coastal high-relief
    // high-latitude). Applies permafrost accessibility lock to CrudeOil / NaturalGas deposits.
    // Must run after derive_soils_and_biomes() and create_province_links().
    static void detect_special_features(WorldState& world);

    // Stage 9 — Population attractiveness (WorldGen v0.18; simplified pass).
    // Re-weights total_population from a settlement attractiveness score derived from soil
    // fertility, infrastructure, climate stress, and hazard factors. Bounded ±40% of archetype
    // baseline so archetypes remain dominant while geology/climate add meaningful variation.
    // Must run after detect_special_features() (reads has_permafrost).
    static void seed_population_attractiveness(WorldState& world, DeterministicRNG& rng);

    // Stage 10 — World commentary (WorldGen v0.18).
    // Generates province_lore strings from tectonic context, climate, and archetype.
    // Must run after generate_plates() and create_provinces() (reads all province fields).
    static void generate_commentary(WorldState& world, DeterministicRNG& rng);
};

}  // namespace econlife
