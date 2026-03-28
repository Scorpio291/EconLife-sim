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

#include <nlohmann/json_fwd.hpp>

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
    std::string output_world_file;   // if non-empty, write world.json to this path after generation

    // -----------------------------------------------------------------------
    // TerrainParams — thresholds for terrain detection and physical refinement
    // -----------------------------------------------------------------------
    struct TerrainParams {
        // Elevation scaling from terrain_roughness (Stage 4a).
        // Final factor = elev_roughness_base + roughness * elev_roughness_range.
        // roughness 0.0 → base; roughness 1.0 → base + range.
        float elev_roughness_base  = 0.30f;  // factor at roughness = 0
        float elev_roughness_range = 1.50f;  // additional factor across [0, 1]
        float elev_floor_m         = 5.0f;   // minimum elevation after scaling (m)

        // Mountain pass detection thresholds (Stage 2 derived).
        float pass_roughness_min         = 0.65f;  // terrain_roughness floor to qualify
        float pass_elevation_min_m       = 350.0f; // elevation_avg_m floor to qualify
        float pass_low_neighbor_fraction = 0.60f;  // neighbor elev < this * pass elev = "low"
        float pass_transit_cost_factor   = 0.60f;  // multiply link cost on pass routes
        float pass_transit_cost_floor    = 0.10f;  // minimum after reduction

        // Permafrost special feature (Stage 7).
        // Scientific: Arctic Circle = 66.5°; keep default but allow fictional worlds to tune.
        float permafrost_latitude_min    = 66.5f;  // provinces above this get has_permafrost
        float permafrost_ag_factor       = 0.40f;  // ag_productivity *= this for permafrost
        float permafrost_ag_floor        = 0.02f;  // ag_productivity minimum

        // Fjord special feature (Stage 7).
        float fjord_min_coastal_km      = 100.0f; // coastal_length_km threshold
        float fjord_min_roughness       = 0.55f;  // terrain_roughness threshold
        float fjord_min_latitude        = 50.0f;  // latitude threshold (glacial origin)
        float fjord_maritime_cost_add   = 0.15f;  // added to Maritime link transit_terrain_cost

        // Estuary special feature (Stage 7).
        float estuary_min_river_access  = 0.40f;  // river_access threshold for estuary
        float estuary_min_coastal_km    = 20.0f;   // coastal_length_km threshold
        float estuary_max_roughness     = 0.45f;   // terrain must be moderate or lower
        float estuary_port_min          = 0.55f;   // port_capacity range for estuaries
        float estuary_port_max          = 0.75f;

        // Ria coast special feature (Stage 7).
        float ria_min_roughness         = 0.30f;   // moderate terrain (drowned valleys)
        float ria_max_roughness         = 0.55f;   // but not extreme (that's fjord territory)
        float ria_max_latitude          = 50.0f;   // below glacial threshold (non-glacial origin)
        float ria_min_coastal_km        = 50.0f;   // needs significant coastline
        float ria_port_min              = 0.70f;   // port_capacity range for rias
        float ria_port_max              = 0.90f;
    } terrain{};

    // -----------------------------------------------------------------------
    // EconomyParams — weights for trade openness, wildfire, and employment
    // -----------------------------------------------------------------------
    struct EconomyParams {
        // Trade openness (Stage 4b).
        // geo_openness = port * port_w + river * river_w + infra * infra_w
        //              - (is_landlocked ? landlocked_penalty : 0)
        //              - (island_isolation ? island_penalty : 0)
        float trade_port_weight        = 0.50f;
        float trade_river_weight       = 0.15f;
        float trade_infra_weight       = 0.10f;
        float trade_landlocked_penalty = 0.10f;
        float trade_island_penalty     = 0.05f;
        // When archetype already set trade_openness: blend(existing, geo) with these weights.
        float trade_existing_blend     = 0.65f;
        float trade_geo_blend          = 0.35f;

        // Wildfire base vulnerability by climate group (Stage 4b).
        float wildfire_mediterranean  = 0.50f;  // Csa, Csb
        float wildfire_steppe_savanna = 0.35f;  // BSh, BSk, Aw
        float wildfire_desert         = 0.20f;  // BWh, BWk
        float wildfire_temperate      = 0.15f;  // Cfa, Cfb, Cfc, Cwa, Dfa, Dfb
        float wildfire_boreal         = 0.20f;  // Dfc, Dfd
        float wildfire_wet_tropical   = 0.06f;  // Af, Am
        float wildfire_polar          = 0.02f;  // ET, EF
        float wildfire_drought_amp    = 0.50f;  // base_wildfire *= (1 + drought_vuln * this)

        // Formal employment adjustments (Stage 4b).
        // adjustment = (infra - 0.5) * infra_scale
        //            - corruption * corruption_scale
        //            - inequality * inequality_scale
        float employment_infra_scale      = 0.15f;
        float employment_corruption_scale = 0.18f;
        float employment_inequality_scale = 0.10f;
    } economy{};

    // -----------------------------------------------------------------------
    // PopulationParams — weights for settlement attractiveness (Stage 9)
    // -----------------------------------------------------------------------
    struct PopulationParams {
        // Attractiveness score contributions (baseline = 0.5 → multiplier = 1.0).
        float ag_productivity_weight  = 0.12f;  // positive
        float infrastructure_weight   = 0.12f;  // positive
        float river_access_weight     = 0.08f;  // positive
        float arable_land_weight      = 0.06f;  // positive
        float tectonic_stress_penalty = 0.12f;  // subtracted
        float drought_penalty         = 0.10f;  // subtracted
        float permafrost_penalty      = 0.20f;  // subtracted if has_permafrost
        float island_penalty          = 0.08f;  // subtracted if island_isolation

        // Population multiplier from score:
        //   multiplier = multiplier_base + score * multiplier_range
        // At default values: score 0.5 → 1.0x; score 0 → 0.60x; score 1 → 1.40x.
        float multiplier_base   = 0.60f;
        float multiplier_range  = 0.80f;

        // Per-province RNG variation: multiplier *= (rng_variation_base + rng * rng_variation_range)
        float rng_variation_base  = 0.95f;
        float rng_variation_range = 0.10f;

        uint32_t population_floor = 10000u;  // minimum total_population after adjustment
    } population{};

    // -----------------------------------------------------------------------
    // HydrologyParams — thresholds for Stage 3 hydrology pass
    // -----------------------------------------------------------------------
    struct HydrologyParams {
        // Snowline derivation: snowline_m = max(0, snowline_base - |lat| * snowline_lat_rate)
        float snowline_base_m       = 5000.0f;  // snowline at equator (tropical glaciers)
        float snowline_lat_rate     = 75.0f;    // drop per degree latitude
        float snowpack_retention    = 0.70f;    // fraction of high-altitude precip held as snow
        float snowmelt_river_scale  = 0.0005f;  // snowpack → river_access conversion
        float melt_decay_km         = 500.0f;   // exponential decay distance for snowmelt propagation

        // Drainage basin: river_access from catchment area
        float catchment_river_scale = 0.15f;    // catchment_area_fraction → river_access weight
        float precip_river_scale    = 0.0008f;  // precipitation_mm → river_access weight

        // Groundwater
        float alluvial_gw_bonus     = 0.30f;    // groundwater_reserve bonus for alluvial fill
        float sedimentary_gw_bonus  = 0.20f;    // groundwater_reserve bonus for sedimentary
        float floodplain_gw_bonus   = 0.15f;    // extra groundwater for high river_access + flat terrain
        float gw_precip_scale       = 0.0004f;  // precipitation contribution to groundwater

        // Alluvial fan detection
        float fan_roughness_min     = 0.55f;    // neighbor terrain_roughness threshold
        float fan_elev_drop_m       = 300.0f;   // elevation drop from neighbor to qualify
        float fan_ag_bonus          = 0.12f;    // agricultural_productivity boost
        float fan_gw_bonus          = 0.15f;    // groundwater_reserve boost

        // Delta detection
        float delta_catchment_min   = 0.40f;    // minimum upstream fraction to qualify as major delta
        float delta_ag_cap          = 0.85f;    // agricultural_productivity cap for deltas
        float delta_flood_floor     = 0.40f;    // minimum flood_vulnerability for deltas
        float delta_port_min        = 0.35f;    // port_capacity range for deltas (min)
        float delta_port_max        = 0.55f;    // port_capacity range for deltas (max)

        // Spring/oasis detection
        float spring_gw_min         = 0.35f;    // minimum groundwater_reserve for spring detection
        float spring_elev_diff_m    = 200.0f;   // recharge zone must be this much higher
        float spring_precip_min_mm  = 400.0f;   // recharge neighbor must have this precipitation
        float artesian_flow_scale   = 0.50f;    // scaling for spring_flow_index
        float spring_attract_weight = 0.25f;    // spring contribution to settlement attractiveness
        float oasis_bonus           = 0.15f;    // extra attractiveness for oasis provinces

        // Port capacity baseline
        float port_coast_norm_km    = 150.0f;   // coastal_length_km normalisation reference
        float port_roughness_penalty = 0.50f;   // terrain_roughness penalty factor
        float port_elev_cap_m       = 2000.0f;  // elevation for max penalty
        float port_elev_max_penalty = 0.50f;    // maximum elevation penalty
        float port_river_mouth_bonus = 0.25f;   // river_access contribution at coast

        // Endorheic basin
        float endorheic_lithium_chance = 0.40f; // probability of lithium brine in endorheic basin
    } hydrology{};

    // -----------------------------------------------------------------------
    // SoilsParams — blending ratios for Stage 5+6 soil and biome pass
    // -----------------------------------------------------------------------
    struct SoilsParams {
        // Per-geology-type soil multipliers are calibrated from real soil science and
        // kept as code constants (see derive_soils_and_biomes). These fields control
        // the blending and bounding of the derived values.

        // Forest coverage blend: final = archetype * forest_archetype_blend
        //                               + climate_expected * forest_climate_blend
        float forest_archetype_blend = 0.60f;
        float forest_climate_blend   = 0.40f;

        // Drought/flood vulnerability blend with archetype-set values.
        float climate_vuln_blend     = 0.50f;  // weight for climate-derived value
        float archetype_vuln_blend   = 0.50f;  // weight for archetype-set value

        // Agricultural productivity bounds after soil/climate adjustment.
        float ag_min = 0.02f;
        float ag_max = 1.00f;
    } soils{};
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

    // Stage 11 — Serialize WorldState to JSON (world.json format).
    // Returns the JSON object; also writes to config.output_world_file if non-empty.
    static nlohmann::json to_world_json(const WorldState& world);

    // Write a WorldState to disk as world.json.
    static void write_world_json(const WorldState& world, const std::string& path);

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

    // Stage 3 — Hydrology (WorldGen v0.18).
    // Computes drainage basins, river networks, snowpack, snowmelt propagation,
    // groundwater, springs, alluvial fans, deltas, endorheic basins, and port
    // capacity baseline. Refines archetype-set river_access with physically
    // derived values. Must run after generate_plates() and create_province_links().
    static void calculate_hydrology(WorldState& world, DeterministicRNG& rng,
                                    const WorldGeneratorConfig& config);

    // Stage 2 derived — Terrain flag detection (WorldGen v0.18).
    // Detects mountain passes (high-terrain chokepoints) and island isolation.
    // Must run after create_province_links() so ProvinceLink vectors are populated.
    static void detect_terrain_flags(WorldState& world, const WorldGeneratorConfig& config);

    // Stage 4a — Province geography refinement (WorldGen v0.18; simplified pass).
    // Applies terrain_roughness → elevation correlation so mountainous provinces have
    // physically consistent altitude. Applies elevation lapse rate to all three
    // temperature fields (6.5 °C / 1 000 m environmental lapse rate).
    // Must run BEFORE detect_terrain_flags() so mountain pass detection uses corrected
    // elevation. No RNG needed — fully deterministic from already-set Province fields.
    static void refine_province_geography(WorldState& world, const WorldGeneratorConfig& config);

    // Stage 4b — Economic geography seeding (WorldGen v0.18; simplified pass).
    // Derives trade_openness from port_capacity + river_access + landlocked/island status,
    // fixing the V1 archetype bug where only coastal_trade sets trade_openness (others 0.0).
    // Refines wildfire_vulnerability from KoppenZone + drought_vulnerability.
    // Adjusts formal_employment_rate for infrastructure richness and corruption.
    // Must run AFTER detect_special_features() so island_isolation and has_permafrost
    // flags are available. No RNG needed.
    static void seed_economic_geography(WorldState& world, const WorldGeneratorConfig& config);

    // Stage 5+6 — Soils and Biomes (WorldGen v0.18; simplified pass).
    // Adjusts agricultural_productivity from geology_type + KoppenZone soil fertility model.
    // Refines forest_coverage, drought_vulnerability, flood_vulnerability from climate zone.
    // Must run after generate_plates() (reads geology_type) and apply_archetype() (reads
    // koppen_zone and initializes agricultural_productivity / forest_coverage baselines).
    static void derive_soils_and_biomes(WorldState& world, DeterministicRNG& rng,
                                        const WorldGeneratorConfig& config);

    // Stage 7 — Special terrain features (WorldGen v0.18; complete pass).
    // Sets has_permafrost (latitude > 66.5 or ET/EF koppen) and has_fjord (coastal high-relief
    // high-latitude). Applies permafrost accessibility lock to CrudeOil / NaturalGas deposits.
    // Must run after derive_soils_and_biomes() and create_province_links().
    static void detect_special_features(WorldState& world, const WorldGeneratorConfig& config);

    // Stage 9 — Population attractiveness (WorldGen v0.18; simplified pass).
    // Re-weights total_population from a settlement attractiveness score derived from soil
    // fertility, infrastructure, climate stress, and hazard factors. Bounded ±40% of archetype
    // baseline so archetypes remain dominant while geology/climate add meaningful variation.
    // Must run after detect_special_features() (reads has_permafrost).
    static void seed_population_attractiveness(WorldState& world, DeterministicRNG& rng,
                                               const WorldGeneratorConfig& config);

    // Stage 10 — World commentary (WorldGen v0.18).
    // Generates province_lore strings from tectonic context, climate, and archetype.
    // Must run after generate_plates() and create_provinces() (reads all province fields).
    static void generate_commentary(WorldState& world, DeterministicRNG& rng);
};

}  // namespace econlife
