// WorldGenerator — V1 world generation implementation.
// See world_generator.h for class documentation.
//
// Produces economically diverse, deterministic worlds from seed + CSV data.
// Province archetypes drive geographic, economic, and demographic variation.
// All random draws go through DeterministicRNG for full reproducibility.

#include "core/world_gen/world_generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/world_gen/h3_utils.h"

namespace econlife {

// ===========================================================================
// Province archetype names (for fictional_name generation)
// ===========================================================================

static const char* archetype_name_prefixes[] = {
    "New ",    // industrial_hub
    "Green ",  // agricultural
    "Iron ",   // resource_rich
    "Port ",   // coastal_trade
    "Crown ",  // financial_center
    "",        // mixed_economy
};

static const char* province_name_suffixes[] = {
    "Haven", "Ridge", "Valley",  "Shore",   "Fields", "Gate", "Brook", "Cliff",  "Marsh",
    "Dale",  "Ford",  "Heights", "Landing", "Point",  "Mill", "Creek", "Bridge", "Hollow",
};

// ===========================================================================
// WorldGenerator::generate — main entry point
// ===========================================================================

WorldState WorldGenerator::generate(const WorldGeneratorConfig& config) {
    DeterministicRNG rng(config.seed);

    WorldState world{};
    world.current_tick = 0;
    world.world_seed = config.seed;
    world.ticks_this_session = 0;
    world.game_mode = GameMode::standard;
    world.current_schema_version = 1;
    world.network_health_dirty = false;
    world.lod2_price_index = nullptr;
    world.player = nullptr;

    // Step 1: Placeholder nation (replaced by form_nations after Stage 9.1).
    // create_nation creates a single nation and regions so early pipeline stages
    // that reference nation_id/region_id have valid data. form_nations() later
    // replaces these with physics-derived multi-nation assignment.
    create_nation(world, config.province_count);

    // Step 2: Provinces with varied geography
    create_provinces(world, rng, config);

    // Step 3: Province links (adjacency graph)
    create_province_links(world);

    // Step 3b: Stage 1 — Tectonic context (WorldGen v0.18).
    // Runs after province geography is set so lat/lng/elevation are available.
    // Assigns tectonic_context, rock_type, geology_type, tectonic_stress, plate_age,
    // and seeds tectonic-context-specific resource deposits.
    generate_plates(world, rng, config);

    // Step 3b2: Stage 4 — Atmosphere (WorldGen v0.18).
    // Derives temperature/precipitation from latitude + altitude + Hadley cells,
    // computes rain shadow, monsoon, ocean currents, ENSO susceptibility,
    // continentality, geographic vulnerability, and re-derives Köppen zones.
    // Runs after generate_plates() and create_province_links().
    simulate_atmosphere(world, rng, config);

    // Step 3b3: Stage 3 — Hydrology (WorldGen v0.18).
    // Computes drainage basins, river networks, snowpack, snowmelt propagation,
    // groundwater, springs, alluvial fans, deltas, endorheic basins, port capacity.
    // Runs after simulate_atmosphere() (reads precipitation for snowpack/groundwater)
    // and generate_plates() (reads geology_type for groundwater).
    calculate_hydrology(world, rng, config);

    // Step 3b4: Stage 4a — Province geography refinement (WorldGen v0.18).
    // Scales elevation from terrain_roughness. Temperature lapse rate is now
    // handled in simulate_atmosphere(), but elevation scaling must run before
    // detect_terrain_flags() so mountain pass detection uses corrected elevation.
    refine_province_geography(world, config);

    // Step 3c: Stage 2 derived — Terrain flag detection (WorldGen v0.18).
    // Detects mountain passes (chokepoints) and island isolation.
    // Runs after refine_province_geography() so corrected elevation is used.
    detect_terrain_flags(world, config);

    // Step 3d: Stage 5+6 — Soils and Biomes (WorldGen v0.18).
    // Adjusts agricultural_productivity from geology_type + climate soil fertility model.
    // Refines forest_coverage, drought/flood vulnerability from KoppenZone.
    // Runs after generate_plates() (geology_type available) and apply_archetype()
    // (KoppenZone and baseline agricultural_productivity set).
    derive_soils_and_biomes(world, rng, config);

    // Step 3e: Stage 7 — Special terrain features (WorldGen v0.18).
    // Sets has_permafrost, has_fjord. Locks CrudeOil/NaturalGas accessibility for
    // permafrost provinces. Applies fjord transit cost to Maritime links.
    // Runs after derive_soils_and_biomes() (permafrost reduces ag_productivity further).
    detect_special_features(world, rng, config);

    // Step 4: Markets from goods catalog
    GoodsCatalog catalog;
    if (!config.goods_directory.empty()) {
        catalog.load_from_directory(config.goods_directory);
    }
    create_markets(world, rng, catalog, config);

    // Step 5: NPC population
    create_npcs(world, rng, config);

    // Step 6: Businesses
    create_businesses(world, rng, config);

    // Step 7: Facilities (assign recipes to businesses)
    RecipeCatalog recipe_catalog;
    FacilityTypeCatalog facility_type_catalog;
    if (!config.recipes_directory.empty()) {
        recipe_catalog.load_from_directory(config.recipes_directory);
    }
    if (!config.facility_types_filepath.empty()) {
        facility_type_catalog.load_from_csv(config.facility_types_filepath);
    }
    if (recipe_catalog.size() > 0 && facility_type_catalog.size() > 0) {
        create_facilities(world, rng, recipe_catalog, facility_type_catalog, config);
    }

    // Store loaded recipes in WorldState for production module access.
    world.loaded_recipes = recipe_catalog.all();

    // Step 3f: Stage 8 — Age-dependent resource modifiers (WorldGen v0.18).
    // Applies radiogenic decay to Uranium, Geothermal; helium accumulation on
    // NaturalGas; cobalt fraction on Copper; peat on Histosol provinces.
    // Runs after soils/biomes (needs soil_type) and after all deposit seeding.
    apply_age_modifiers(world, rng, config);

    // Step 3f2: Stage 8 — Deterministic resource seeding (WorldGen v0.18).
    // Sand, aggregate, solar potential, wind potential — derived from geology,
    // climate, and geography rather than tectonic probability table.
    seed_deterministic_resources(world, rng, config);

    // Step 3g: Stage 4b — Economic geography seeding (WorldGen v0.18).
    // Derives trade_openness for all province archetypes (fixes the bug where only
    // coastal_trade sets it). Refines wildfire_vulnerability from KoppenZone. Adjusts
    // formal_employment_rate for infrastructure and corruption. MUST run after
    // detect_special_features() so island_isolation flag is available.
    seed_economic_geography(world, config);

    // Step 7b: Stage 9 — Population attractiveness (WorldGen v0.18).
    // Adjusts total_population from geology/climate attractiveness score (bounded ±40%).
    // Runs after detect_special_features() so permafrost and island flags are set.
    seed_population_attractiveness(world, rng, config);

    // Step 7b2: Stage 9.5 — Nation formation (WorldGen v0.18).
    // Replaces the single placeholder nation with physics-derived multi-nation assignment.
    // Seeds nation territories via Voronoi growth, language families, border changes.
    // Runs after seed_population_attractiveness() (reads settlement_attractiveness).
    form_nations(world, rng, config);

    // Step 7b3: Stage 9.6 — Nomadic population (WorldGen v0.18).
    // Seeds pastoral_carrying_capacity and nomadic_population_fraction from climate.
    seed_nomadic_population(world, rng, config);

    // Step 7b4: Stage 9.7 — Nation capital seeding (WorldGen v0.18).
    // Selects highest settlement_attractiveness province per nation as capital.
    seed_nation_capitals(world, config);

    // Step 7c: Stage 10 — World commentary pipeline (WorldGen v0.18).
    // commentary_depth controls how much text is generated:
    //   "none"    → skip Stage 10 entirely
    //   "minimal" → sidebar facts + current_character only
    //   "full"    → complete history, features, pre-game events
    if (config.commentary_depth != CommentaryDepth::none) {
        // Stage 10.1 — Named feature detection.
        detect_named_features(world, rng, config);

        if (config.commentary_depth == CommentaryDepth::full) {
            // Stage 10.2 — Province history generation (full only).
            generate_province_histories(world, rng, config);

            // Stage 10.3 — Pre-game event extraction (full only).
            seed_pre_game_events(world, rng, config);
        } else {
            // Minimal: classify archetype and current_character only.
            for (auto& prov : world.provinces) {
                prov.history.province_archetype_label = classify_province_archetype(prov);
                prov.history.current_character =
                    "A " + prov.history.province_archetype_label +
                    " province defined by its " +
                    (prov.infrastructure_rating > 0.60f
                         ? "developed infrastructure"
                         : "natural landscape") +
                    " and " +
                    (prov.agricultural_productivity > 0.50f
                         ? "productive agricultural base."
                         : "extractive or service economy.");
            }
        }

        // Stage 10.4 — Loading commentary (always for non-none).
        generate_loading_commentary(world, rng, config);

        // Stage 10 legacy — Province lore strings.
        generate_commentary(world, rng);
    }

    // Step 8: Technology — load catalog and seed initial holdings.
    TechnologyCatalog tech_catalog;
    if (!config.technology_directory.empty()) {
        std::string nodes_path = config.technology_directory + "/technology_nodes.csv";
        std::string ceilings_path = config.technology_directory + "/maturation_ceilings.csv";
        tech_catalog.load_nodes_csv(nodes_path);
        tech_catalog.load_ceilings_csv(ceilings_path);
    }
    seed_technology(world, rng, tech_catalog, config);

    // Step 9: Stage 11 — Output world.json if path configured.
    if (!config.output_world_file.empty()) {
        write_world_json(world, config.output_world_file);
    }

    // Stage 10.5 — Encyclopedia JSON output (UI layer only).
    if (!config.output_encyclopedia_file.empty() &&
        config.commentary_depth != CommentaryDepth::none) {
        write_encyclopedia_json(world, config, config.output_encyclopedia_file);
    }

    return world;
}

WorldGenerator::WorldWithPlayer WorldGenerator::generate_with_player(
    const WorldGeneratorConfig& config) {
    WorldWithPlayer result;
    result.world = generate(config);

    // Create player in first province.
    result.player = PlayerCharacter{};
    result.player.id = 1;
    result.player.background = Background::MiddleClass;
    result.player.traits = {Trait::Analytical, Trait::Disciplined, Trait::Cautious};
    result.player.starting_province_id = 0;
    result.player.home_province_id = 0;
    result.player.current_province_id = 0;
    result.player.travel_status = static_cast<NPCTravelStatus>(0);
    result.player.health.current_health = 1.0f;
    result.player.health.base_lifespan = 75.0f;
    result.player.health.lifespan_projection = 75.0f;
    result.player.health.exhaustion_accumulator = 0.0f;
    result.player.health.degradation_rate = 0.0001f;
    result.player.age = 30.0f;
    result.player.reputation = {0.0f, 0.0f, 0.0f, 0.0f};
    result.player.wealth = 50000.0f;
    result.player.net_assets = 50000.0f;
    result.player.network_health = {0.5f, 0.3f, 0.2f, 0.1f};
    result.player.movement_follower_count = 0;
    result.player.ironman_eligible = false;
    result.player.residence_id = 0;
    result.player.partner_npc_id = 0;
    result.player.designated_heir_npc_id = 0;
    result.player.restoration_history.restoration_count = 0;

    return result;
}

// ===========================================================================
// Nation creation
// ===========================================================================

void WorldGenerator::create_nation(WorldState& world, uint32_t province_count) {
    Nation nation{};
    nation.id = 0;
    nation.name = "Republic of Avalon";
    nation.currency_code = "AVL";
    nation.government_type = GovernmentType::Democracy;
    nation.political_cycle = {0, 0.55f, false, 365 * 4};  // 4-year election cycle
    nation.corporate_tax_rate = 0.22f;
    nation.income_tax_rate_top_bracket = 0.37f;
    nation.trade_balance_fraction = 0.0f;
    nation.inflation_rate = 0.02f;
    nation.credit_rating = 0.75f;
    nation.tariff_schedule = nullptr;
    nation.lod1_profile = std::nullopt;  // LOD 0 — player's home nation

    for (uint32_t p = 0; p < province_count; ++p) {
        nation.province_ids.push_back(p);
    }

    world.nations.push_back(std::move(nation));

    // One region per province for V1.
    for (uint32_t r = 0; r < province_count; ++r) {
        Region region{};
        region.id = r;
        region.fictional_name = "Region_" + std::to_string(r);
        region.nation_id = 0;
        region.province_ids.push_back(r);
        world.region_groups.push_back(std::move(region));
    }
}

// ===========================================================================
// Province archetype assignment
// ===========================================================================

WorldGenerator::ProvinceArchetype WorldGenerator::assign_archetype(DeterministicRNG& rng,
                                                                   uint32_t province_idx,
                                                                   uint32_t province_count) {
    // Ensure diversity: first 6 provinces get distinct archetypes.
    // Beyond 6, assign randomly.
    if (province_idx < 6) {
        // Shuffle order for variety using seed-dependent permutation.
        // Use a simple Fisher-Yates-like selection.
        static constexpr ProvinceArchetype base_order[] = {
            ProvinceArchetype::industrial_hub,   ProvinceArchetype::agricultural,
            ProvinceArchetype::resource_rich,    ProvinceArchetype::coastal_trade,
            ProvinceArchetype::financial_center, ProvinceArchetype::mixed_economy,
        };
        // For determinism with small province counts, use modular selection
        // influenced by RNG.
        uint32_t offset = rng.next_uint(6);
        return base_order[(province_idx + offset) % 6];
    }
    return static_cast<ProvinceArchetype>(rng.next_uint(6));
}

// ===========================================================================
// Province creation with archetype-driven variation
// ===========================================================================

void WorldGenerator::apply_archetype(Province& province, ProvinceArchetype archetype,
                                     DeterministicRNG& rng, const WorldGeneratorConfig& config) {
    // Base values — varied by archetype.
    float lat_base = 30.0f + rng.next_float() * 30.0f;   // 30-60 degrees
    float lon_base = -20.0f + rng.next_float() * 80.0f;  // -20 to 60

    switch (archetype) {
        case ProvinceArchetype::industrial_hub:
            province.geography.terrain_roughness = 0.2f + rng.next_float() * 0.2f;
            province.geography.arable_land_fraction = 0.2f + rng.next_float() * 0.2f;
            province.geography.forest_coverage = 0.1f + rng.next_float() * 0.2f;
            province.geography.coastal_length_km = rng.next_float() * 50.0f;
            province.geography.is_landlocked = (rng.next_uint(3) == 0);
            province.geography.port_capacity = province.geography.is_landlocked ? 0.0f : 0.3f;
            province.geography.river_access = 0.4f + rng.next_float() * 0.3f;
            province.infrastructure_rating = 0.7f + rng.next_float() * 0.2f;
            province.agricultural_productivity = 0.3f + rng.next_float() * 0.2f;
            province.demographics.total_population = 800000 + rng.next_uint(400000);
            province.demographics.education_level = 0.6f + rng.next_float() * 0.2f;
            province.demographics.income_low_fraction = 0.25f;
            province.demographics.income_middle_fraction = 0.50f;
            province.demographics.income_high_fraction = 0.25f;
            province.climate.koppen_zone = KoppenZone::Cfb;
            province.energy_cost_baseline = 0.04f + rng.next_float() * 0.02f;
            break;

        case ProvinceArchetype::agricultural:
            province.geography.terrain_roughness = 0.1f + rng.next_float() * 0.15f;
            province.geography.arable_land_fraction = 0.6f + rng.next_float() * 0.3f;
            province.geography.forest_coverage = 0.15f + rng.next_float() * 0.15f;
            province.geography.coastal_length_km = rng.next_float() * 30.0f;
            province.geography.is_landlocked = (rng.next_uint(2) == 0);
            province.geography.port_capacity = province.geography.is_landlocked ? 0.0f : 0.2f;
            province.geography.river_access = 0.5f + rng.next_float() * 0.3f;
            province.infrastructure_rating = 0.4f + rng.next_float() * 0.2f;
            province.agricultural_productivity = 0.7f + rng.next_float() * 0.25f;
            province.demographics.total_population = 300000 + rng.next_uint(300000);
            province.demographics.education_level = 0.4f + rng.next_float() * 0.2f;
            province.demographics.income_low_fraction = 0.40f;
            province.demographics.income_middle_fraction = 0.45f;
            province.demographics.income_high_fraction = 0.15f;
            province.climate.koppen_zone = KoppenZone::Cfa;
            province.climate.precipitation_mm = 700.0f + rng.next_float() * 500.0f;
            province.energy_cost_baseline = 0.05f + rng.next_float() * 0.03f;
            lat_base = 25.0f + rng.next_float() * 20.0f;  // warmer
            break;

        case ProvinceArchetype::resource_rich:
            province.geography.terrain_roughness = 0.5f + rng.next_float() * 0.3f;
            province.geography.arable_land_fraction = 0.1f + rng.next_float() * 0.15f;
            province.geography.forest_coverage = 0.3f + rng.next_float() * 0.3f;
            province.geography.coastal_length_km = rng.next_float() * 20.0f;
            province.geography.is_landlocked = (rng.next_uint(3) != 0);  // often landlocked
            province.geography.port_capacity = province.geography.is_landlocked ? 0.0f : 0.1f;
            province.geography.river_access = 0.2f + rng.next_float() * 0.3f;
            province.infrastructure_rating = 0.3f + rng.next_float() * 0.3f;
            province.agricultural_productivity = 0.2f + rng.next_float() * 0.2f;
            province.demographics.total_population = 200000 + rng.next_uint(300000);
            province.demographics.education_level = 0.35f + rng.next_float() * 0.2f;
            province.demographics.income_low_fraction = 0.35f;
            province.demographics.income_middle_fraction = 0.40f;
            province.demographics.income_high_fraction = 0.25f;
            province.climate.koppen_zone = KoppenZone::Dfb;  // continental
            province.energy_cost_baseline = 0.03f + rng.next_float() * 0.02f;
            lat_base = 45.0f + rng.next_float() * 15.0f;  // northern
            break;

        case ProvinceArchetype::coastal_trade:
            province.geography.terrain_roughness = 0.15f + rng.next_float() * 0.2f;
            province.geography.arable_land_fraction = 0.25f + rng.next_float() * 0.2f;
            province.geography.forest_coverage = 0.1f + rng.next_float() * 0.15f;
            province.geography.coastal_length_km = 150.0f + rng.next_float() * 200.0f;
            province.geography.is_landlocked = false;
            province.geography.port_capacity = 0.6f + rng.next_float() * 0.3f;
            province.geography.river_access = 0.3f + rng.next_float() * 0.4f;
            province.infrastructure_rating = 0.6f + rng.next_float() * 0.3f;
            province.agricultural_productivity = 0.3f + rng.next_float() * 0.2f;
            province.demographics.total_population = 600000 + rng.next_uint(500000);
            province.demographics.education_level = 0.55f + rng.next_float() * 0.2f;
            province.demographics.income_low_fraction = 0.30f;
            province.demographics.income_middle_fraction = 0.45f;
            province.demographics.income_high_fraction = 0.25f;
            province.climate.koppen_zone = KoppenZone::Csa;  // Mediterranean
            province.climate.precipitation_mm = 500.0f + rng.next_float() * 300.0f;
            province.energy_cost_baseline = 0.05f + rng.next_float() * 0.03f;
            province.trade_openness = 0.7f + rng.next_float() * 0.2f;
            break;

        case ProvinceArchetype::financial_center:
            province.geography.terrain_roughness = 0.1f + rng.next_float() * 0.15f;
            province.geography.arable_land_fraction = 0.15f + rng.next_float() * 0.15f;
            province.geography.forest_coverage = 0.05f + rng.next_float() * 0.1f;
            province.geography.coastal_length_km = 50.0f + rng.next_float() * 100.0f;
            province.geography.is_landlocked = false;
            province.geography.port_capacity = 0.4f + rng.next_float() * 0.3f;
            province.geography.river_access = 0.3f + rng.next_float() * 0.3f;
            province.infrastructure_rating = 0.8f + rng.next_float() * 0.15f;
            province.agricultural_productivity = 0.2f + rng.next_float() * 0.15f;
            province.demographics.total_population = 1000000 + rng.next_uint(500000);
            province.demographics.education_level = 0.75f + rng.next_float() * 0.15f;
            province.demographics.income_low_fraction = 0.20f;
            province.demographics.income_middle_fraction = 0.40f;
            province.demographics.income_high_fraction = 0.40f;
            province.climate.koppen_zone = KoppenZone::Cfb;
            province.energy_cost_baseline = 0.06f + rng.next_float() * 0.02f;
            break;

        case ProvinceArchetype::mixed_economy:
        default:
            province.geography.terrain_roughness = 0.2f + rng.next_float() * 0.3f;
            province.geography.arable_land_fraction = 0.35f + rng.next_float() * 0.3f;
            province.geography.forest_coverage = 0.2f + rng.next_float() * 0.3f;
            province.geography.coastal_length_km = rng.next_float() * 100.0f;
            province.geography.is_landlocked = (rng.next_uint(3) == 0);
            province.geography.port_capacity =
                province.geography.is_landlocked ? 0.0f : 0.3f + rng.next_float() * 0.3f;
            province.geography.river_access = 0.3f + rng.next_float() * 0.4f;
            province.infrastructure_rating = 0.5f + rng.next_float() * 0.3f;
            province.agricultural_productivity = 0.4f + rng.next_float() * 0.3f;
            province.demographics.total_population = 400000 + rng.next_uint(400000);
            province.demographics.education_level = 0.5f + rng.next_float() * 0.2f;
            province.demographics.income_low_fraction = 0.30f;
            province.demographics.income_middle_fraction = 0.45f;
            province.demographics.income_high_fraction = 0.25f;
            province.climate.koppen_zone = KoppenZone::Cfb;
            province.energy_cost_baseline = 0.05f + rng.next_float() * 0.03f;
            break;
    }

    // Common geography fields.
    province.geography.latitude = lat_base;
    province.geography.longitude = lon_base;
    province.geography.elevation_avg_m = 50.0f + rng.next_float() * 800.0f;
    province.geography.area_km2 = 1500.0f + rng.next_float() * 600.0f;

    // Common climate fields.
    province.climate.temperature_avg_c =
        25.0f - province.geography.latitude * 0.3f + rng.next_float() * 5.0f;
    province.climate.temperature_min_c =
        province.climate.temperature_avg_c - 15.0f - rng.next_float() * 10.0f;
    province.climate.temperature_max_c =
        province.climate.temperature_avg_c + 10.0f + rng.next_float() * 10.0f;
    if (province.climate.precipitation_mm <= 0.0f) {
        province.climate.precipitation_mm = 400.0f + rng.next_float() * 800.0f;
    }
    province.climate.precipitation_seasonality = 0.2f + rng.next_float() * 0.5f;
    province.climate.drought_vulnerability = 0.1f + rng.next_float() * 0.3f;
    province.climate.flood_vulnerability = 0.05f + rng.next_float() * 0.25f;
    province.climate.wildfire_vulnerability = 0.05f + rng.next_float() * 0.2f;
    province.climate.climate_stress_current = 0.05f + rng.next_float() * 0.1f;

    // Demographics common fields.
    province.demographics.median_age = 30.0f + rng.next_float() * 15.0f;
    province.demographics.political_lean = -0.3f + rng.next_float() * 0.6f;

    // Simulation state — initial conditions.
    province.community.cohesion = 0.5f + rng.next_float() * 0.3f;
    province.community.grievance_level = 0.1f + rng.next_float() * 0.15f;
    province.community.institutional_trust = 0.5f + rng.next_float() * 0.3f;
    province.community.resource_access = 0.4f + rng.next_float() * 0.4f;
    province.community.response_stage = 0;

    province.political.governing_office_id = 0;
    province.political.approval_rating = 0.4f + rng.next_float() * 0.3f;
    province.political.election_due_tick = 365 * 4;  // 4 years
    province.political.corruption_index = config.corruption_baseline + rng.next_float() * 0.15f;

    province.conditions.stability_score = 0.6f + rng.next_float() * 0.3f;
    province.conditions.inequality_index = 0.2f + rng.next_float() * 0.3f;
    province.conditions.crime_rate = 0.05f + rng.next_float() * 0.1f;
    province.conditions.addiction_rate = 0.02f + rng.next_float() * 0.05f;
    province.conditions.criminal_dominance_index =
        config.criminal_baseline + rng.next_float() * 0.05f;
    province.conditions.formal_employment_rate = 0.6f + rng.next_float() * 0.25f;
    province.conditions.regulatory_compliance_index = 0.7f + rng.next_float() * 0.2f;
    province.conditions.drought_modifier = 1.0f;
    province.conditions.flood_modifier = 1.0f;

    province.has_karst = (archetype == ProvinceArchetype::resource_rich && rng.next_uint(3) == 0);
    province.historical_trauma_index = rng.next_float() * 0.3f;

    // Apply historical trauma effects on community state.
    float trauma_grievance_floor = province.historical_trauma_index * 0.25f;
    float trauma_trust_ceiling = 1.0f - province.historical_trauma_index * 0.30f;
    province.community.grievance_level =
        std::max(province.community.grievance_level, trauma_grievance_floor);
    province.community.institutional_trust =
        std::min(province.community.institutional_trust, trauma_trust_ceiling);

    // Seed resource deposits based on archetype.
    seed_resource_deposits(province, archetype, rng, config.resource_richness);
}

void WorldGenerator::create_provinces(WorldState& world, DeterministicRNG& rng,
                                      const WorldGeneratorConfig& config) {
    // Derive a deterministic center lat/lng from the world seed.
    // Range: [-60, 60] lat, [-180, 180] lng — avoids polar edge cases.
    double center_lat = (static_cast<double>(rng.next_uint(12000)) / 100.0) - 60.0;
    double center_lng = (static_cast<double>(rng.next_uint(36000)) / 100.0) - 180.0;
    H3Index center_cell = h3_utils::lat_lng_to_cell(center_lat, center_lng, 4);

    // Get a contiguous group of province_count cells via BFS (sorted for determinism).
    auto h3_cells = h3_utils::grid_compact_group(center_cell, config.province_count);

    for (uint32_t p = 0; p < config.province_count; ++p) {
        Province province{};
        province.id = p;
        province.h3_index      = h3_cells[p];
        province.is_pentagon   = h3_utils::is_pentagon(h3_cells[p]);
        province.neighbor_count = province.is_pentagon ? 5 : 6;
        world.h3_province_map[h3_cells[p]] = p;
        province.region_id = p;
        province.nation_id = 0;
        province.lod_level = SimulationLOD::full;
        province.cohort_stats = nullptr;

        ProvinceArchetype archetype = assign_archetype(rng, p, config.province_count);
        province.province_archetype_index = static_cast<uint8_t>(archetype);

        // Generate fictional name.
        uint32_t suffix_idx = rng.next_uint(static_cast<uint32_t>(
            sizeof(province_name_suffixes) / sizeof(province_name_suffixes[0])));
        province.fictional_name =
            std::string(archetype_name_prefixes[static_cast<uint8_t>(archetype)]) +
            province_name_suffixes[suffix_idx];
        province.real_world_reference = "generated";

        apply_archetype(province, archetype, rng, config);

        world.provinces.push_back(std::move(province));
    }
}

// ===========================================================================
// Resource deposit seeding
// ===========================================================================

void WorldGenerator::seed_resource_deposits(Province& province, ProvinceArchetype archetype,
                                            DeterministicRNG& rng, float richness) {
    uint32_t deposit_id_base = province.id * 100;

    auto add_deposit = [&](ResourceType type, float qty_base, float quality_base,
                            uint8_t era = 1) {
        ResourceDeposit d{};
        d.id = deposit_id_base++;
        d.type = type;
        d.quantity = qty_base * richness * (0.7f + rng.next_float() * 0.6f);
        d.quality = std::min(1.0f, quality_base + rng.next_float() * 0.3f);
        d.depth = rng.next_float() * 0.8f;
        d.accessibility = 0.3f + rng.next_float() * 0.5f;
        d.depletion_rate = 0.0001f + rng.next_float() * 0.0002f;
        d.quantity_remaining = d.quantity;
        d.era_unlock = era;
        province.deposits.push_back(d);
    };

    // Every province gets some basic resources.
    add_deposit(ResourceType::LimestoneSilica, 500.0f, 0.5f);

    switch (archetype) {
        case ProvinceArchetype::resource_rich:
            // Heavy geological deposits.
            add_deposit(ResourceType::IronOre, 2000.0f, 0.6f);
            add_deposit(ResourceType::Copper, 800.0f, 0.5f);
            add_deposit(ResourceType::Coal, 3000.0f, 0.7f);
            add_deposit(ResourceType::CrudeOil, 1500.0f, 0.6f);
            add_deposit(ResourceType::NaturalGas, 1000.0f, 0.5f);
            if (rng.next_uint(3) == 0) {
                add_deposit(ResourceType::Bauxite, 600.0f, 0.4f);
            }
            if (rng.next_uint(4) == 0) {
                add_deposit(ResourceType::Lithium, 200.0f, 0.5f);
            }
            break;

        case ProvinceArchetype::agricultural:
            // Biological resources.
            add_deposit(ResourceType::Wheat, 5000.0f, 0.7f);
            add_deposit(ResourceType::Corn, 4000.0f, 0.6f);
            add_deposit(ResourceType::Soybeans, 3000.0f, 0.6f);
            add_deposit(ResourceType::Cotton, 1500.0f, 0.5f);
            add_deposit(ResourceType::Timber, 2000.0f, 0.5f);
            break;

        case ProvinceArchetype::industrial_hub:
            // Moderate resources, focus on processed inputs.
            add_deposit(ResourceType::IronOre, 500.0f, 0.4f);
            add_deposit(ResourceType::Coal, 800.0f, 0.5f);
            add_deposit(ResourceType::Corn, 1000.0f, 0.4f);
            break;

        case ProvinceArchetype::coastal_trade:
            // Fish and moderate agriculture.
            add_deposit(ResourceType::Fish, 3000.0f, 0.7f);
            add_deposit(ResourceType::Wheat, 1500.0f, 0.5f);
            if (rng.next_uint(2) == 0) {
                add_deposit(ResourceType::CrudeOil, 500.0f, 0.4f);  // offshore
            }
            break;

        case ProvinceArchetype::financial_center:
            // Minimal natural resources.
            add_deposit(ResourceType::Fish, 500.0f, 0.4f);
            break;

        case ProvinceArchetype::mixed_economy:
        default:
            // A bit of everything.
            add_deposit(ResourceType::IronOre, 300.0f, 0.4f);
            add_deposit(ResourceType::Wheat, 2000.0f, 0.5f);
            add_deposit(ResourceType::Timber, 1500.0f, 0.5f);
            if (rng.next_uint(3) == 0) {
                add_deposit(ResourceType::Coal, 500.0f, 0.4f);
            }
            break;
    }

    // Wind and solar potential everywhere (varying).
    add_deposit(ResourceType::SolarPotential, 100.0f + rng.next_float() * 400.0f, 0.5f);
    add_deposit(ResourceType::WindPotential, 100.0f + rng.next_float() * 300.0f, 0.5f);
}

// ===========================================================================
// Province links — adjacency graph
// ===========================================================================

// Determine link type from province geography.
static LinkType assign_link_type(const Province& a, const Province& b, float border_km) {
    if (border_km == 0.0f) {
        // No shared land border — maritime if either has coastal access.
        if (!a.geography.is_landlocked || !b.geography.is_landlocked) {
            return LinkType::Maritime;
        }
    }
    if (a.geography.river_access > 0.5f && b.geography.river_access > 0.5f) {
        return LinkType::River;
    }
    return LinkType::Land;
}

void WorldGenerator::create_province_links(WorldState& world) {
    if (world.provinces.size() <= 1)
        return;

    for (uint32_t p = 0; p < static_cast<uint32_t>(world.provinces.size()); ++p) {
        auto& prov = world.provinces[p];
        prov.links.clear();

        auto neighbors = h3_utils::grid_neighbors(prov.h3_index);
        for (H3Index neighbor_cell : neighbors) {
            auto it = world.h3_province_map.find(neighbor_cell);
            if (it == world.h3_province_map.end()) continue;

            uint32_t neighbor_idx = it->second;
            const auto& neighbor_prov = world.provinces[neighbor_idx];

            float border_km = h3_utils::shared_border_km(prov.h3_index, neighbor_cell);

            ProvinceLink link{};
            link.neighbor_h3           = neighbor_cell;
            link.type                  = assign_link_type(prov, neighbor_prov, border_km);
            link.shared_border_km      = border_km;
            link.transit_terrain_cost  = 0.2f + prov.geography.terrain_roughness * 0.3f;
            link.infrastructure_bonus  =
                std::min(prov.infrastructure_rating, neighbor_prov.infrastructure_rating);
            prov.links.push_back(link);
        }
    }
}

// ===========================================================================
// Market creation from goods catalog
// ===========================================================================

void WorldGenerator::create_markets(WorldState& world, DeterministicRNG& rng,
                                    const GoodsCatalog& catalog,
                                    const WorldGeneratorConfig& config) {
    auto available_goods = catalog.goods_available_at(config.starting_era, config.max_good_tier);

    if (available_goods.empty()) {
        // Fallback: create synthetic goods if no CSV data loaded.
        // This ensures the generator always produces a functional world.
        static const struct {
            const char* name;
            float price;
        } fallback_goods[] = {
            {"food", 25.0f},           {"raw_materials", 15.0f},
            {"consumer_goods", 50.0f}, {"industrial_goods", 80.0f},
            {"energy", 40.0f},         {"services", 60.0f},
            {"luxury_goods", 120.0f},  {"construction", 45.0f},
            {"agriculture", 20.0f},    {"technology", 100.0f},
        };

        for (uint32_t p = 0; p < static_cast<uint32_t>(world.provinces.size()); ++p) {
            for (uint32_t g = 0; g < 10; ++g) {
                RegionalMarket m{};
                m.good_id = g;
                m.province_id = p;
                m.spot_price = fallback_goods[g].price * (0.8f + rng.next_float() * 0.4f);
                m.equilibrium_price = fallback_goods[g].price;
                m.adjustment_rate = 0.05f + rng.next_float() * 0.05f;
                m.supply = 80.0f + rng.next_float() * 120.0f;
                m.demand_buffer = 0.0f;
                m.import_price_ceiling = 0.0f;
                m.export_price_floor = 0.0f;
                world.regional_markets.push_back(m);
                world.provinces[p].market_ids.push_back(
                    static_cast<uint32_t>(world.regional_markets.size()) - 1);
            }
        }
        return;
    }

    // Create one market per (good, province) for all available goods.
    for (uint32_t p = 0; p < static_cast<uint32_t>(world.provinces.size()); ++p) {
        const auto& province = world.provinces[p];

        for (const auto* good : available_goods) {
            RegionalMarket m{};
            m.good_id = good->numeric_id;
            m.province_id = p;

            // Base price from CSV, with local variation.
            float local_variation = 0.85f + rng.next_float() * 0.30f;  // 0.85-1.15
            m.equilibrium_price = good->base_price * local_variation;

            // Supply influenced by province resources and archetype.
            float supply_base = 50.0f;
            // Higher supply for goods matching province resources.
            if (good->category == "geological" && !province.deposits.empty()) {
                supply_base *= 1.0f + province.deposits.size() * 0.2f;
            } else if (good->category == "biological" &&
                       province.agricultural_productivity > 0.5f) {
                supply_base *= 1.0f + province.agricultural_productivity;
            } else if (good->category == "metals" && province.infrastructure_rating > 0.6f) {
                supply_base *= 1.2f;
            }
            m.supply = supply_base * (0.5f + rng.next_float());

            // Spot price starts near equilibrium with slight jitter.
            m.spot_price = m.equilibrium_price * (0.95f + rng.next_float() * 0.1f);
            m.adjustment_rate = 0.05f + rng.next_float() * 0.05f;
            m.demand_buffer = 0.0f;
            m.import_price_ceiling = 0.0f;
            m.export_price_floor = 0.0f;

            world.regional_markets.push_back(m);
            world.provinces[p].market_ids.push_back(
                static_cast<uint32_t>(world.regional_markets.size()) - 1);
        }
    }
}

// ===========================================================================
// NPC population seeding
// ===========================================================================

void WorldGenerator::create_npcs(WorldState& world, DeterministicRNG& rng,
                                 const WorldGeneratorConfig& config) {
    // Role distribution weights — determines the workforce composition.
    // Workers are the majority; specialists are rarer.
    struct RoleWeight {
        NPCRole role;
        uint32_t weight;  // relative frequency
    };
    static constexpr RoleWeight role_distribution[] = {
        {NPCRole::worker, 40},
        {NPCRole::corporate_executive, 5},
        {NPCRole::middle_manager, 8},
        {NPCRole::banker, 3},
        {NPCRole::lawyer, 3},
        {NPCRole::accountant, 3},
        {NPCRole::journalist, 2},
        {NPCRole::politician, 3},
        {NPCRole::community_leader, 3},
        {NPCRole::law_enforcement, 5},
        {NPCRole::regulator, 2},
        {NPCRole::prosecutor, 1},
        {NPCRole::judge, 1},
        {NPCRole::criminal_operator, 4},
        {NPCRole::criminal_enforcer, 3},
        {NPCRole::fixer, 2},
        {NPCRole::union_organizer, 1},
        {NPCRole::family_member, 8},
        {NPCRole::media_editor, 1},
        {NPCRole::ngo_investigator, 1},
    };
    static constexpr uint32_t total_weight =
        40 + 5 + 8 + 3 + 3 + 3 + 2 + 3 + 3 + 5 + 2 + 1 + 1 + 4 + 3 + 2 + 1 + 8 + 1 + 1;

    // Distribute NPCs across provinces weighted by population.
    uint64_t total_pop = 0;
    for (const auto& p : world.provinces) {
        total_pop += p.demographics.total_population;
    }
    if (total_pop == 0)
        total_pop = 1;

    uint32_t npc_id_counter = 100;  // start NPC IDs at 100

    for (uint32_t p = 0; p < static_cast<uint32_t>(world.provinces.size()); ++p) {
        // NPCs per province proportional to population.
        float pop_fraction = static_cast<float>(world.provinces[p].demographics.total_population) /
                             static_cast<float>(total_pop);
        uint32_t npcs_for_province =
            std::max(1u, static_cast<uint32_t>(config.npc_count * pop_fraction + 0.5f));

        for (uint32_t i = 0; i < npcs_for_province; ++i) {
            // Select role via weighted random.
            uint32_t roll = rng.next_uint(total_weight);
            NPCRole role = NPCRole::worker;  // default
            uint32_t cumulative = 0;
            for (const auto& rw : role_distribution) {
                cumulative += rw.weight;
                if (roll < cumulative) {
                    role = rw.role;
                    break;
                }
            }

            NPC npc{};
            npc.id = npc_id_counter++;
            npc.role = role;
            npc.home_province_id = p;
            npc.current_province_id = p;
            npc.travel_status = static_cast<NPCTravelStatus>(0);  // resident
            npc.status = NPCStatus::active;

            // Capital varies by role.
            float capital_base;
            switch (role) {
                case NPCRole::corporate_executive:
                case NPCRole::banker:
                    capital_base = 50000.0f + static_cast<float>(rng.next_uint(100000));
                    break;
                case NPCRole::lawyer:
                case NPCRole::politician:
                case NPCRole::judge:
                case NPCRole::media_editor:
                    capital_base = 30000.0f + static_cast<float>(rng.next_uint(50000));
                    break;
                case NPCRole::middle_manager:
                case NPCRole::accountant:
                    capital_base = 20000.0f + static_cast<float>(rng.next_uint(30000));
                    break;
                case NPCRole::criminal_operator:
                    capital_base = 15000.0f + static_cast<float>(rng.next_uint(80000));
                    break;
                case NPCRole::fixer:
                    capital_base = 10000.0f + static_cast<float>(rng.next_uint(60000));
                    break;
                default:
                    capital_base = 5000.0f + static_cast<float>(rng.next_uint(25000));
                    break;
            }
            npc.capital = capital_base;

            // Risk tolerance varies by role.
            switch (role) {
                case NPCRole::criminal_operator:
                case NPCRole::criminal_enforcer:
                case NPCRole::fixer:
                    npc.risk_tolerance = 0.5f + rng.next_float() * 0.4f;
                    break;
                case NPCRole::law_enforcement:
                case NPCRole::judge:
                    npc.risk_tolerance = 0.2f + rng.next_float() * 0.3f;
                    break;
                default:
                    npc.risk_tolerance = 0.2f + rng.next_float() * 0.6f;
                    break;
            }

            // Social capital varies by role.
            switch (role) {
                case NPCRole::community_leader:
                case NPCRole::politician:
                    npc.social_capital = 0.5f + rng.next_float() * 0.4f;
                    break;
                case NPCRole::journalist:
                case NPCRole::media_editor:
                    npc.social_capital = 0.3f + rng.next_float() * 0.4f;
                    break;
                default:
                    npc.social_capital = 0.1f + rng.next_float() * 0.4f;
                    break;
            }

            npc.movement_follower_count = 0;

            // Motivation vector — 8 weights summing to 1.0.
            // Order: financial_security, social_standing, personal_safety, power_influence,
            //        ideology, loyalty, self_preservation, risk_seeking
            float weights[8];
            float weight_sum = 0.0f;
            for (int w = 0; w < 8; ++w) {
                weights[w] = 0.05f + rng.next_float() * 0.3f;
                weight_sum += weights[w];
            }
            // Bias based on role.
            switch (role) {
                case NPCRole::corporate_executive:
                case NPCRole::banker:
                    weights[0] *= 2.0f;        // financial_security
                    weight_sum += weights[0];  // adjust
                    break;
                case NPCRole::politician:
                case NPCRole::community_leader:
                    weights[3] *= 2.0f;  // power_influence
                    weight_sum += weights[3];
                    break;
                case NPCRole::criminal_operator:
                case NPCRole::criminal_enforcer:
                    weights[0] *= 1.5f;  // financial
                    weights[7] *= 1.5f;  // risk_seeking
                    weight_sum += weights[0] * 0.5f + weights[7] * 0.5f;
                    break;
                case NPCRole::law_enforcement:
                case NPCRole::prosecutor:
                    weights[4] *= 1.5f;  // ideology
                    weights[6] *= 1.5f;  // self_preservation
                    weight_sum += weights[4] * 0.5f + weights[6] * 0.5f;
                    break;
                default:
                    break;
            }
            // Recalculate sum and normalize to 1.0.
            weight_sum = 0.0f;
            for (int w = 0; w < 8; ++w)
                weight_sum += weights[w];
            for (int w = 0; w < 8; ++w)
                weights[w] /= weight_sum;
            npc.motivations.weights = {weights[0], weights[1], weights[2], weights[3],
                                       weights[4], weights[5], weights[6], weights[7]};

            world.provinces[p].significant_npc_ids.push_back(npc.id);
            world.significant_npcs.push_back(std::move(npc));
        }
    }
}

// ===========================================================================
// Business seeding
// ===========================================================================

void WorldGenerator::create_businesses(WorldState& world, DeterministicRNG& rng,
                                       const WorldGeneratorConfig& config) {
    // Business count: roughly 1 per 10 NPCs, minimum 1 per province.
    uint32_t total_businesses = std::max(static_cast<uint32_t>(world.provinces.size()),
                                         static_cast<uint32_t>(world.significant_npcs.size()) / 10);

    // Sector distribution per province — influenced by archetype.
    // Collect executive/corporate NPCs as potential owners.
    struct OwnerCandidate {
        uint32_t npc_id;
        uint32_t province_id;
    };
    std::vector<OwnerCandidate> owner_pool;
    for (const auto& npc : world.significant_npcs) {
        if (npc.role == NPCRole::corporate_executive || npc.role == NPCRole::middle_manager ||
            npc.role == NPCRole::criminal_operator || npc.role == NPCRole::fixer) {
            owner_pool.push_back({npc.id, npc.home_province_id});
        }
    }

    // If not enough owners, also include bankers and lawyers.
    if (owner_pool.size() < total_businesses) {
        for (const auto& npc : world.significant_npcs) {
            if (npc.role == NPCRole::banker || npc.role == NPCRole::lawyer) {
                owner_pool.push_back({npc.id, npc.home_province_id});
            }
        }
    }

    // Fallback: any NPC can own a business.
    if (owner_pool.empty()) {
        for (const auto& npc : world.significant_npcs) {
            owner_pool.push_back({npc.id, npc.home_province_id});
        }
    }

    // Sector options for formal businesses.
    static constexpr BusinessSector formal_sectors[] = {
        BusinessSector::manufacturing, BusinessSector::food_beverage,
        BusinessSector::retail,        BusinessSector::services,
        BusinessSector::agriculture,   BusinessSector::technology,
        BusinessSector::real_estate,   BusinessSector::energy,
        BusinessSector::finance,       BusinessSector::transport_logistics,
        BusinessSector::media,         BusinessSector::research,
    };
    static constexpr uint32_t formal_sector_count =
        sizeof(formal_sectors) / sizeof(formal_sectors[0]);

    // Business profiles.
    static constexpr BusinessProfile profiles[] = {
        BusinessProfile::cost_cutter,
        BusinessProfile::quality_player,
        BusinessProfile::fast_expander,
        BusinessProfile::defensive_incumbent,
    };

    uint32_t biz_id_counter = 1000;

    for (uint32_t b = 0; b < total_businesses; ++b) {
        uint32_t owner_idx = rng.next_uint(static_cast<uint32_t>(owner_pool.size()));
        const auto& owner = owner_pool[owner_idx];

        bool is_criminal = false;
        // Criminal businesses: ~10% of total, owned by criminal operators.
        for (const auto& npc : world.significant_npcs) {
            if (npc.id == owner.npc_id &&
                (npc.role == NPCRole::criminal_operator || npc.role == NPCRole::fixer)) {
                is_criminal = (rng.next_uint(3) == 0);  // 33% chance for criminal NPCs
                break;
            }
        }

        NPCBusiness biz{};
        biz.id = biz_id_counter++;
        biz.owner_id = owner.npc_id;
        biz.province_id = owner.province_id;
        biz.criminal_sector = is_criminal;

        if (is_criminal) {
            biz.sector = BusinessSector::criminal;
        } else {
            biz.sector = formal_sectors[rng.next_uint(formal_sector_count)];
        }

        biz.profile = profiles[rng.next_uint(4)];

        // Cash and revenue based on province demographics.
        float province_wealth = 1.0f;
        if (owner.province_id < world.provinces.size()) {
            province_wealth =
                world.provinces[owner.province_id].demographics.income_high_fraction * 2.0f +
                world.provinces[owner.province_id].demographics.income_middle_fraction;
        }
        biz.cash = (20000.0f + static_cast<float>(rng.next_uint(200000))) * province_wealth;
        biz.revenue_per_tick = (200.0f + static_cast<float>(rng.next_uint(800))) * province_wealth;
        biz.cost_per_tick =
            biz.revenue_per_tick * (0.6f + rng.next_float() * 0.3f);  // 60-90% cost ratio

        biz.market_share = 0.02f + rng.next_float() * 0.15f;
        biz.strategic_decision_tick = rng.next_uint(90);  // stagger quarterly decisions
        biz.dispatch_day_offset = static_cast<uint8_t>(biz.id % 30);
        biz.actor_tech_state.effective_tech_tier = 1.0f;
        biz.regulatory_violation_severity = is_criminal ? 0.3f + rng.next_float() * 0.5f : 0.0f;
        biz.default_activity_scope =
            is_criminal ? VisibilityScope::concealed : VisibilityScope::institutional;
        biz.deferred_salary_liability = 0.0f;
        biz.accounts_payable_float = 0.0f;

        world.npc_businesses.push_back(std::move(biz));
    }
}

// ===========================================================================
// Facility assignment — gives each business 1-3 facilities with recipes
// ===========================================================================

// Maps business sector to preferred facility type categories.
static std::vector<std::string> facility_categories_for_sector(BusinessSector sector) {
    switch (sector) {
        case BusinessSector::manufacturing:
            return {"manufacturing", "processing"};
        case BusinessSector::food_beverage:
            return {"manufacturing", "agriculture"};
        case BusinessSector::agriculture:
            return {"agriculture"};
        case BusinessSector::energy:
            return {"extraction", "processing"};
        case BusinessSector::technology:
            return {"manufacturing"};
        case BusinessSector::research:
            return {"manufacturing"};
        case BusinessSector::transport_logistics:
            return {"processing", "manufacturing"};
        case BusinessSector::criminal:
            return {"processing", "manufacturing", "agriculture"};
        default:
            return {"manufacturing", "processing"};
    }
}

void WorldGenerator::create_facilities(WorldState& world, DeterministicRNG& rng,
                                       const RecipeCatalog& recipes,
                                       const FacilityTypeCatalog& facility_types,
                                       const WorldGeneratorConfig& config) {
    uint32_t facility_id_counter = 5000;

    for (auto& biz : world.npc_businesses) {
        // Determine how many facilities this business gets (1-3).
        uint32_t facility_count = 1 + rng.next_uint(3);

        // Get preferred categories for this business sector.
        auto preferred_cats = facility_categories_for_sector(biz.sector);

        // Collect candidate facility types from preferred categories.
        std::vector<const FacilityType*> candidate_types;
        for (const auto& cat : preferred_cats) {
            auto cat_types = facility_types.by_category(cat);
            for (const auto* ft : cat_types) {
                candidate_types.push_back(ft);
            }
        }

        // Fallback: all facility types if no match.
        if (candidate_types.empty()) {
            for (const auto& ft : facility_types.all()) {
                candidate_types.push_back(&ft);
            }
        }
        if (candidate_types.empty())
            continue;

        for (uint32_t f = 0; f < facility_count; ++f) {
            // Pick a facility type.
            uint32_t ft_idx = rng.next_uint(static_cast<uint32_t>(candidate_types.size()));
            const FacilityType* ft = candidate_types[ft_idx];

            // Pick a recipe that runs on this facility type.
            auto ft_recipes = recipes.recipes_for_facility_type(ft->key);

            // Filter by era.
            std::vector<const Recipe*> available;
            for (const auto* r : ft_recipes) {
                if (r->era_available <= config.starting_era) {
                    available.push_back(r);
                }
            }
            if (available.empty())
                continue;

            uint32_t r_idx = rng.next_uint(static_cast<uint32_t>(available.size()));
            const Recipe* recipe = available[r_idx];

            Facility facility{};
            facility.id = facility_id_counter++;
            facility.business_id = biz.id;
            facility.province_id = biz.province_id;
            facility.recipe_id = recipe->id;
            facility.tech_tier = recipe->min_tech_tier;
            facility.output_rate_modifier = 1.0f;
            facility.soil_health =
                (ft->category == "agriculture") ? 0.8f + rng.next_float() * 0.2f : 1.0f;
            facility.worker_count = std::min(
                ft->max_workers,
                static_cast<uint32_t>(3 + rng.next_uint(std::max(1u, ft->max_workers / 4))));
            facility.is_operational = true;

            world.facilities.push_back(std::move(facility));
        }
    }
}

// ===========================================================================
// Technology seeding — assign baseline tech holdings to businesses
// ===========================================================================

void WorldGenerator::seed_technology(WorldState& world, DeterministicRNG& rng,
                                     const TechnologyCatalog& tech_catalog,
                                     const WorldGeneratorConfig& config) {
    // Initialize GlobalTechnologyState.
    world.technology.current_era = SimulationEra::era_1_turn_of_millennium;
    world.technology.era_started_tick = 0;
    world.technology.base_year = 2000;

    // Seed initial domain knowledge from config defaults.
    TechnologyConfig tech_config;
    for (uint8_t i = 0; i < RESEARCH_DOMAIN_COUNT; ++i) {
        world.technology.domain_knowledge[i] = tech_config.era1_domain_knowledge[i];
    }

    // Get baseline technology nodes (available at game start).
    auto baseline = tech_catalog.baseline_nodes();
    if (baseline.empty()) return;

    // Sector-to-relevant-domain mapping for seeding.
    // Businesses get baseline tech holdings in their relevant domains.
    auto domains_for_sector = [](BusinessSector sector) -> std::vector<std::string> {
        switch (sector) {
            case BusinessSector::manufacturing:
                return {"mechanical_engineering", "materials_science"};
            case BusinessSector::food_beverage:
                return {"biotechnology", "chemical_synthesis"};
            case BusinessSector::agriculture:
                return {"biotechnology"};
            case BusinessSector::energy:
                return {"energy_systems"};
            case BusinessSector::technology:
                return {"semiconductor_physics", "software_systems"};
            case BusinessSector::research:
                return {"semiconductor_physics", "software_systems", "biotechnology"};
            case BusinessSector::transport_logistics:
                return {"mechanical_engineering"};
            case BusinessSector::criminal:
                return {"illicit_chemistry", "chemical_synthesis"};
            default:
                return {"materials_science"};
        }
    };

    for (auto& biz : world.npc_businesses) {
        auto relevant_domains = domains_for_sector(biz.sector);

        // Give each business holdings in baseline nodes matching their sector domains.
        for (const TechnologyNode* node : baseline) {
            bool relevant = false;
            for (const auto& domain : relevant_domains) {
                if (node->domain == domain) {
                    relevant = true;
                    break;
                }
            }
            if (!relevant) continue;

            TechHolding holding;
            holding.node_key = node->node_key;
            holding.holder_id = biz.id;
            holding.stage = TechStage::commercialized;  // baseline tech is fully commercialized
            holding.researched_tick = 0;
            holding.commercialized_tick = 0;
            holding.has_patent = false;
            holding.internal_use_only = false;

            // Maturation: baseline tech is mature (0.7-0.95 range, randomized).
            float ceiling = tech_catalog.ceiling_for(node->node_key, config.starting_era);
            if (ceiling < 0.0f) ceiling = 1.0f;  // fallback if no ceiling data
            holding.maturation_ceiling = ceiling;
            holding.maturation_level =
                std::min(ceiling, 0.7f + rng.next_float() * 0.25f);

            biz.actor_tech_state.holdings[node->node_key] = std::move(holding);
        }

        // Also give a few non-baseline Era 1 techs to a fraction of businesses
        // (simulating early R&D investment that existed before year 2000).
        auto era1_nodes = tech_catalog.nodes_available_at(1);
        for (const TechnologyNode* node : era1_nodes) {
            if (node->is_baseline) continue;

            // Check domain relevance.
            bool relevant = false;
            for (const auto& domain : relevant_domains) {
                if (node->domain == domain) {
                    relevant = true;
                    break;
                }
            }
            if (!relevant) continue;

            // ~20% chance of having started early research.
            if (rng.next_float() > 0.20f) continue;

            TechHolding holding;
            holding.node_key = node->node_key;
            holding.holder_id = biz.id;
            holding.stage = TechStage::researched;  // early research, not yet commercialized
            holding.researched_tick = 0;
            holding.commercialized_tick = 0;
            holding.has_patent = (node->patentable && rng.next_float() < 0.3f);
            holding.internal_use_only = false;

            float ceiling = tech_catalog.ceiling_for(node->node_key, config.starting_era);
            if (ceiling < 0.0f) ceiling = 0.3f;  // low ceiling for early-era tech
            holding.maturation_ceiling = ceiling;
            // Early research: low maturation (0.05-0.25).
            holding.maturation_level =
                std::min(ceiling, 0.05f + rng.next_float() * 0.20f);

            biz.actor_tech_state.holdings[node->node_key] = std::move(holding);
        }
    }
}

// ===========================================================================
// Stage 1 — Tectonic plate generation and context classification
// ===========================================================================

void WorldGenerator::generate_plates(WorldState& world, DeterministicRNG& rng,
                                     const WorldGeneratorConfig& config) {
    if (world.provinces.empty()) return;

    // Plate count: 3 for V1 (6 provinces → ~2 per plate); scale for larger worlds.
    const uint32_t plate_count =
        std::max(2u, std::min(5u, static_cast<uint32_t>(world.provinces.size()) / 2u));

    struct TectonicPlate {
        float center_lat;
        float center_lng;
        bool is_oceanic;
        float age;  // 1.0 (young crust) to 4.5 (ancient shield)
    };

    // Find province lat/lng extent to spread plates across the world region.
    float min_lat = 90.0f, max_lat = -90.0f;
    float min_lng = 180.0f, max_lng = -180.0f;
    for (const auto& p : world.provinces) {
        min_lat = std::min(min_lat, p.geography.latitude);
        max_lat = std::max(max_lat, p.geography.latitude);
        min_lng = std::min(min_lng, p.geography.longitude);
        max_lng = std::max(max_lng, p.geography.longitude);
    }
    float lat_span = std::max(max_lat - min_lat, 8.0f);
    float lng_span = std::max(max_lng - min_lng, 8.0f);

    std::vector<TectonicPlate> plates;
    plates.reserve(plate_count);
    for (uint32_t i = 0; i < plate_count; ++i) {
        TectonicPlate pl;
        // Distribute centers across the lat range with random lng placement.
        float lat_fraction = (static_cast<float>(i) + 0.5f) / static_cast<float>(plate_count);
        pl.center_lat = min_lat + lat_span * lat_fraction +
                        (rng.next_float() - 0.5f) * lat_span * 0.25f;
        pl.center_lng = min_lng + lng_span * rng.next_float();
        pl.is_oceanic = (rng.next_uint(4) == 0);  // 25% oceanic; most land provinces are continental
        pl.age = 1.0f + rng.next_float() * 3.5f;  // 1.0–4.5 Gyr
        plates.push_back(pl);
    }

    // Assign each province to its nearest plate (Voronoi).
    std::vector<uint32_t> province_plate(world.provinces.size(), 0);
    for (uint32_t p = 0; p < static_cast<uint32_t>(world.provinces.size()); ++p) {
        float best_dist_sq = std::numeric_limits<float>::max();
        for (uint32_t pl = 0; pl < plate_count; ++pl) {
            float dlat = world.provinces[p].geography.latitude - plates[pl].center_lat;
            float dlng = world.provinces[p].geography.longitude - plates[pl].center_lng;
            float dist_sq = dlat * dlat + dlng * dlng;
            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                province_plate[p] = pl;
            }
        }
    }

    // Classify tectonic context for each province.
    for (uint32_t p = 0; p < static_cast<uint32_t>(world.provinces.size()); ++p) {
        auto& prov = world.provinces[p];
        uint32_t my_plate = province_plate[p];
        bool assigned = false;

        for (const auto& link : prov.links) {
            auto it = world.h3_province_map.find(link.neighbor_h3);
            if (it == world.h3_province_map.end()) continue;
            uint32_t nb_idx = it->second;
            if (province_plate[nb_idx] == my_plate) continue;

            // This province is on a plate boundary.
            bool i_am_oceanic = plates[my_plate].is_oceanic;
            bool nb_is_oceanic = plates[province_plate[nb_idx]].is_oceanic;

            if (i_am_oceanic && !nb_is_oceanic) {
                // I'm the subducting oceanic plate.
                prov.tectonic_context = TectonicContext::Subduction;
            } else if (!i_am_oceanic && nb_is_oceanic) {
                // I'm the overriding continental plate above the subduction zone.
                prov.tectonic_context = TectonicContext::Subduction;
            } else if (!i_am_oceanic && !nb_is_oceanic) {
                // Continental-continental boundary.
                float age_diff =
                    std::abs(plates[my_plate].age - plates[province_plate[nb_idx]].age);
                if (age_diff > 1.5f) {
                    // Very different ages → collision (one plate much older)
                    prov.tectonic_context = TectonicContext::ContinentalCollision;
                } else if (rng.next_uint(4) == 0) {
                    prov.tectonic_context = TectonicContext::TransformFault;
                } else {
                    prov.tectonic_context = TectonicContext::ContinentalCollision;
                }
            } else {
                // Oceanic-oceanic boundary.
                prov.tectonic_context = rng.next_uint(3) == 0 ? TectonicContext::HotSpot
                                                               : TectonicContext::TransformFault;
            }
            assigned = true;
            break;  // First cross-plate neighbor determines context.
        }

        if (!assigned) {
            // Interior province (no cross-plate neighbor within adjacency graph).
            if (plates[my_plate].is_oceanic) {
                prov.tectonic_context = TectonicContext::PassiveMargin;
            } else if (plates[my_plate].age > 3.0f) {
                // Ancient stable continental interior.
                prov.tectonic_context = rng.next_uint(4) == 0 ? TectonicContext::SedimentaryBasin
                                                               : TectonicContext::CratonInterior;
            } else {
                // Younger continental interior — often sedimentary basin.
                prov.tectonic_context = rng.next_uint(3) == 0 ? TectonicContext::CratonInterior
                                                               : TectonicContext::SedimentaryBasin;
            }
        }

        // Rift zones are rare: override some TransformFault or SedimentaryBasin provinces.
        if (!assigned && rng.next_uint(6) == 0) {
            prov.tectonic_context = TectonicContext::RiftZone;
        } else if (assigned && prov.tectonic_context == TectonicContext::TransformFault &&
                   rng.next_uint(5) == 0) {
            prov.tectonic_context = TectonicContext::RiftZone;
        }

        // Derive rock_type.
        switch (prov.tectonic_context) {
            case TectonicContext::Subduction:
            case TectonicContext::HotSpot:
                prov.rock_type = RockType::Igneous;
                break;
            case TectonicContext::ContinentalCollision:
                prov.rock_type = RockType::Metamorphic;
                break;
            case TectonicContext::SedimentaryBasin:
            case TectonicContext::PassiveMargin:
                prov.rock_type = RockType::Sedimentary;
                break;
            default:
                prov.rock_type = RockType::Mixed;
                break;
        }

        // Derive geology_type.
        switch (prov.tectonic_context) {
            case TectonicContext::Subduction:
                prov.geology_type = GeologyType::VolcanicArc;
                break;
            case TectonicContext::ContinentalCollision:
                prov.geology_type = rng.next_uint(2) == 0 ? GeologyType::MetamorphicCore
                                                           : GeologyType::CarbonateSequence;
                break;
            case TectonicContext::RiftZone:
            case TectonicContext::HotSpot:
                prov.geology_type = GeologyType::BasalticPlateau;
                break;
            case TectonicContext::TransformFault:
                prov.geology_type = GeologyType::AlluvialFill;
                break;
            case TectonicContext::PassiveMargin:
            case TectonicContext::SedimentaryBasin:
                prov.geology_type = rng.next_uint(3) == 0 ? GeologyType::CarbonateSequence
                                                           : GeologyType::SedimentarySequence;
                break;
            case TectonicContext::CratonInterior:
                prov.geology_type = rng.next_uint(3) == 0 ? GeologyType::GreenstoneBelt
                                                           : GeologyType::GraniteShield;
                break;
        }

        // tectonic_stress: active boundaries are high; stable interiors are low.
        switch (prov.tectonic_context) {
            case TectonicContext::Subduction:
                prov.tectonic_stress = 0.5f + rng.next_float() * 0.4f;
                break;
            case TectonicContext::ContinentalCollision:
            case TectonicContext::RiftZone:
                prov.tectonic_stress = 0.4f + rng.next_float() * 0.4f;
                break;
            case TectonicContext::TransformFault:
                prov.tectonic_stress = 0.3f + rng.next_float() * 0.4f;
                break;
            case TectonicContext::HotSpot:
                prov.tectonic_stress = 0.35f + rng.next_float() * 0.3f;
                break;
            case TectonicContext::PassiveMargin:
                prov.tectonic_stress = 0.05f + rng.next_float() * 0.10f;
                break;
            case TectonicContext::CratonInterior:
                prov.tectonic_stress = 0.02f + rng.next_float() * 0.08f;
                break;
            case TectonicContext::SedimentaryBasin:
                prov.tectonic_stress = 0.04f + rng.next_float() * 0.12f;
                break;
        }

        prov.plate_age = plates[my_plate].age;

        // Karst is primarily a CarbonateSequence / SedimentarySequence feature.
        // Override the archetype-based karst assignment with geological logic.
        if (prov.geology_type == GeologyType::CarbonateSequence) {
            prov.has_karst = (rng.next_uint(3) != 0);  // 67%
        } else if (prov.geology_type == GeologyType::SedimentarySequence) {
            prov.has_karst = (rng.next_uint(5) == 0);  // 20%
        } else if (prov.geology_type == GeologyType::MetamorphicCore) {
            prov.has_karst = false;  // metamorphic rock does not form karst
        }
        // Other geology types: keep value from archetype seeding.
    }

    // Seed tectonic-context deposits on top of archetype deposits.
    for (auto& prov : world.provinces) {
        seed_tectonic_deposits(prov, rng, config.resource_richness);
    }
}

// ---------------------------------------------------------------------------
// seed_tectonic_deposits — adds geology-driven deposits (called from generate_plates)
// ---------------------------------------------------------------------------

void WorldGenerator::seed_tectonic_deposits(Province& province, DeterministicRNG& rng,
                                             float richness) {
    uint32_t deposit_id_base = province.id * 100 + 50;  // offset to avoid collision with archetype IDs

    auto add = [&](ResourceType type, float qty_base, float quality_base,
                    uint8_t era = 1) {
        ResourceDeposit d{};
        d.id = deposit_id_base++;
        d.type = type;
        d.quantity = qty_base * richness * (0.7f + rng.next_float() * 0.6f);
        d.quality = std::min(1.0f, quality_base + rng.next_float() * 0.25f);
        d.depth = 0.2f + rng.next_float() * 0.6f;
        d.accessibility = 0.3f + rng.next_float() * 0.5f;
        d.depletion_rate = 0.0001f + rng.next_float() * 0.0002f;
        d.quantity_remaining = d.quantity;
        d.era_unlock = era;
        province.deposits.push_back(d);
    };

    switch (province.tectonic_context) {
        case TectonicContext::Subduction:
            // Porphyry copper and epithermal gold from magmatic arc fluids.
            add(ResourceType::Copper, 1200.0f, 0.55f);
            add(ResourceType::Gold, 80.0f, 0.65f);
            if (rng.next_uint(3) == 0) {
                add(ResourceType::Lithium, 150.0f, 0.45f);  // arc-associated pegmatites
            }
            break;

        case TectonicContext::CratonInterior:
            // Iron formation, orogenic gold, uranium from ancient shield.
            add(ResourceType::IronOre, 1500.0f, 0.65f);
            add(ResourceType::Gold, 60.0f, 0.60f);
            if (rng.next_uint(3) == 0) {
                add(ResourceType::Uranium, 40.0f, 0.55f);
            }
            if (rng.next_uint(4) == 0) {
                add(ResourceType::Copper, 300.0f, 0.45f);  // greenstone belt copper
            }
            break;

        case TectonicContext::SedimentaryBasin:
            // Coal from ancient swamps; oil and gas from source rock; evaporite potash.
            add(ResourceType::Coal, 2000.0f, 0.65f);
            add(ResourceType::CrudeOil, 1800.0f, 0.60f);
            add(ResourceType::NaturalGas, 1200.0f, 0.55f);
            if (rng.next_uint(2) == 0) {
                add(ResourceType::Potash, 400.0f, 0.50f);
            }
            break;

        case TectonicContext::RiftZone:
            // Lithium brines; geothermal; rift-associated volcanic gas.
            add(ResourceType::Lithium, 600.0f, 0.60f);
            add(ResourceType::Geothermal, 800.0f, 0.70f);
            if (rng.next_uint(3) == 0) {
                add(ResourceType::NaturalGas, 500.0f, 0.40f);
            }
            break;

        case TectonicContext::PassiveMargin:
            // Offshore petroleum from continental shelf; phosphate; rich fishery.
            add(ResourceType::CrudeOil, 1000.0f, 0.55f);
            add(ResourceType::NaturalGas, 800.0f, 0.50f);
            add(ResourceType::Fish, 2000.0f, 0.70f);
            break;

        case TectonicContext::HotSpot:
            // Geothermal; bauxite from deep basalt weathering.
            add(ResourceType::Geothermal, 1200.0f, 0.80f);
            add(ResourceType::Bauxite, 400.0f, 0.50f);
            break;

        case TectonicContext::ContinentalCollision:
            // Marble and construction limestone; karst aquifer potential.
            add(ResourceType::LimestoneSilica, 1500.0f, 0.70f);
            if (rng.next_uint(3) == 0) {
                add(ResourceType::Copper, 200.0f, 0.40f);  // skarn deposits at marble contact
            }
            break;

        case TectonicContext::TransformFault:
            // Fault-valley alluvial gold; otherwise resource-poor.
            if (rng.next_uint(3) == 0) {
                add(ResourceType::Gold, 30.0f, 0.40f);  // placer gold in pull-apart basins
            }
            break;
    }
}

// ===========================================================================
// Stage 8 — Age-dependent resource modifiers
// ===========================================================================

void WorldGenerator::apply_age_modifiers(WorldState& world, DeterministicRNG& rng,
                                         const WorldGeneratorConfig& config) {
    // -----------------------------------------------------------------------
    // Radiogenic decay formulas from design spec §8.2.
    // Uses plate_age as proxy for geological age (per-province).
    // -----------------------------------------------------------------------
    auto u238_remaining = [](float age) -> float {
        return std::pow(0.5f, age / 4.47f);
    };
    auto u235_remaining = [](float age) -> float {
        return std::pow(0.5f, age / 0.704f);
    };
    auto uranium_age_modifier = [&](float age) -> float {
        float num = 0.993f * u238_remaining(age) + 0.007f * u235_remaining(age);
        float den = 0.993f * 1.0f + 0.007f * 1.0f;  // at age 0
        return num / den;
    };
    auto thorium_age_modifier = [](float age) -> float {
        return std::pow(0.5f, age / 14.05f);
    };
    auto geothermal_age_modifier = [](float age) -> float {
        // Mantle heat decays; blend of primordial and radiogenic.
        float primordial = std::exp(-age * 0.15f);
        float radiogenic = std::pow(0.5f, age / 4.47f) * 0.50f +
                           std::pow(0.5f, age / 14.05f) * 0.30f +
                           std::pow(0.5f, age / 1.25f) * 0.20f;
        return primordial * 0.60f + radiogenic * 0.40f;
    };
    auto he4_accumulation = [&](float age) -> float {
        return 8.0f * (1.0f - u238_remaining(age)) +
               7.0f * (1.0f - u235_remaining(age)) +
               6.0f * (1.0f - thorium_age_modifier(age));
    };

    // Track next deposit ID for new deposits (peat).
    uint32_t next_deposit_id = 0;
    for (const auto& prov : world.provinces) {
        for (const auto& d : prov.deposits) {
            if (d.id >= next_deposit_id) next_deposit_id = d.id + 1;
        }
    }

    for (auto& prov : world.provinces) {
        float age = prov.plate_age;

        // ---- Apply age modifiers to existing deposits ----
        for (auto& deposit : prov.deposits) {
            switch (deposit.type) {
                case ResourceType::Uranium:
                    deposit.quantity *= uranium_age_modifier(age);
                    deposit.quantity_remaining = deposit.quantity;
                    break;
                case ResourceType::Geothermal:
                    deposit.quantity *= geothermal_age_modifier(age);
                    deposit.quantity_remaining = deposit.quantity;
                    break;
                case ResourceType::NaturalGas: {
                    // Helium fraction increases with age (alpha decay accumulation).
                    // Store as quality modifier: higher quality = more helium content.
                    float he_frac = he4_accumulation(age) / 21.0f;  // normalise to ~1.0 at max
                    deposit.quality = std::min(1.0f,
                        deposit.quality + he_frac * 0.10f);  // helium upgrades gas quality
                    break;
                }
                case ResourceType::Copper:
                    // Cobalt fraction on copper deposits.
                    if (prov.tectonic_context == TectonicContext::CratonInterior ||
                        prov.tectonic_context == TectonicContext::SedimentaryBasin) {
                        // Sediment-hosted copper (Central African Copperbelt analog).
                        deposit.quality = std::min(1.0f,
                            deposit.quality + 0.05f + rng.next_float() * 0.15f);
                    }
                    break;
                default:
                    break;
            }
        }

        // ---- Seed peat on Histosol provinces ----
        if (prov.soil_type == SoilType::Histosol) {
            bool has_peat = false;
            for (const auto& d : prov.deposits) {
                if (d.type == ResourceType::Coal) {  // peat → coal precursor; use Coal type
                    has_peat = true;
                    break;
                }
            }
            // Peat is slow-burning fuel; use Coal type with low quality (lignite-grade).
            if (!has_peat) {
                ResourceDeposit peat{};
                peat.id = next_deposit_id++;
                peat.type = ResourceType::Coal;
                peat.quantity = 200.0f + rng.next_float() * 800.0f;
                peat.quality = 0.15f + rng.next_float() * 0.15f;  // low-grade (peat, not coal)
                peat.depth = 0.05f + rng.next_float() * 0.10f;    // very shallow
                peat.accessibility = 0.70f + rng.next_float() * 0.20f;  // easy surface extraction
                peat.depletion_rate = 0.0002f;
                peat.quantity_remaining = peat.quantity;
                peat.era_unlock = 1;
                prov.deposits.push_back(std::move(peat));
            }
        }

        // ---- Seed PGMs on impact crater provinces ----
        // Impact hydrothermal systems concentrate platinum group metals and nickel.
        // Sudbury (Ontario) and Bushveld (South Africa) are real-world analogs.
        if (prov.has_impact_crater && prov.impact_mineral_signal > 0.30f) {
            bool has_pgm = false;
            for (const auto& d : prov.deposits) {
                if (d.type == ResourceType::PlatinumGroupMetals) {
                    has_pgm = true;
                    break;
                }
            }
            if (!has_pgm) {
                ResourceDeposit pgm{};
                pgm.id = next_deposit_id++;
                pgm.type = ResourceType::PlatinumGroupMetals;
                pgm.quantity = prov.impact_mineral_signal *
                               (200.0f + rng.next_float() * 600.0f);
                pgm.quality = 0.40f + prov.impact_mineral_signal * 0.40f;
                pgm.depth = 0.20f + rng.next_float() * 0.30f;
                pgm.accessibility = 0.30f + rng.next_float() * 0.30f;  // hard to extract
                pgm.depletion_rate = 0.0005f;
                pgm.quantity_remaining = pgm.quantity;
                pgm.era_unlock = 1;
                prov.deposits.push_back(std::move(pgm));
            }
        }
    }
}

// ===========================================================================
// Stage 8 — Deterministic resource seeding: sand, aggregate, solar, wind
// ===========================================================================

void WorldGenerator::seed_deterministic_resources(WorldState& world, DeterministicRNG& rng,
                                                  const WorldGeneratorConfig& config) {
    // Track next deposit ID.
    uint32_t next_deposit_id = 0;
    for (const auto& prov : world.provinces) {
        for (const auto& d : prov.deposits) {
            if (d.id >= next_deposit_id) next_deposit_id = d.id + 1;
        }
    }

    for (auto& prov : world.provinces) {
        // Remove pre-existing archetype-seeded SolarPotential and WindPotential
        // deposits — we'll replace them with physics-derived values.
        prov.deposits.erase(
            std::remove_if(prov.deposits.begin(), prov.deposits.end(),
                           [](const ResourceDeposit& d) {
                               return d.type == ResourceType::SolarPotential ||
                                      d.type == ResourceType::WindPotential;
                           }),
            prov.deposits.end());

        // -----------------------------------------------------------------
        // Sand: marine/river-deposited; highest-volume extracted material.
        // -----------------------------------------------------------------
        float sand_qty = 0.0f;

        // Coastal sand (marine deposition).
        if (prov.geography.coastal_length_km > 50.0f) {
            sand_qty += prov.geography.coastal_length_km * 0.003f;  // COASTAL_SAND_SCALE
        }

        // River/floodplain sand.
        if (prov.soil_type == SoilType::Alluvial || prov.soil_type == SoilType::Entisol) {
            sand_qty += prov.geography.river_access * 0.40f;  // RIVER_SAND_SCALE
        }

        // Desert sand is abundant but mostly unusable for construction (too fine/rounded).
        KoppenZone kz = prov.climate.koppen_zone;
        float sand_quality = 0.85f;  // river/marine sand: angular, suitable for concrete
        if (kz == KoppenZone::BWh || kz == KoppenZone::BWk) {
            sand_qty += 0.90f;
            sand_quality = 0.05f;  // erg sand too rounded for construction
        }

        if (sand_qty > 0.01f) {
            ResourceDeposit sand{};
            sand.id = next_deposit_id++;
            sand.type = ResourceType::Sand;
            sand.quantity = std::min(1.0f, sand_qty);
            sand.quality = sand_quality;
            sand.depth = 0.0f;
            sand.accessibility = 0.90f + rng.next_float() * 0.10f;
            sand.depletion_rate = 0.0001f;  // very slow depletion
            sand.quantity_remaining = sand.quantity;
            sand.era_unlock = 1;
            prov.deposits.push_back(std::move(sand));
        }

        // -----------------------------------------------------------------
        // Aggregate: quarry rock (crushed stone, gravel).
        // -----------------------------------------------------------------
        float agg_qty = 0.0f;

        if (prov.rock_type == RockType::Igneous || prov.rock_type == RockType::Metamorphic) {
            agg_qty += 0.80f;  // granite, basalt, gneiss: excellent aggregate
        } else if (prov.rock_type == RockType::Sedimentary) {
            if (prov.geology_type == GeologyType::CarbonateSequence) {
                agg_qty += 0.70f;  // limestone: common road base + cement feedstock
            } else {
                agg_qty += 0.40f;  // softer sedimentary; lower quality
            }
        } else {
            // Mixed rock type.
            agg_qty += 0.55f;
        }

        if (agg_qty > 0.01f) {
            ResourceDeposit agg{};
            agg.id = next_deposit_id++;
            agg.type = ResourceType::Aggregate;
            agg.quantity = std::min(1.0f, agg_qty);
            agg.quality = std::min(1.0f, agg_qty);  // quality tracks hardness
            agg.depth = 0.05f + rng.next_float() * 0.10f;
            agg.accessibility = 0.85f + rng.next_float() * 0.10f;
            agg.depletion_rate = 0.0001f;
            agg.quantity_remaining = agg.quantity;
            agg.era_unlock = 1;
            prov.deposits.push_back(std::move(agg));
        }

        // -----------------------------------------------------------------
        // SolarPotential: deterministic capacity index from latitude + cloud.
        // -----------------------------------------------------------------
        float abs_lat = std::abs(prov.geography.latitude);
        float lat_solar;
        if (abs_lat < 15.0f) {
            lat_solar = 0.90f;
        } else if (abs_lat < 35.0f) {
            lat_solar = 1.00f;  // subtropical desert belt: global optimum
        } else if (abs_lat < 55.0f) {
            lat_solar = 0.70f;
        } else if (abs_lat < 70.0f) {
            lat_solar = 0.40f;
        } else {
            lat_solar = 0.10f;
        }

        // Cloud cover penalty by Köppen zone.
        float cloud_penalty = 0.25f;  // default
        switch (kz) {
            case KoppenZone::Af:  cloud_penalty = 0.35f; break;
            case KoppenZone::Am:  cloud_penalty = 0.40f; break;
            case KoppenZone::Aw:  cloud_penalty = 0.15f; break;
            case KoppenZone::BWh: cloud_penalty = 0.05f; break;
            case KoppenZone::BWk: cloud_penalty = 0.08f; break;
            case KoppenZone::BSh: cloud_penalty = 0.10f; break;
            case KoppenZone::BSk: cloud_penalty = 0.15f; break;
            case KoppenZone::Cfa: cloud_penalty = 0.25f; break;
            case KoppenZone::Cfb: cloud_penalty = 0.35f; break;
            case KoppenZone::Cfc: cloud_penalty = 0.45f; break;
            case KoppenZone::Csa: cloud_penalty = 0.10f; break;
            case KoppenZone::Csb: cloud_penalty = 0.12f; break;
            case KoppenZone::Cwa: cloud_penalty = 0.20f; break;
            case KoppenZone::Dfa: cloud_penalty = 0.20f; break;
            case KoppenZone::Dfb: cloud_penalty = 0.25f; break;
            case KoppenZone::Dfc: cloud_penalty = 0.35f; break;
            case KoppenZone::Dfd: cloud_penalty = 0.40f; break;
            case KoppenZone::ET:  cloud_penalty = 0.30f; break;
            case KoppenZone::EF:  cloud_penalty = 0.25f; break;
        }

        float solar_potential = std::max(0.0f, std::min(1.0f,
            lat_solar - cloud_penalty));

        if (solar_potential >= 0.05f) {
            ResourceDeposit solar{};
            solar.id = next_deposit_id++;
            solar.type = ResourceType::SolarPotential;
            solar.quantity = solar_potential;
            solar.quality = solar_potential;
            solar.depth = 0.0f;
            solar.accessibility = 1.0f;
            solar.depletion_rate = 0.0f;  // renewable: does not deplete
            solar.quantity_remaining = solar_potential;
            solar.era_unlock = 2;  // utility-scale solar is Era 2
            prov.deposits.push_back(std::move(solar));
        }

        // -----------------------------------------------------------------
        // WindPotential: deterministic capacity index from latitude + terrain.
        // -----------------------------------------------------------------
        float lat_wind;
        if (abs_lat < 10.0f) {
            lat_wind = 0.25f;  // ITCZ calm zone
        } else if (abs_lat < 30.0f) {
            lat_wind = 0.55f;  // trade winds
        } else if (abs_lat < 60.0f) {
            lat_wind = 0.80f;  // westerlies: strongest surface winds
        } else {
            lat_wind = 0.60f;  // polar easterlies
        }

        // Coastal bonus.
        float coastal_wind = (prov.geography.coastal_length_km > 30.0f) ? 0.20f : 0.0f;

        // Terrain factor: use roughness as proxy (low roughness = open/exposed).
        float terrain_wind;
        if (prov.geography.terrain_roughness < 0.15f) {
            terrain_wind = 1.00f;  // flat open land
        } else if (prov.geography.terrain_roughness < 0.30f) {
            terrain_wind = 0.90f;  // rolling hills
        } else if (prov.geography.terrain_roughness < 0.50f) {
            terrain_wind = 0.80f;  // moderate terrain
        } else if (prov.geography.terrain_roughness < 0.70f) {
            terrain_wind = 0.70f;  // mountainous (summits viable, valleys sheltered)
        } else {
            terrain_wind = 0.50f;  // extreme terrain
        }

        // Elevation bonus: plateaus above boundary layer.
        if (prov.geography.elevation_avg_m > 1500.0f &&
            prov.geography.terrain_roughness < 0.35f) {
            terrain_wind = 1.25f;  // plateau: elevated + flat = excellent
        }

        // Forest canopy drag.
        if (kz == KoppenZone::Af || kz == KoppenZone::Am) {
            terrain_wind *= 0.30f;  // dense rainforest canopy blocks wind
        }

        float continent_wind_penalty = prov.climate.continentality * 0.20f;

        float wind_potential = std::max(0.0f, std::min(1.0f,
            (lat_wind + coastal_wind) * terrain_wind - continent_wind_penalty));

        if (wind_potential >= 0.10f) {
            ResourceDeposit wind{};
            wind.id = next_deposit_id++;
            wind.type = ResourceType::WindPotential;
            wind.quantity = wind_potential;
            wind.quality = wind_potential;
            wind.depth = 0.0f;
            wind.accessibility = 1.0f;
            wind.depletion_rate = 0.0f;  // renewable
            wind.quantity_remaining = wind_potential;
            wind.era_unlock = 2;
            prov.deposits.push_back(std::move(wind));
        }
    }
}

// ===========================================================================
// Stage 4 — Atmosphere: temperature, precipitation, rain shadow, monsoon,
//           ocean currents, ENSO, continentality, Köppen re-derivation
// ===========================================================================

void WorldGenerator::simulate_atmosphere(WorldState& world, DeterministicRNG& rng,
                                         const WorldGeneratorConfig& config) {
    if (world.provinces.empty()) return;

    const auto& a = config.atmosphere;
    const uint32_t n = static_cast<uint32_t>(world.provinces.size());

    // -----------------------------------------------------------------------
    // Pass 1: Continentality — proxy for distance to coast.
    // Uses coastal_length_km and landlocked status as proxy since we don't
    // have a full distance-to-coast raster. Landlocked provinces with no
    // coastal neighbors are more continental.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        if (!prov.geography.is_landlocked && prov.geography.coastal_length_km > 0.0f) {
            // Coastal province: low continentality.
            prov.climate.continentality = std::max(0.0f,
                0.10f - prov.geography.coastal_length_km * 0.0005f);
        } else {
            // Approximate distance-to-coast via neighbor chain depth.
            // Count hops to nearest coastal province (BFS, max 3 hops).
            float min_dist_proxy = 999.0f;
            for (const auto& link : prov.links) {
                auto it = world.h3_province_map.find(link.neighbor_h3);
                if (it == world.h3_province_map.end()) continue;
                const auto& nb = world.provinces[it->second];
                if (!nb.geography.is_landlocked && nb.geography.coastal_length_km > 0.0f) {
                    // Direct neighbor is coastal.
                    float dist = std::sqrt(nb.geography.area_km2);
                    if (dist < min_dist_proxy) min_dist_proxy = dist;
                } else {
                    // Check 2nd-hop neighbors.
                    for (const auto& link2 : nb.links) {
                        auto it2 = world.h3_province_map.find(link2.neighbor_h3);
                        if (it2 == world.h3_province_map.end()) continue;
                        const auto& nb2 = world.provinces[it2->second];
                        if (!nb2.geography.is_landlocked && nb2.geography.coastal_length_km > 0.0f) {
                            float dist = std::sqrt(nb.geography.area_km2) +
                                         std::sqrt(nb2.geography.area_km2);
                            if (dist < min_dist_proxy) min_dist_proxy = dist;
                        }
                    }
                }
            }
            if (min_dist_proxy > 500.0f) min_dist_proxy = 500.0f;
            prov.climate.continentality = 1.0f - 1.0f / (1.0f + min_dist_proxy * a.cont_decay);
        }
    }

    // -----------------------------------------------------------------------
    // Pass 2: Base temperature from latitude + altitude.
    // Replaces/refines the archetype-set temperature values with physically
    // derived ones. Blends 60% physics + 40% archetype to maintain archetype
    // character while adding geographic realism.
    // -----------------------------------------------------------------------
    static constexpr float kLapseRateCPerM = 0.0065f;
    for (auto& prov : world.provinces) {
        float base_temp = a.temp_equator_c -
                          std::abs(prov.geography.latitude) * a.temp_lat_rate;
        float lapse = prov.geography.elevation_avg_m * kLapseRateCPerM;
        float phys_temp = base_temp - lapse;

        // Blend with archetype.
        float arch_temp = prov.climate.temperature_avg_c;
        prov.climate.temperature_avg_c = arch_temp * 0.40f + phys_temp * 0.60f;

        // Derive min/max from average with seasonal amplitude.
        float lat_amplitude = std::abs(prov.geography.latitude) * 0.25f;  // higher lat = more seasons
        float continent_amp = prov.climate.continentality * 8.0f;         // interior = bigger swings
        float amplitude = std::max(5.0f, lat_amplitude + continent_amp);

        prov.climate.temperature_min_c = prov.climate.temperature_avg_c - amplitude * 0.6f;
        prov.climate.temperature_max_c = prov.climate.temperature_avg_c + amplitude * 0.4f;

        // Clamp to physically plausible extremes.
        prov.climate.temperature_avg_c = std::max(-50.0f, std::min(50.0f, prov.climate.temperature_avg_c));
        prov.climate.temperature_min_c = std::max(-65.0f, std::min(prov.climate.temperature_avg_c,
                                                                     prov.climate.temperature_min_c));
        prov.climate.temperature_max_c = std::max(prov.climate.temperature_avg_c,
                                                   std::min(60.0f, prov.climate.temperature_max_c));
    }

    // -----------------------------------------------------------------------
    // Pass 3: Base precipitation from Hadley cell model + continentality.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        float abs_lat = std::abs(prov.geography.latitude);
        float hadley_base;

        if (abs_lat < a.itcz_lat_max) {
            // ITCZ: high convective precipitation.
            hadley_base = a.itcz_precip_mm;
        } else if (abs_lat < 30.0f) {
            // Hadley descent zone: subtropical dry belt.
            float frac = (abs_lat - a.itcz_lat_max) / (30.0f - a.itcz_lat_max);
            hadley_base = a.itcz_precip_mm * (1.0f - frac * 0.75f);  // drops to ~500mm
        } else if (abs_lat < 60.0f) {
            // Ferrel / Westerlies: moderate, variable.
            float frac = (abs_lat - 30.0f) / 30.0f;
            hadley_base = 500.0f + 400.0f * (1.0f - frac);  // 500-900mm
        } else {
            // Polar: cold desert.
            hadley_base = 200.0f + (90.0f - abs_lat) * 10.0f;  // 200-500mm
        }

        // Continentality reduces precipitation (interior = drier).
        float cont_factor = 1.0f - prov.climate.continentality * 0.50f;
        float phys_precip = hadley_base * cont_factor;

        // Blend with archetype (50/50; precipitation is less archetype-bound).
        float arch_precip = prov.climate.precipitation_mm;
        if (arch_precip < 1.0f) arch_precip = phys_precip;  // unset archetype
        prov.climate.precipitation_mm = arch_precip * 0.50f + phys_precip * 0.50f;
    }

    // -----------------------------------------------------------------------
    // Pass 4: Rain shadow — simplified province-level implementation.
    // For each province with high terrain_roughness, reduce precipitation
    // of downwind neighbors based on wind direction for latitude band.
    // -----------------------------------------------------------------------
    // Determine prevailing wind dx for each province's latitude band.
    auto wind_direction = [](float latitude) -> int {
        float abs_lat = std::abs(latitude);
        if (abs_lat < 8.0f)  return 0;   // ITCZ: no persistent direction
        if (abs_lat < 30.0f) return -1;   // Trade winds: E→W (moisture moves west)
        if (abs_lat < 60.0f) return 1;    // Westerlies: W→E
        return -1;                         // Polar easterlies
    };

    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        if (prov.geography.terrain_roughness < 0.40f) continue;  // no significant orographic lift

        int wind_dx = wind_direction(prov.geography.latitude);
        if (wind_dx == 0) continue;  // ITCZ: convective, no shadow

        float lift = prov.geography.terrain_roughness * a.rain_shadow_lift_coeff;
        float shadow_strength = lift * a.rain_shadow_precip_eff;
        shadow_strength = std::min(shadow_strength, a.rain_shadow_max_depletion);

        // Windward side gets orographic enhancement; leeward gets shadow.
        for (const auto& link : prov.links) {
            auto it = world.h3_province_map.find(link.neighbor_h3);
            if (it == world.h3_province_map.end()) continue;
            auto& nb = world.provinces[it->second];

            // Determine if neighbor is downwind (leeward).
            float lng_diff = nb.geography.longitude - prov.geography.longitude;
            bool is_leeward = (wind_dx > 0 && lng_diff > 0.0f) ||
                              (wind_dx < 0 && lng_diff < 0.0f);

            if (is_leeward && nb.geography.elevation_avg_m < prov.geography.elevation_avg_m) {
                // Rain shadow: reduce precipitation.
                nb.climate.precipitation_mm *= (1.0f - shadow_strength);
                nb.climate.precipitation_mm = std::max(50.0f, nb.climate.precipitation_mm);
            } else if (!is_leeward && nb.geography.elevation_avg_m < prov.geography.elevation_avg_m) {
                // Windward enhancement: moisture is lifted and deposited.
                float windward_bonus = shadow_strength * 0.40f;
                prov.climate.precipitation_mm *= (1.0f + windward_bonus);
                prov.climate.precipitation_mm = std::min(4000.0f, prov.climate.precipitation_mm);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Pass 5: Ocean currents — cold upwelling and warm poleward.
    // Simplified: assign based on latitude band + coastal + archetype hints.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        if (prov.geography.is_landlocked || prov.geography.coastal_length_km < 20.0f) continue;

        float abs_lat = std::abs(prov.geography.latitude);

        // Cold upwelling currents: subtropical western ocean margins (15-35°).
        // In simplified model: coastal provinces in subtropical dry belt with low precip.
        if (abs_lat >= a.cold_current_lat_min && abs_lat <= a.cold_current_lat_max) {
            // Probability based on how dry this coastal area already is (dry coast = likely upwelling).
            float dryness = 1.0f - std::min(1.0f, prov.climate.precipitation_mm / 800.0f);
            if (dryness > 0.40f && rng.next_float() < dryness * 0.50f) {
                prov.climate.cold_current_adjacent = true;
                prov.climate.temperature_avg_c -= a.cold_current_temp_drop;
                prov.climate.temperature_min_c -= a.cold_current_temp_drop;
                prov.climate.temperature_max_c -= a.cold_current_temp_drop;
                prov.climate.precipitation_mm *= a.cold_current_precip_suppression;
                prov.climate.precipitation_mm = std::max(20.0f, prov.climate.precipitation_mm);
            }
        }

        // Warm currents: poleward eastern ocean margins (40-65°).
        if (abs_lat >= a.warm_current_lat_min && abs_lat <= a.warm_current_lat_max) {
            // Probability: coastal provinces at high latitude are likely warm-current influenced.
            if (rng.next_float() < 0.30f) {
                prov.climate.temperature_avg_c += a.warm_current_temp_boost;
                prov.climate.temperature_min_c += a.warm_current_temp_boost * 0.80f;
                prov.climate.temperature_max_c += a.warm_current_temp_boost * 0.40f;
                prov.climate.precipitation_mm *= (1.0f + a.warm_current_precip_boost);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Pass 6: Monsoon detection and modification.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        float abs_lat = std::abs(prov.geography.latitude);
        if (abs_lat < a.monsoon_lat_min || abs_lat > a.monsoon_lat_max) continue;
        if (prov.geography.is_landlocked) continue;  // need ocean adjacency
        if (prov.climate.cold_current_adjacent) continue;  // cold current suppresses monsoon

        // Coastal or near-coastal provinces in monsoon belt.
        bool near_coast = !prov.geography.is_landlocked &&
                          (prov.geography.coastal_length_km > 0.0f || prov.climate.continentality < 0.40f);
        if (!near_coast) continue;

        prov.climate.is_monsoon = true;
        prov.climate.precipitation_mm *= (1.0f + a.monsoon_precip_bonus);
        prov.climate.precipitation_seasonality =
            std::max(prov.climate.precipitation_seasonality, a.monsoon_seasonality);
        prov.climate.flood_vulnerability =
            std::min(1.0f, prov.climate.flood_vulnerability + a.monsoon_flood_bonus);
    }

    // -----------------------------------------------------------------------
    // Pass 7: ENSO susceptibility per province.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        float abs_lat = std::abs(prov.geography.latitude);
        float base;

        if (abs_lat < 10.0f) {
            base = 0.90f;
        } else if (abs_lat < 25.0f) {
            base = 0.80f;
        } else if (abs_lat < 40.0f) {
            base = 0.50f;
        } else if (abs_lat < 60.0f) {
            base = 0.25f;
        } else {
            base = 0.05f;
        }

        // Continentality dampens ENSO signal.
        prov.climate.enso_susceptibility = base * (1.0f - prov.climate.continentality * 0.40f);
    }

    // -----------------------------------------------------------------------
    // Pass 8: Precipitation seasonality for non-monsoon provinces.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        if (prov.climate.is_monsoon) continue;  // already set in monsoon pass

        float abs_lat = std::abs(prov.geography.latitude);
        if (abs_lat < 10.0f) {
            // Equatorial: relatively even year-round.
            prov.climate.precipitation_seasonality = 0.10f + rng.next_float() * 0.10f;
        } else if (abs_lat < 30.0f) {
            // Subtropical: dry season + wet season.
            prov.climate.precipitation_seasonality = 0.30f + rng.next_float() * 0.25f;
        } else if (abs_lat < 60.0f) {
            // Temperate/oceanic: moderate seasonality.
            float cont_effect = prov.climate.continentality * 0.15f;
            prov.climate.precipitation_seasonality = 0.15f + cont_effect + rng.next_float() * 0.10f;
        } else {
            // Polar: low precipitation, concentrated in warmer months.
            prov.climate.precipitation_seasonality = 0.25f + rng.next_float() * 0.15f;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 9: Re-derive Köppen zone from physics.
    // Uses {temperature_avg_c, precipitation_mm, precipitation_seasonality}
    // to classify. Blends with archetype: 70% physics / 30% archetype (keep
    // archetype if physics is ambiguous within the same major group).
    // -----------------------------------------------------------------------
    auto classify_koppen = [](float temp_avg, float precip_mm, float seasonality) -> KoppenZone {
        // Polar
        if (temp_avg < -10.0f) return KoppenZone::EF;
        if (temp_avg < 0.0f)   return KoppenZone::ET;

        // Hot desert / steppe
        if (precip_mm < 250.0f) {
            return temp_avg > 18.0f ? KoppenZone::BWh : KoppenZone::BWk;
        }
        if (precip_mm < 500.0f) {
            return temp_avg > 18.0f ? KoppenZone::BSh : KoppenZone::BSk;
        }

        // Tropical (warm year-round + wet)
        if (temp_avg > 22.0f && precip_mm > 1500.0f) {
            if (seasonality < 0.30f) return KoppenZone::Af;
            if (seasonality < 0.60f) return KoppenZone::Am;
            return KoppenZone::Aw;
        }
        if (temp_avg > 20.0f && precip_mm > 1000.0f) {
            return seasonality > 0.50f ? KoppenZone::Aw : KoppenZone::Am;
        }

        // Continental (cold winters)
        if (temp_avg < 5.0f) {
            if (precip_mm < 400.0f) return KoppenZone::Dfd;
            return temp_avg < -5.0f ? KoppenZone::Dfc : KoppenZone::Dfb;
        }

        // Temperate
        if (seasonality > 0.55f) {
            // Dry summer = Mediterranean.
            return temp_avg > 16.0f ? KoppenZone::Csa : KoppenZone::Csb;
        }
        if (seasonality > 0.40f && temp_avg > 18.0f) {
            return KoppenZone::Cwa;  // subtropical monsoon
        }
        if (temp_avg > 14.0f) {
            return precip_mm > 1000.0f ? KoppenZone::Cfa : KoppenZone::Cfb;
        }
        if (temp_avg > 8.0f) return KoppenZone::Cfb;
        if (temp_avg > 3.0f) return KoppenZone::Cfc;

        // Continental fallback.
        return temp_avg > 10.0f ? KoppenZone::Dfa : KoppenZone::Dfb;
    };

    // Helper: same major group?
    auto major_group = [](KoppenZone kz) -> uint8_t {
        uint8_t v = static_cast<uint8_t>(kz);
        if (v <= 2) return 0;   // Tropical
        if (v <= 6) return 1;   // Arid
        if (v <= 12) return 2;  // Temperate
        if (v <= 16) return 3;  // Continental
        return 4;               // Polar
    };

    for (auto& prov : world.provinces) {
        KoppenZone physics_kz = classify_koppen(
            prov.climate.temperature_avg_c,
            prov.climate.precipitation_mm,
            prov.climate.precipitation_seasonality);

        KoppenZone archetype_kz = prov.climate.koppen_zone;

        // If physics and archetype agree on major group, keep archetype (finer detail).
        // If they disagree, physics wins.
        if (major_group(physics_kz) == major_group(archetype_kz)) {
            // Keep archetype — finer detail within same group.
        } else {
            prov.climate.koppen_zone = physics_kz;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 10: Geographic vulnerability derivation.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        KoppenZone kz = prov.climate.koppen_zone;
        float abs_lat = std::abs(prov.geography.latitude);
        float base_vuln;

        // Polar/alpine
        if (kz == KoppenZone::EF || kz == KoppenZone::ET ||
            prov.geography.elevation_avg_m > 3000.0f) {
            base_vuln = 0.85f + rng.next_float() * 0.15f;
        }
        // Tropical coastal
        else if ((kz == KoppenZone::Af || kz == KoppenZone::Am) &&
                 prov.geography.coastal_length_km > 50.0f) {
            base_vuln = 0.80f + rng.next_float() * 0.15f;
        }
        // Hot arid
        else if (kz == KoppenZone::BWh || kz == KoppenZone::BSh) {
            base_vuln = 0.65f + rng.next_float() * 0.15f;
        }
        // Subtropical / tropical savanna
        else if (kz == KoppenZone::Cfa || kz == KoppenZone::Cwa || kz == KoppenZone::Aw) {
            base_vuln = 0.45f + rng.next_float() * 0.20f;
        }
        // Continental warm
        else if (kz == KoppenZone::Dfa || kz == KoppenZone::Dfb) {
            base_vuln = 0.30f + rng.next_float() * 0.15f;
        }
        // Subarctic non-coastal
        else if ((kz == KoppenZone::Dfc || kz == KoppenZone::Dfd) &&
                 prov.geography.is_landlocked) {
            base_vuln = 0.25f + rng.next_float() * 0.15f;
        }
        // Temperate oceanic
        else if (kz == KoppenZone::Cfb || kz == KoppenZone::Cfc) {
            base_vuln = 0.20f + rng.next_float() * 0.15f;
        }
        // Default
        else {
            base_vuln = 0.35f + rng.next_float() * 0.20f;
        }

        // Tectonic stress adds vulnerability (earthquake/eruption risk).
        base_vuln += prov.tectonic_stress * 0.10f;

        prov.climate.geographic_vulnerability = std::min(1.0f, std::max(0.0f, base_vuln));
    }

    // -----------------------------------------------------------------------
    // Pass 11: Drought vulnerability refinement from atmosphere physics.
    // Blend the archetype-set value with a physics-derived estimate.
    // -----------------------------------------------------------------------
    for (auto& prov : world.provinces) {
        float phys_drought = 0.0f;
        KoppenZone kz = prov.climate.koppen_zone;

        if (kz == KoppenZone::BWh || kz == KoppenZone::BWk) {
            phys_drought = 0.70f + rng.next_float() * 0.20f;
        } else if (kz == KoppenZone::BSh || kz == KoppenZone::BSk) {
            phys_drought = 0.50f + rng.next_float() * 0.20f;
        } else if (kz == KoppenZone::Csa || kz == KoppenZone::Csb) {
            phys_drought = 0.35f + rng.next_float() * 0.15f;  // dry summers
        } else if (kz == KoppenZone::Aw) {
            phys_drought = 0.30f + rng.next_float() * 0.15f;  // dry season
        } else if (kz == KoppenZone::Af || kz == KoppenZone::Am) {
            phys_drought = 0.05f + rng.next_float() * 0.10f;  // wet tropics
        } else if (kz == KoppenZone::Cfb || kz == KoppenZone::Cfc) {
            phys_drought = 0.10f + rng.next_float() * 0.10f;
        } else {
            phys_drought = 0.20f + rng.next_float() * 0.15f;
        }

        // Blend 50% physics, 50% archetype.
        prov.climate.drought_vulnerability =
            prov.climate.drought_vulnerability * 0.50f + phys_drought * 0.50f;
    }
}

// ===========================================================================
// Stage 3 — Hydrology: drainage basins, rivers, snowpack, springs, ports
// ===========================================================================

void WorldGenerator::calculate_hydrology(WorldState& world, DeterministicRNG& rng,
                                         const WorldGeneratorConfig& config) {
    if (world.provinces.empty()) return;

    const auto& h = config.hydrology;
    const uint32_t n = static_cast<uint32_t>(world.provinces.size());

    // -----------------------------------------------------------------------
    // Pass 1: Drainage direction — each province drains to its lowest neighbor.
    // Coastal provinces drain to ocean (sentinel -1). Provinces lower than all
    // neighbors are local minima (endorheic basin seeds).
    // -----------------------------------------------------------------------
    std::vector<int32_t> drain_target(n, -1);  // -1 = drains to ocean / is local minimum
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];

        // Coastal provinces drain to ocean.
        if (prov.geography.coastal_length_km > 0.0f && !prov.geography.is_landlocked) {
            drain_target[i] = -1;
            continue;
        }

        float my_elev = prov.geography.elevation_avg_m;
        float lowest_elev = my_elev;
        int32_t lowest_idx = -1;

        for (const auto& link : prov.links) {
            auto it = world.h3_province_map.find(link.neighbor_h3);
            if (it == world.h3_province_map.end()) continue;
            uint32_t nb_idx = it->second;
            float nb_elev = world.provinces[nb_idx].geography.elevation_avg_m;
            if (nb_elev < lowest_elev) {
                lowest_elev = nb_elev;
                lowest_idx = static_cast<int32_t>(nb_idx);
            }
        }
        drain_target[i] = lowest_idx;  // -1 if this is a local minimum
    }

    // -----------------------------------------------------------------------
    // Pass 2: Compute topological order (headwaters first, mouths last).
    // Count upstream dependencies, then process in BFS order from sources.
    // -----------------------------------------------------------------------
    std::vector<uint32_t> upstream_count(n, 0);
    for (uint32_t i = 0; i < n; ++i) {
        if (drain_target[i] >= 0) {
            upstream_count[static_cast<uint32_t>(drain_target[i])]++;
        }
    }

    std::vector<uint32_t> topo_order;
    topo_order.reserve(n);
    // Seeds: provinces with no upstream tributaries.
    for (uint32_t i = 0; i < n; ++i) {
        if (upstream_count[i] == 0) {
            topo_order.push_back(i);
        }
    }

    // BFS drain (Kahn's algorithm on drainage DAG).
    std::vector<uint32_t> working_upstream = upstream_count;
    for (size_t head = 0; head < topo_order.size(); ++head) {
        uint32_t idx = topo_order[head];
        int32_t target = drain_target[idx];
        if (target >= 0) {
            uint32_t t = static_cast<uint32_t>(target);
            working_upstream[t]--;
            if (working_upstream[t] == 0) {
                topo_order.push_back(t);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Pass 3: Catchment area accumulation (headwaters → mouths).
    // Each province starts with area 1.0 (itself) and accumulates from upstream.
    // -----------------------------------------------------------------------
    std::vector<float> catchment_area(n, 1.0f);
    for (uint32_t idx : topo_order) {
        int32_t target = drain_target[idx];
        if (target >= 0) {
            catchment_area[static_cast<uint32_t>(target)] += catchment_area[idx];
        }
    }

    // Max catchment for normalisation.
    float max_catchment = 1.0f;
    for (float ca : catchment_area) {
        if (ca > max_catchment) max_catchment = ca;
    }

    // -----------------------------------------------------------------------
    // Pass 4: Derive river_access from catchment + precipitation.
    // Blends with archetype-set value (archetype 60%, hydrology 40%).
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        float catch_frac = catchment_area[i] / max_catchment;
        float precip_contrib = prov.climate.precipitation_mm * h.precip_river_scale;
        float hydro_river = std::min(1.0f, catch_frac * h.catchment_river_scale * max_catchment
                                            + precip_contrib);

        // Blend: keep archetype influence (stability) while adding hydrology signal.
        float archetype_river = prov.geography.river_access;
        prov.geography.river_access = std::min(1.0f,
            archetype_river * 0.60f + hydro_river * 0.40f);
    }

    // -----------------------------------------------------------------------
    // Pass 5: Endorheic basin detection.
    // Local minima (drain_target == -1 AND landlocked) are endorheic seeds.
    // Propagate flag upstream through drainage graph.
    // -----------------------------------------------------------------------
    std::vector<bool> is_endorheic_basin(n, false);
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        if (drain_target[i] == -1 && prov.geography.is_landlocked) {
            is_endorheic_basin[i] = true;
        }
    }

    // Propagate endorheic flag upstream (reverse topo order = mouths → headwaters).
    for (auto it = topo_order.rbegin(); it != topo_order.rend(); ++it) {
        uint32_t idx = *it;
        int32_t target = drain_target[idx];
        if (target >= 0 && is_endorheic_basin[static_cast<uint32_t>(target)]) {
            is_endorheic_basin[idx] = true;
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
        world.provinces[i].geography.is_endorheic = is_endorheic_basin[i];
    }

    // -----------------------------------------------------------------------
    // Pass 6: Snowpack computation and downstream snowmelt propagation.
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        float snowline = std::max(0.0f,
            h.snowline_base_m - std::abs(prov.geography.latitude) * h.snowline_lat_rate);

        if (prov.geography.elevation_avg_m < snowline) {
            prov.geography.snowpack_contribution = 0.0f;
            continue;
        }

        float snow_fraction = std::min(1.0f,
            (prov.geography.elevation_avg_m - snowline) / 1000.0f);

        prov.geography.snowpack_contribution =
            snow_fraction * prov.climate.precipitation_mm * h.snowpack_retention;

        // Local snowpack feeds local rivers.
        prov.geography.river_access = std::min(1.0f,
            prov.geography.river_access +
            prov.geography.snowpack_contribution * h.snowmelt_river_scale);
    }

    // Propagate snowmelt downstream through drainage graph (topo order).
    for (uint32_t idx : topo_order) {
        auto& prov = world.provinces[idx];
        if (prov.geography.snowpack_contribution <= 0.0f) continue;

        float melt_volume = prov.geography.snowpack_contribution * prov.geography.area_km2;

        // Walk downstream chain, applying distance decay.
        int32_t current = drain_target[idx];
        float distance_km = 0.0f;

        while (current >= 0) {
            auto& downstream = world.provinces[static_cast<uint32_t>(current)];

            // Approximate distance as area-based step (~sqrt(area)).
            distance_km += std::sqrt(downstream.geography.area_km2);
            float decay = std::exp(-distance_km / h.melt_decay_km);

            if (decay < 0.01f) break;  // negligible contribution

            float contribution = (melt_volume * decay / std::max(1.0f, downstream.geography.area_km2))
                                 * h.snowmelt_river_scale;
            downstream.geography.river_access = std::min(1.0f,
                downstream.geography.river_access + contribution);
            downstream.geography.snowmelt_fed = true;

            current = drain_target[static_cast<uint32_t>(current)];
        }
    }

    // -----------------------------------------------------------------------
    // Pass 7: River flow regime classification.
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];

        if (prov.geography.river_access < 0.05f) {
            prov.geography.river_flow_regime = RiverFlowRegime::None;
        } else if (prov.geography.snowpack_contribution > 100.0f &&
                   prov.geography.elevation_avg_m > 3000.0f) {
            prov.geography.river_flow_regime = RiverFlowRegime::Glacierfed;
        } else if (prov.geography.snowmelt_fed ||
                   prov.geography.snowpack_contribution > 50.0f) {
            // Perennial if precipitation supports year-round base flow.
            if (prov.climate.precipitation_mm > 400.0f) {
                prov.geography.river_flow_regime = RiverFlowRegime::SnowmeltPerennial;
            } else {
                prov.geography.river_flow_regime = RiverFlowRegime::SnowmeltEphemeral;
            }
        } else if (prov.climate.precipitation_mm > 600.0f) {
            prov.geography.river_flow_regime = RiverFlowRegime::RainfedPerennial;
        } else if (prov.climate.precipitation_mm > 200.0f) {
            prov.geography.river_flow_regime = RiverFlowRegime::RainfedEphemeral;
        } else {
            prov.geography.river_flow_regime = RiverFlowRegime::None;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 8: Groundwater reserve derivation.
    // Driven by geology (alluvial fill, sedimentary → porous) + precipitation.
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        float gw = 0.10f;  // baseline

        // Geology contribution.
        if (prov.geology_type == GeologyType::AlluvialFill) {
            gw += h.alluvial_gw_bonus;
        } else if (prov.geology_type == GeologyType::SedimentarySequence ||
                   prov.geology_type == GeologyType::CarbonateSequence) {
            gw += h.sedimentary_gw_bonus;
        }

        // Precipitation recharge.
        gw += prov.climate.precipitation_mm * h.gw_precip_scale;

        // Floodplain bonus: high river access + flat terrain = saturated alluvium.
        if (prov.geography.river_access > 0.4f && prov.geography.terrain_roughness < 0.25f) {
            gw += h.floodplain_gw_bonus;
        }

        // RNG variation (±10%).
        gw *= (0.90f + rng.next_float() * 0.20f);

        prov.geography.groundwater_reserve = std::min(1.0f, std::max(0.0f, gw));
    }

    // -----------------------------------------------------------------------
    // Pass 9: Alluvial fan detection.
    // Province at foot of steep neighbor where elevation drops significantly.
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        if (prov.geography.terrain_roughness > 0.40f) continue;  // fans form on flat terrain

        for (const auto& link : prov.links) {
            auto it = world.h3_province_map.find(link.neighbor_h3);
            if (it == world.h3_province_map.end()) continue;
            const auto& nb = world.provinces[it->second];

            if (nb.geography.terrain_roughness > h.fan_roughness_min &&
                (nb.geography.elevation_avg_m - prov.geography.elevation_avg_m) > h.fan_elev_drop_m) {
                prov.geography.has_alluvial_fan = true;
                prov.agricultural_productivity = std::min(1.0f,
                    prov.agricultural_productivity + h.fan_ag_bonus);
                prov.geography.groundwater_reserve = std::min(1.0f,
                    prov.geography.groundwater_reserve + h.fan_gw_bonus);
                break;  // one fan is enough
            }
        }
    }

    // -----------------------------------------------------------------------
    // Pass 10: River delta detection.
    // Coastal province with large upstream catchment area.
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        if (prov.geography.is_landlocked) continue;
        if (prov.geography.coastal_length_km < 10.0f) continue;

        float catch_frac = catchment_area[i] / max_catchment;
        if (catch_frac < h.delta_catchment_min) continue;

        prov.geography.is_delta = true;

        // Delta effects: high ag (natural fertilisation), high flood, moderate port.
        prov.agricultural_productivity = std::max(prov.agricultural_productivity, h.delta_ag_cap);
        prov.climate.flood_vulnerability = std::max(prov.climate.flood_vulnerability,
                                                     h.delta_flood_floor);

        // Delta ports are shallow (shifting channels, requires dredging).
        float delta_port = h.delta_port_min + rng.next_float() * (h.delta_port_max - h.delta_port_min);
        prov.geography.port_capacity = std::min(prov.geography.port_capacity, delta_port);
    }

    // -----------------------------------------------------------------------
    // Pass 11: Artesian spring and oasis detection.
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        if (prov.geography.groundwater_reserve < h.spring_gw_min) continue;

        // Check for high-elevation, wet recharge neighbors with permeable rock.
        bool has_recharge_neighbor = false;
        float recharge_precip_sum = 0.0f;
        uint32_t recharge_count = 0;

        for (const auto& link : prov.links) {
            auto it = world.h3_province_map.find(link.neighbor_h3);
            if (it == world.h3_province_map.end()) continue;
            const auto& nb = world.provinces[it->second];

            if (nb.geography.elevation_avg_m >
                    prov.geography.elevation_avg_m + h.spring_elev_diff_m &&
                nb.climate.precipitation_mm > h.spring_precip_min_mm &&
                (nb.rock_type == RockType::Sedimentary || nb.rock_type == RockType::Mixed)) {
                has_recharge_neighbor = true;
                recharge_precip_sum += nb.climate.precipitation_mm;
                recharge_count++;
            }
        }

        if (!has_recharge_neighbor) continue;

        // Granite shield has no confining layer — water disperses.
        if (prov.rock_type == RockType::Igneous &&
            prov.geology_type == GeologyType::GraniteShield) {
            continue;
        }

        prov.geography.has_artesian_spring = true;
        float mean_recharge_precip = recharge_precip_sum / static_cast<float>(recharge_count);
        prov.geography.spring_flow_index = std::min(1.0f,
            prov.geography.groundwater_reserve
            * (mean_recharge_precip / 1000.0f)
            * h.artesian_flow_scale);

        // Oasis: desert province with spring.
        KoppenZone kz = prov.climate.koppen_zone;
        if (kz == KoppenZone::BWh || kz == KoppenZone::BWk ||
            kz == KoppenZone::BSh || kz == KoppenZone::BSk) {
            prov.geography.is_oasis = true;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 12: Port capacity baseline (pre-Stage 7 override).
    // Replaces archetype-set port_capacity with physically derived values
    // for non-landlocked provinces. Blends with archetype (50/50) to maintain
    // archetype influence while adding geographic realism.
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        if (prov.geography.is_landlocked || prov.geography.coastal_length_km <= 0.0f) {
            prov.geography.port_capacity = 0.0f;
            continue;
        }

        float coast_factor = std::min(1.0f,
            prov.geography.coastal_length_km / h.port_coast_norm_km);
        float terrain_factor = 1.0f - prov.geography.terrain_roughness * h.port_roughness_penalty;
        float elev_factor = 1.0f - std::min(h.port_elev_max_penalty,
            prov.geography.elevation_avg_m / h.port_elev_cap_m);
        float river_mouth_bonus = prov.geography.river_access * h.port_river_mouth_bonus;

        float geo_port = std::min(1.0f, std::max(0.0f,
            coast_factor * terrain_factor * elev_factor + river_mouth_bonus));

        // Blend with archetype value.
        float archetype_port = prov.geography.port_capacity;
        prov.geography.port_capacity = std::min(1.0f,
            archetype_port * 0.50f + geo_port * 0.50f);

        // Island isolation minimum: island must be accessible.
        if (prov.island_isolation && prov.geography.port_capacity < 0.50f) {
            prov.geography.port_capacity = 0.50f;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 13: Seed lithium in endorheic basins (brine deposits).
    // -----------------------------------------------------------------------
    uint32_t next_deposit_id = 0;
    for (auto& prov : world.provinces) {
        for (const auto& d : prov.deposits) {
            if (d.id >= next_deposit_id) next_deposit_id = d.id + 1;
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
        auto& prov = world.provinces[i];
        if (!prov.geography.is_endorheic) continue;

        // Check if province already has lithium.
        bool has_lithium = false;
        for (const auto& d : prov.deposits) {
            if (d.type == ResourceType::Lithium) {
                has_lithium = true;
                break;
            }
        }
        if (has_lithium) continue;

        // Probabilistic lithium brine deposit.
        if (rng.next_float() < h.endorheic_lithium_chance) {
            ResourceDeposit deposit{};
            deposit.id = next_deposit_id++;
            deposit.type = ResourceType::Lithium;
            deposit.quantity = 500.0f + rng.next_float() * 1500.0f;
            deposit.quality = 0.5f + rng.next_float() * 0.3f;
            deposit.depth = 0.1f + rng.next_float() * 0.3f;  // shallow brine
            deposit.accessibility = 0.5f + rng.next_float() * 0.3f;
            deposit.depletion_rate = 0.0001f;
            deposit.quantity_remaining = deposit.quantity;
            deposit.era_unlock = 1;
            prov.deposits.push_back(std::move(deposit));
        }
    }
}

// ===========================================================================
// Stage 2 derived — Terrain flag detection
// ===========================================================================

void WorldGenerator::detect_terrain_flags(WorldState& world,
                                          const WorldGeneratorConfig& config) {
    const auto& t = config.terrain;
    for (auto& prov : world.provinces) {
        // Island isolation: no land or river links.
        if (!prov.links.empty()) {
            bool has_land_link = false;
            for (const auto& link : prov.links) {
                if (link.type == LinkType::Land || link.type == LinkType::River) {
                    has_land_link = true;
                    break;
                }
            }
            prov.island_isolation = !has_land_link;
        }

        // Mountain pass detection: high-terrain province flanked by lower provinces on
        // multiple sides. Creates transit chokepoints as specified in WorldGen v0.18 §ProvinceLink.
        if (prov.geography.terrain_roughness > t.pass_roughness_min &&
            prov.geography.elevation_avg_m > t.pass_elevation_min_m) {
            float my_elev = prov.geography.elevation_avg_m;
            uint32_t low_neighbor_count = 0;

            for (const auto& link : prov.links) {
                auto it = world.h3_province_map.find(link.neighbor_h3);
                if (it == world.h3_province_map.end()) continue;
                const auto& nb = world.provinces[it->second];
                if (nb.geography.elevation_avg_m < my_elev * t.pass_low_neighbor_fraction) {
                    ++low_neighbor_count;
                }
            }

            if (low_neighbor_count >= 2) {
                prov.is_mountain_pass = true;
                for (auto& link : prov.links) {
                    auto it = world.h3_province_map.find(link.neighbor_h3);
                    if (it == world.h3_province_map.end()) continue;
                    const auto& nb = world.provinces[it->second];
                    if (nb.geography.elevation_avg_m < my_elev * t.pass_low_neighbor_fraction) {
                        link.transit_terrain_cost =
                            std::max(t.pass_transit_cost_floor,
                                     link.transit_terrain_cost * t.pass_transit_cost_factor);
                    }
                }
            }
        }
    }
}

// ===========================================================================
// Stage 4a — Province geography refinement (elevation + temperature lapse rate)
// ===========================================================================

void WorldGenerator::refine_province_geography(WorldState& world,
                                               const WorldGeneratorConfig& config) {
    const auto& t = config.terrain;

    for (auto& prov : world.provinces) {
        // ---- Elevation-roughness correlation ----
        // Scale the archetype-set value by a roughness-driven factor.
        // Final factor = elev_roughness_base + roughness * elev_roughness_range.
        float roughness_factor =
            t.elev_roughness_base + prov.geography.terrain_roughness * t.elev_roughness_range;
        prov.geography.elevation_avg_m =
            std::max(t.elev_floor_m, prov.geography.elevation_avg_m * roughness_factor);

        // Note: temperature lapse rate from elevation is now handled in
        // simulate_atmosphere() (Stage 4). This function only scales elevation.
    }
}

// ===========================================================================
// Stage 4b — Economic geography seeding (trade openness, wildfire, employment)
// ===========================================================================

void WorldGenerator::seed_economic_geography(WorldState& world,
                                             const WorldGeneratorConfig& config) {
    const auto& e = config.economy;
    for (auto& prov : world.provinces) {
        // ---- Trade openness from geography ----
        float geo_openness = prov.geography.port_capacity * e.trade_port_weight
                           + prov.geography.river_access   * e.trade_river_weight
                           + prov.infrastructure_rating    * e.trade_infra_weight;
        if (prov.geography.is_landlocked) geo_openness -= e.trade_landlocked_penalty;
        if (prov.island_isolation)        geo_openness -= e.trade_island_penalty;
        geo_openness = std::max(0.08f, std::min(0.85f, geo_openness));

        if (prov.trade_openness < 0.01f) {
            prov.trade_openness = geo_openness;
        } else {
            prov.trade_openness = std::max(0.10f, std::min(0.95f,
                prov.trade_openness * e.trade_existing_blend +
                geo_openness        * e.trade_geo_blend));
        }

        // ---- Wildfire vulnerability from climate zone ----
        float base_wildfire;
        switch (prov.climate.koppen_zone) {
            case KoppenZone::Csa:
            case KoppenZone::Csb:
                base_wildfire = e.wildfire_mediterranean;
                break;
            case KoppenZone::BSh:
            case KoppenZone::BSk:
            case KoppenZone::Aw:
                base_wildfire = e.wildfire_steppe_savanna;
                break;
            case KoppenZone::BWh:
            case KoppenZone::BWk:
                base_wildfire = e.wildfire_desert;
                break;
            case KoppenZone::Cfa:
            case KoppenZone::Cfb:
            case KoppenZone::Cfc:
            case KoppenZone::Cwa:
            case KoppenZone::Dfa:
            case KoppenZone::Dfb:
                base_wildfire = e.wildfire_temperate;
                break;
            case KoppenZone::Dfc:
            case KoppenZone::Dfd:
                base_wildfire = e.wildfire_boreal;
                break;
            case KoppenZone::Af:
            case KoppenZone::Am:
                base_wildfire = e.wildfire_wet_tropical;
                break;
            case KoppenZone::ET:
            case KoppenZone::EF:
                base_wildfire = e.wildfire_polar;
                break;
            default:
                base_wildfire = e.wildfire_temperate;
                break;
        }
        float drought_amp = 1.0f + prov.climate.drought_vulnerability * e.wildfire_drought_amp;
        prov.climate.wildfire_vulnerability =
            std::max(0.01f, std::min(0.95f, base_wildfire * drought_amp));

        // ---- Formal employment rate from infrastructure and governance ----
        float infra_bonus        = (prov.infrastructure_rating  - 0.50f) * e.employment_infra_scale;
        float corruption_penalty = prov.political.corruption_index       * e.employment_corruption_scale;
        float inequality_penalty = prov.conditions.inequality_index      * e.employment_inequality_scale;
        prov.conditions.formal_employment_rate =
            std::max(0.20f, std::min(0.95f,
                prov.conditions.formal_employment_rate
                    + infra_bonus - corruption_penalty - inequality_penalty));
    }
}

// ===========================================================================
// Stage 5+6 — Soils and Biomes (simplified WorldGen v0.18 pass)
// ===========================================================================

void WorldGenerator::derive_soils_and_biomes(WorldState& world, DeterministicRNG& rng,
                                              const WorldGeneratorConfig& config) {
    const auto& s = config.soils;
    for (auto& prov : world.provinces) {
        // ---- Stage 5a: Soil type classification ----
        // Derived from geology + climate + terrain + hydrology per design spec §5.
        KoppenZone kz = prov.climate.koppen_zone;
        bool is_volcanic = (prov.tectonic_context == TectonicContext::HotSpot ||
                            prov.tectonic_context == TectonicContext::Subduction);

        if (prov.has_permafrost || kz == KoppenZone::ET || kz == KoppenZone::EF) {
            prov.soil_type = SoilType::Cryosol;
        } else if (prov.geography.is_delta || prov.geography.has_alluvial_fan) {
            prov.soil_type = SoilType::Alluvial;
        } else if (is_volcanic && prov.plate_age < 2.0f) {
            prov.soil_type = SoilType::Andisol;
        } else if (kz == KoppenZone::BWh || kz == KoppenZone::BWk) {
            prov.soil_type = SoilType::Aridisol;
        } else if (kz == KoppenZone::Af || kz == KoppenZone::Am) {
            if (prov.plate_age > 2.5f) {
                prov.soil_type = SoilType::Oxisol;  // old tropical surface
            } else {
                prov.soil_type = SoilType::Entisol;  // young tropical
            }
        } else if ((kz == KoppenZone::Aw || kz == KoppenZone::BSh) &&
                   (prov.geology_type == GeologyType::BasalticPlateau ||
                    prov.geology_type == GeologyType::AlluvialFill)) {
            prov.soil_type = SoilType::Vertisol;  // seasonal wet/dry clay
        } else if (kz == KoppenZone::Dfc || kz == KoppenZone::Dfd) {
            prov.soil_type = SoilType::Spodosol;  // boreal acidic
        } else if (prov.geography.groundwater_reserve > 0.60f &&
                   prov.geography.river_access > 0.50f &&
                   prov.geography.terrain_roughness < 0.20f) {
            prov.soil_type = SoilType::Histosol;  // waterlogged peat
        } else if ((kz == KoppenZone::Dfa || kz == KoppenZone::Dfb) &&
                   prov.geography.arable_land_fraction > 0.40f) {
            prov.soil_type = SoilType::Mollisol;  // temperate grassland black soils
        } else if (is_volcanic) {
            prov.soil_type = SoilType::Andisol;  // older volcanic
        } else if (prov.geology_type == GeologyType::AlluvialFill) {
            prov.soil_type = SoilType::Alluvial;
        } else {
            prov.soil_type = SoilType::Entisol;  // default young/undeveloped
        }

        // ---- Stage 5b: Soil fertility from geology_type ----
        // Volcanic and alluvial soils are most productive; ancient shields are leached and thin.
        float soil_multiplier;
        switch (prov.geology_type) {
            case GeologyType::VolcanicArc:
                // Young volcanic (andisols): highest natural fertility; great CEC, mineralogy.
                soil_multiplier = 1.10f + rng.next_float() * 0.20f;  // 1.10–1.30
                break;
            case GeologyType::AlluvialFill:
                // River/lake sediment (fluvisols/mollisols): excellent; deep, water-retaining.
                soil_multiplier = 1.15f + rng.next_float() * 0.20f;  // 1.15–1.35
                break;
            case GeologyType::BasalticPlateau:
                // Flood basalt weathering (vertisols): moderate-good fertility; shrink-swell clays.
                soil_multiplier = 1.00f + rng.next_float() * 0.15f;  // 1.00–1.15
                break;
            case GeologyType::CarbonateSequence:
                // Terra rossa / rendzinas on limestone: moderate; drought-prone in dry climates.
                soil_multiplier = 0.95f + rng.next_float() * 0.15f;  // 0.95–1.10
                break;
            case GeologyType::SedimentarySequence:
                // Layered sediments (cambisols/luvisols): moderate, varies with grain size.
                soil_multiplier = 0.90f + rng.next_float() * 0.15f;  // 0.90–1.05
                break;
            case GeologyType::MetamorphicCore:
                // Thin regolith on hard metamorphics (leptosols): poor; low depth, low water.
                soil_multiplier = 0.70f + rng.next_float() * 0.15f;  // 0.70–0.85
                break;
            case GeologyType::GraniteShield:
                // Ancient granites (acrisols/podzols): nutrient-leached, acidic; poor.
                soil_multiplier = 0.65f + rng.next_float() * 0.20f;  // 0.65–0.85
                break;
            case GeologyType::GreenstoneBelt:
                // Archean greenstone (ultramafics): variable; nickel-toxic in places, moderate avg.
                soil_multiplier = 0.75f + rng.next_float() * 0.20f;  // 0.75–0.95
                break;
        }

        // Climate modifier on soil: arid zones dry out soils; tropical zones leach nutrients.
        // Temperate humid zones are optimal.
        float climate_soil_mod = 1.0f;
        switch (prov.climate.koppen_zone) {
            case KoppenZone::Cfb:
            case KoppenZone::Cfa:
            case KoppenZone::Dfb:
            case KoppenZone::Dfa:
                climate_soil_mod = 1.05f;  // temperate humid = best for agriculture
                break;
            case KoppenZone::Am:
            case KoppenZone::Cfc:
            case KoppenZone::Dfc:
                climate_soil_mod = 0.95f;  // excess moisture leaches moderately
                break;
            case KoppenZone::Af:
                climate_soil_mod = 0.85f;  // tropical forest: leached oxisols despite lush cover
                break;
            case KoppenZone::Aw:
            case KoppenZone::BSh:
            case KoppenZone::BSk:
            case KoppenZone::Csa:
            case KoppenZone::Csb:
                climate_soil_mod = 0.90f;  // seasonal dryness limits nutrient cycling
                break;
            case KoppenZone::BWh:
            case KoppenZone::BWk:
                climate_soil_mod = 0.60f;  // desert: shallow, saline, minimal organic matter
                break;
            case KoppenZone::Dfd:
            case KoppenZone::ET:
                climate_soil_mod = 0.55f;  // very cold: slow decomposition, thin active layer
                break;
            case KoppenZone::EF:
                climate_soil_mod = 0.30f;  // ice cap / permanent snow: no agriculture
                break;
            default:
                climate_soil_mod = 1.0f;
                break;
        }

        // Apply soil fertility to agricultural_productivity (bounded by config).
        prov.agricultural_productivity =
            std::max(s.ag_min, std::min(s.ag_max,
                prov.agricultural_productivity * soil_multiplier * climate_soil_mod));

        // ---- Stage 6: Biomes — forest coverage from climate zone ----
        // Adjust forest_coverage toward the climate-expected value. Use a blend: 40% climate
        // expectation, 60% archetype baseline, so archetypes still dominate but geology/climate
        // add meaningful variation.
        float expected_forest;
        switch (prov.climate.koppen_zone) {
            case KoppenZone::Af:
            case KoppenZone::Am:
                expected_forest = 0.75f + rng.next_float() * 0.20f;  // tropical rainforest
                break;
            case KoppenZone::Aw:
                expected_forest = 0.30f + rng.next_float() * 0.20f;  // tropical savanna
                break;
            case KoppenZone::BWh:
            case KoppenZone::BWk:
                expected_forest = 0.02f + rng.next_float() * 0.05f;  // desert
                break;
            case KoppenZone::BSh:
            case KoppenZone::BSk:
                expected_forest = 0.05f + rng.next_float() * 0.10f;  // steppe
                break;
            case KoppenZone::Csa:
            case KoppenZone::Csb:
                expected_forest = 0.20f + rng.next_float() * 0.20f;  // Mediterranean scrub
                break;
            case KoppenZone::Cfa:
            case KoppenZone::Cfb:
            case KoppenZone::Cwa:
            case KoppenZone::Cfc:
                expected_forest = 0.35f + rng.next_float() * 0.25f;  // temperate broadleaf
                break;
            case KoppenZone::Dfa:
            case KoppenZone::Dfb:
            case KoppenZone::Dfc:
            case KoppenZone::Dfd:
                expected_forest = 0.45f + rng.next_float() * 0.30f;  // boreal/continental
                break;
            case KoppenZone::ET:
                expected_forest = 0.05f + rng.next_float() * 0.10f;  // tundra
                break;
            case KoppenZone::EF:
                expected_forest = 0.0f;  // ice cap
                break;
            default:
                expected_forest = 0.25f + rng.next_float() * 0.20f;
                break;
        }
        // Blend: archetype_blend% archetype, climate_blend% climate expectation.
        prov.geography.forest_coverage =
            std::max(0.0f, std::min(1.0f,
                prov.geography.forest_coverage * s.forest_archetype_blend +
                expected_forest                * s.forest_climate_blend));

        // ---- Stage 6: Refine drought/flood vulnerability from climate zone ----
        // Override only if the archetype set a generic value; clamp to reasonable range.
        float climate_drought;
        float climate_flood;
        switch (prov.climate.koppen_zone) {
            case KoppenZone::BWh:
                climate_drought = 0.75f + rng.next_float() * 0.20f;
                climate_flood   = 0.02f + rng.next_float() * 0.05f;
                break;
            case KoppenZone::BWk:
                climate_drought = 0.65f + rng.next_float() * 0.20f;
                climate_flood   = 0.02f + rng.next_float() * 0.05f;
                break;
            case KoppenZone::BSh:
            case KoppenZone::BSk:
                climate_drought = 0.40f + rng.next_float() * 0.25f;
                climate_flood   = 0.05f + rng.next_float() * 0.10f;
                break;
            case KoppenZone::Csa:
            case KoppenZone::Csb:
                climate_drought = 0.30f + rng.next_float() * 0.20f;
                climate_flood   = 0.08f + rng.next_float() * 0.10f;
                break;
            case KoppenZone::Af:
            case KoppenZone::Am:
                climate_drought = 0.05f + rng.next_float() * 0.08f;
                climate_flood   = 0.50f + rng.next_float() * 0.25f;
                break;
            case KoppenZone::Aw:
                climate_drought = 0.25f + rng.next_float() * 0.20f;
                climate_flood   = 0.20f + rng.next_float() * 0.20f;
                break;
            case KoppenZone::Dfa:
            case KoppenZone::Dfb:
                climate_drought = 0.15f + rng.next_float() * 0.15f;
                climate_flood   = 0.15f + rng.next_float() * 0.15f;
                break;
            case KoppenZone::Dfc:
            case KoppenZone::Dfd:
            case KoppenZone::ET:
                climate_drought = 0.08f + rng.next_float() * 0.10f;
                climate_flood   = 0.10f + rng.next_float() * 0.12f;
                break;
            case KoppenZone::EF:
                climate_drought = 0.02f;
                climate_flood   = 0.02f;
                break;
            default:
                climate_drought = prov.climate.drought_vulnerability;
                climate_flood   = prov.climate.flood_vulnerability;
                break;
        }
        // Blend archetype_vuln_blend : climate_vuln_blend with existing values.
        prov.climate.drought_vulnerability =
            std::max(0.0f, std::min(1.0f,
                prov.climate.drought_vulnerability * s.archetype_vuln_blend +
                climate_drought                    * s.climate_vuln_blend));
        prov.climate.flood_vulnerability =
            std::max(0.0f, std::min(1.0f,
                prov.climate.flood_vulnerability * s.archetype_vuln_blend +
                climate_flood                    * s.climate_vuln_blend));

        // ---- Stage 5c: Irrigation fields ----
        // Water availability composite.
        prov.water_availability =
            prov.geography.river_access          * 0.50f +
            prov.geography.groundwater_reserve   * 0.35f +
            prov.geography.spring_flow_index     * 0.15f;

        // Irrigation potential: maximum ag_productivity achievable with full irrigation.
        float temp_suit = std::max(0.0f, std::min(1.0f,
            (prov.climate.temperature_avg_c - 5.0f) / 25.0f));

        switch (prov.soil_type) {
            case SoilType::Aridisol:
                prov.irrigation_potential = 0.75f * temp_suit;
                break;
            case SoilType::Vertisol:
                prov.irrigation_potential = 0.85f * temp_suit;
                break;
            case SoilType::Alluvial:
                prov.irrigation_potential = 0.90f;
                break;
            case SoilType::Oxisol:
            case SoilType::Spodosol:
                prov.irrigation_potential = 0.30f;
                break;
            case SoilType::Cryosol:
                prov.irrigation_potential = 0.0f;
                break;
            default:
                prov.irrigation_potential = std::min(1.0f,
                    prov.agricultural_productivity * 1.15f);
                break;
        }

        // No water = no irrigation.
        if (prov.water_availability < 0.10f) {
            prov.irrigation_potential = 0.0f;
        }

        // Irrigation cost index: terrain × water lift × scarcity.
        float terrain_cost = 1.0f + prov.geography.terrain_roughness * 1.5f;
        float water_lift_cost = 1.0f;
        if (prov.geography.groundwater_reserve > 0.0f) {
            // Deeper water table = higher pump cost.
            float depth_proxy = 1.0f - prov.geography.groundwater_reserve;
            water_lift_cost = 1.0f + depth_proxy * 2.0f;
        }
        float scarcity = 1.0f + std::max(0.0f, 0.50f - prov.water_availability) * 4.0f;
        prov.irrigation_cost_index = std::max(0.5f, std::min(5.0f,
            terrain_cost * water_lift_cost * scarcity * 0.30f));

        // Salinisation risk: endorheic basins + arid climate + poor drainage.
        float salin = 0.0f;
        if (prov.geography.is_endorheic) salin += 0.30f;
        if (prov.soil_type == SoilType::Aridisol) salin += 0.25f;
        if (prov.soil_type == SoilType::Vertisol) salin += 0.10f;
        if (prov.climate.precipitation_mm < 400.0f) salin += 0.15f;
        if (prov.geography.terrain_roughness < 0.15f) salin += 0.10f;  // flat = poor drainage
        prov.salinisation_risk = std::min(1.0f, salin);
    }
}

// ===========================================================================
// Stage 7 — Special terrain features (complete WorldGen v0.18 pass)
// ===========================================================================

void WorldGenerator::detect_special_features(WorldState& world, DeterministicRNG& rng,
                                             const WorldGeneratorConfig& config) {
    const auto& t = config.terrain;
    for (auto& prov : world.provinces) {
        const float lat = prov.geography.latitude;
        const KoppenZone kz = prov.climate.koppen_zone;

        // ---- Permafrost ----
        bool permafrost_condition =
            (lat > t.permafrost_latitude_min) ||
            (kz == KoppenZone::ET) ||
            (kz == KoppenZone::EF);

        if (permafrost_condition) {
            prov.has_permafrost = true;

            // Lock CrudeOil and NaturalGas accessibility until arctic_drilling tech + thaw.
            for (auto& deposit : prov.deposits) {
                if (deposit.type == ResourceType::CrudeOil ||
                    deposit.type == ResourceType::NaturalGas) {
                    deposit.accessibility = 0.0f;
                }
            }

            // Permafrost severely limits agriculture (frozen ground, short growing season).
            prov.agricultural_productivity =
                std::max(t.permafrost_ag_floor,
                         prov.agricultural_productivity * t.permafrost_ag_factor);
        }

        // ---- Fjord ----
        bool fjord_condition =
            !prov.geography.is_landlocked &&
            (prov.geography.coastal_length_km > t.fjord_min_coastal_km) &&
            (prov.geography.terrain_roughness > t.fjord_min_roughness) &&
            (lat > t.fjord_min_latitude);

        if (fjord_condition) {
            prov.has_fjord = true;

            for (auto& link : prov.links) {
                if (link.type == LinkType::Maritime) {
                    link.transit_terrain_cost =
                        std::min(1.0f, link.transit_terrain_cost + t.fjord_maritime_cost_add);
                }
            }
        }

        // ---- Estuary ----
        // Tidal mixing zone where river meets sea; sheltered water; good port.
        bool estuary_condition =
            !prov.geography.is_landlocked &&
            (prov.geography.river_access > t.estuary_min_river_access) &&
            (prov.geography.coastal_length_km > t.estuary_min_coastal_km) &&
            (prov.geography.terrain_roughness < t.estuary_max_roughness) &&
            !prov.geography.is_delta &&  // deltas and estuaries are distinct
            !prov.has_fjord;

        if (estuary_condition) {
            prov.has_estuary = true;
            // Override port capacity with estuary values.
            float estuary_port = t.estuary_port_min +
                (prov.geography.river_access - t.estuary_min_river_access) *
                (t.estuary_port_max - t.estuary_port_min);
            prov.geography.port_capacity = std::max(prov.geography.port_capacity,
                                                     std::min(t.estuary_port_max, estuary_port));
        }

        // ---- Ria coast ----
        // Drowned river valleys (non-glacial); natural harbours; multiple inlets.
        bool ria_condition =
            !prov.geography.is_landlocked &&
            (prov.geography.coastal_length_km > t.ria_min_coastal_km) &&
            (prov.geography.terrain_roughness >= t.ria_min_roughness) &&
            (prov.geography.terrain_roughness <= t.ria_max_roughness) &&
            (lat < t.ria_max_latitude) &&  // below glacial line (glacial = fjord instead)
            !prov.has_fjord &&
            !prov.has_estuary;

        if (ria_condition) {
            prov.has_ria_coast = true;
            // Override port capacity with ria values (excellent natural harbours).
            float ria_port = t.ria_port_min +
                (prov.geography.terrain_roughness - t.ria_min_roughness) *
                (t.ria_port_max - t.ria_port_min) / (t.ria_max_roughness - t.ria_min_roughness);
            prov.geography.port_capacity = std::max(prov.geography.port_capacity,
                                                     std::min(t.ria_port_max, ria_port));
        }

        // ---- Fjord port override ----
        // Fjords have excellent deep-water natural harbours.
        if (prov.has_fjord) {
            float fjord_port = 0.85f + (prov.geography.terrain_roughness - t.fjord_min_roughness)
                               * 0.10f / (1.0f - t.fjord_min_roughness);
            prov.geography.port_capacity = std::max(prov.geography.port_capacity,
                                                     std::min(0.95f, fjord_port));
        }

        // ---- Badlands ----
        // Eroded soft sedimentary rock in arid climate; spectacular ravine networks.
        bool badlands_condition =
            (prov.rock_type == RockType::Sedimentary) &&
            (prov.geology_type == GeologyType::SedimentarySequence ||
             prov.geology_type == GeologyType::CarbonateSequence) &&
            (kz == KoppenZone::BWh || kz == KoppenZone::BWk ||
             kz == KoppenZone::BSh || kz == KoppenZone::BSk) &&
            (prov.geography.terrain_roughness > 0.55f) &&
            (prov.geography.elevation_avg_m < 2000.0f);

        if (badlands_condition) {
            prov.has_badlands = true;
            prov.geography.arable_land_fraction = 0.0f;
            prov.facility_concealment_bonus = std::max(prov.facility_concealment_bonus, 0.30f);
        }

        // ---- Impact crater ----
        // Probability based on tectonic stability and plate age.
        // Craton interiors preserve craters; young/active crust recycles them.
        // At V1 scale (6 provinces), ~1 in 6 provinces might have a significant crater.
        {
            float preservation = 0.0f;
            if (prov.tectonic_context == TectonicContext::CratonInterior) {
                preservation += 0.80f;  // stable shield preserves craters
            } else if (prov.tectonic_context == TectonicContext::SedimentaryBasin) {
                preservation += 0.40f;  // buried but detectable
            } else if (prov.tectonic_context == TectonicContext::PassiveMargin) {
                preservation += 0.30f;
            } else {
                preservation += 0.10f;  // active tectonics erase craters
            }

            // Old crust had more time to accumulate impacts.
            preservation *= std::min(1.0f, prov.plate_age / 3.0f);

            // Erosion reduces preservation (wet climates erase faster).
            float erosion_rate = prov.climate.precipitation_mm / 3000.0f;
            preservation *= (1.0f - erosion_rate * 0.50f);

            // Probability of a preserved crater existing.
            float crater_probability = preservation * 0.25f;  // base ~25% for perfect craton

            if (rng.next_float() < crater_probability) {
                prov.has_impact_crater = true;

                // Diameter: log-distributed (many small, few large).
                float size_roll = rng.next_float();
                if (size_roll < 0.60f) {
                    prov.impact_crater_diameter_km = 5.0f + rng.next_float() * 25.0f;  // 5-30 km
                } else if (size_roll < 0.90f) {
                    prov.impact_crater_diameter_km = 30.0f + rng.next_float() * 70.0f;  // 30-100 km
                } else {
                    prov.impact_crater_diameter_km = 100.0f + rng.next_float() * 200.0f;  // 100-300 km
                }

                // Mineral alteration from impact hydrothermal system.
                // Larger craters = more alteration; young = better preserved signal.
                float size_factor = std::min(1.0f, prov.impact_crater_diameter_km / 100.0f);
                prov.impact_mineral_signal = std::min(1.0f,
                    size_factor * preservation * (0.50f + rng.next_float() * 0.50f));
            }
        }

        // ---- Glacial history ----
        // Provinces at high latitude or high elevation with old crust were glaciated.
        {
            float abs_lat = std::abs(lat);

            // Glaciation line: latitude-dependent (lower at higher latitudes).
            // Continental ice sheets reached ~40°N in Pleistocene (Great Lakes, Scandinavia).
            bool was_glaciated = false;
            if (abs_lat > 55.0f) {
                was_glaciated = true;  // always glaciated at polar latitudes
            } else if (abs_lat > 40.0f && prov.geography.elevation_avg_m > 500.0f) {
                was_glaciated = true;  // Pleistocene ice sheets
            } else if (prov.geography.elevation_avg_m > 2500.0f && abs_lat > 25.0f) {
                was_glaciated = true;  // mountain glaciation
            }

            // Glacial scour: flat craton at high latitude → Canadian Shield / Scandinavia.
            if (was_glaciated &&
                (prov.tectonic_context == TectonicContext::CratonInterior ||
                 prov.tectonic_context == TectonicContext::SedimentaryBasin) &&
                prov.geography.terrain_roughness < 0.35f) {
                prov.is_glacial_scoured = true;
                // Thin soils from ice scour.
                prov.agricultural_productivity = std::min(prov.agricultural_productivity, 0.25f);
                // Many lakes (fishing, freshwater).
                prov.geography.river_access = std::min(1.0f,
                    prov.geography.river_access + 0.15f);
            }

            // Loess: windblown silt deposited downwind of glaciated regions.
            // Loess accumulates at 30-55° latitude, on flat terrain adjacent to
            // glaciated regions. It creates incredibly fertile soil (Midwest, Pampas,
            // Chinese Loess Plateau, Ukraine Chernozem).
            bool loess_condition =
                !was_glaciated &&  // loess deposits DOWNWIND, not under the ice
                abs_lat > 30.0f && abs_lat < 55.0f &&
                prov.geography.terrain_roughness < 0.30f &&
                (prov.climate.precipitation_mm > 400.0f &&
                 prov.climate.precipitation_mm < 1200.0f);  // not too wet (would wash away)

            // Check if any neighbor was glaciated (loess source).
            if (loess_condition) {
                bool has_glacial_neighbor = false;
                for (const auto& link : prov.links) {
                    auto it = world.h3_province_map.find(link.neighbor_h3);
                    if (it == world.h3_province_map.end()) continue;
                    const auto& nb = world.provinces[it->second];
                    float nb_abs_lat = std::abs(nb.geography.latitude);
                    if (nb_abs_lat > 55.0f ||
                        (nb_abs_lat > 40.0f && nb.geography.elevation_avg_m > 500.0f) ||
                        nb.is_glacial_scoured) {
                        has_glacial_neighbor = true;
                        break;
                    }
                }
                // Also allow loess if in continental interior at right latitude
                // (loess can travel hundreds of km on wind).
                if (has_glacial_neighbor || (prov.climate.continentality > 0.40f && abs_lat > 38.0f)) {
                    prov.has_loess = true;
                    // Loess is extremely fertile — ag_productivity bonus.
                    prov.agricultural_productivity = std::min(1.0f,
                        prov.agricultural_productivity + 0.15f);
                }
            }
        }

        // ---- Atoll ----
        // Subsided HotSpot volcanic island with coral: tropical, low elevation, small.
        bool atoll_condition =
            prov.island_isolation &&
            (prov.tectonic_context == TectonicContext::HotSpot ||
             prov.tectonic_context == TectonicContext::PassiveMargin) &&
            prov.geography.elevation_avg_m < 50.0f &&
            std::abs(lat) < 30.0f &&  // coral grows in warm water
            !prov.geography.is_landlocked;

        if (atoll_condition) {
            prov.is_atoll = true;
            prov.agricultural_productivity = 0.0f;  // no arable land
            prov.infrastructure_rating = std::min(prov.infrastructure_rating, 0.15f);
            // Lagoon provides moderate natural harbour.
            prov.geography.port_capacity = std::max(prov.geography.port_capacity, 0.45f);
        }

        // ---- Permafrost precludes karst ----
        if (prov.has_permafrost) {
            prov.has_karst = false;
        }

        // ---- Karst concealment bonus ----
        if (prov.has_karst) {
            prov.facility_concealment_bonus = std::min(1.0f,
                prov.facility_concealment_bonus + 0.25f);
        }

        // ---- Salt flat ----
        // Endorheic basin in arid climate: evaporite surface (Bonneville, Salar de Uyuni).
        if (prov.geography.is_endorheic &&
            (kz == KoppenZone::BWh || kz == KoppenZone::BWk ||
             kz == KoppenZone::BSh || kz == KoppenZone::BSk)) {
            prov.is_salt_flat = true;
        }

        // ---- Fisheries ----
        // Schaefer surplus production model seeding.
        {
            auto& f = prov.fisheries;

            if (prov.geography.coastal_length_km < 1.0f &&
                prov.geography.river_access < 0.15f) {
                f.access_type = FishingAccessType::NoAccess;
            } else if (prov.geography.coastal_length_km < 1.0f) {
                // Inland freshwater fisheries.
                f.access_type = FishingAccessType::Freshwater;
                f.carrying_capacity = prov.geography.river_access * 0.20f;
                // Glacial-scoured provinces: thousands of lakes boost freshwater fisheries.
                if (prov.is_glacial_scoured) {
                    f.carrying_capacity = std::min(1.0f, f.carrying_capacity + 0.15f);
                }
            } else if (prov.climate.cold_current_adjacent) {
                // Upwelling: highest yield (Humboldt, Benguela, Canary currents).
                f.access_type = FishingAccessType::Upwelling;
                f.carrying_capacity = 0.70f + rng.next_float() * 0.15f;
            } else if (prov.is_atoll || prov.geography.port_capacity > 0.70f) {
                f.access_type = FishingAccessType::Inshore;
                f.carrying_capacity = 0.45f + rng.next_float() * 0.20f;
            } else if (prov.geography.coastal_length_km > 200.0f) {
                f.access_type = FishingAccessType::Offshore;
                f.carrying_capacity = 0.30f + rng.next_float() * 0.15f;
            } else {
                // Moderate coast — default inshore.
                f.access_type = FishingAccessType::Inshore;
                f.carrying_capacity = 0.25f + rng.next_float() * 0.20f;
            }

            if (f.access_type != FishingAccessType::NoAccess) {
                // Growth rate by access type.
                switch (f.access_type) {
                    case FishingAccessType::Upwelling:   f.intrinsic_growth_rate = 0.60f; break;
                    case FishingAccessType::Inshore:     f.intrinsic_growth_rate = 0.40f; break;
                    case FishingAccessType::Offshore:    f.intrinsic_growth_rate = 0.25f; break;
                    case FishingAccessType::Pelagic:     f.intrinsic_growth_rate = 0.55f; break;
                    case FishingAccessType::Freshwater:  f.intrinsic_growth_rate = 0.35f; break;
                    default: break;
                }

                // MSY = 0.5 * r * K (Schaefer model).
                f.max_sustainable_yield = 0.5f * f.intrinsic_growth_rate * f.carrying_capacity;

                // Seasonal closure from ice and spawning.
                if (kz == KoppenZone::ET || kz == KoppenZone::EF ||
                    prov.geography.elevation_avg_m > 3000.0f) {
                    f.seasonal_closure = 0.50f;
                } else if (kz == KoppenZone::Dfc || kz == KoppenZone::Dfd) {
                    f.seasonal_closure = 0.25f;
                } else {
                    f.seasonal_closure = 0.05f;
                }

                f.current_stock = f.carrying_capacity * 0.85f;
                f.is_migratory = (f.access_type == FishingAccessType::Pelagic ||
                                  f.access_type == FishingAccessType::Offshore);
            }
        }
    }
}

// ===========================================================================
// Stage 9 — Population and Infrastructure Seeding (WorldGen v0.18 §9)
// ===========================================================================

void WorldGenerator::seed_population_attractiveness(WorldState& world, DeterministicRNG& rng,
                                                    const WorldGeneratorConfig& config) {
    const auto& p = config.population;

    for (auto& prov : world.provinces) {
        // -----------------------------------------------------------------
        // §9.2 — Disease burden from climate zone + standing water + elevation.
        // -----------------------------------------------------------------
        float burden = 0.0f;
        KoppenZone kz = prov.climate.koppen_zone;

        if (kz == KoppenZone::Af || kz == KoppenZone::Am) {
            burden += 0.55f;  // hyperendemic tropical rainforest
        } else if (kz == KoppenZone::Aw || kz == KoppenZone::BSh) {
            burden += 0.35f;  // tropical savanna/steppe; seasonal
        } else if (kz == KoppenZone::Cfa || kz == KoppenZone::Cwa) {
            burden += 0.15f;  // humid subtropical
        }

        // Standing water amplifies vector habitat.
        if (prov.climate.flood_vulnerability > 0.6f) {
            burden += 0.15f;
        }

        // Elevation reduces disease (malaria vector altitude limit ~2,000m).
        if (prov.geography.elevation_avg_m > 2000.0f) {
            burden *= 0.10f;
        } else if (prov.geography.elevation_avg_m > 1500.0f) {
            burden *= 0.35f;
        } else if (prov.geography.elevation_avg_m > 1000.0f) {
            burden *= 0.65f;
        }

        prov.disease_burden = std::min(1.0f, std::max(0.0f, burden));

        // -----------------------------------------------------------------
        // §9.1 — Settlement attractiveness formula.
        // -----------------------------------------------------------------

        // Check for geothermal deposit.
        bool has_geothermal = false;
        for (const auto& d : prov.deposits) {
            if (d.type == ResourceType::Geothermal) {
                has_geothermal = true;
                break;
            }
        }

        float base =
            prov.agricultural_productivity                * p.w_ag_productivity
          + prov.geography.port_capacity                  * p.w_port_capacity
          + prov.geography.river_access                   * p.w_river_access
          + (1.0f - prov.geography.terrain_roughness)     * p.w_terrain_flatness
          + (prov.soil_type == SoilType::Alluvial ? 1.0f : 0.0f) * p.w_alluvial_soil
          + (has_geothermal ? 1.0f : 0.0f)                * p.w_geothermal
          + (prov.soil_type == SoilType::Andisol ? 1.0f : 0.0f) * p.w_volcanic_soil;

        // Altitude ceiling — hard physiological limits.
        if (prov.geography.elevation_avg_m > 4500.0f) {
            base *= p.alt_4500m_mult;
        } else if (prov.geography.elevation_avg_m > 3500.0f) {
            base *= p.alt_3500m_mult;
        } else if (prov.geography.elevation_avg_m > 2500.0f) {
            base *= p.alt_2500m_mult;
        } else if (prov.geography.elevation_avg_m > 1500.0f) {
            base *= p.alt_1500m_mult;
        }

        // Disease burden penalty (§9.2).
        base *= (1.0f - prov.disease_burden * p.disease_max_penalty);

        // Environmental penalties — genuine uninhabitability.
        if (kz == KoppenZone::BWh || kz == KoppenZone::BWk) {
            base *= p.desert_mult;
        }
        if (kz == KoppenZone::EF) {
            base *= p.ice_cap_mult;
        }
        if (kz == KoppenZone::ET) {
            base *= p.tundra_mult;
        }
        if (prov.geography.terrain_roughness > 0.85f) {
            base *= p.extreme_terrain_mult;
        }
        if (prov.island_isolation) {
            base *= p.island_isolation_mult;
        }
        if (prov.has_permafrost) {
            base *= p.continuous_permafrost_mult;
        }

        float score = std::max(0.0f, std::min(1.0f, base));
        prov.settlement_attractiveness = score;

        // -----------------------------------------------------------------
        // §9.3 — Infrastructure derivation from attractiveness.
        // Flood vulnerability reduces infrastructure (costlier construction)
        // without reducing population attractiveness.
        // -----------------------------------------------------------------
        float infra = score * p.infra_attract_scale
                    - prov.climate.flood_vulnerability * p.infra_flood_penalty
                    + (rng.next_float() - 0.5f) * 2.0f * p.infra_variance_sigma;
        infra = std::max(0.0f, std::min(1.0f, infra));

        // Blend with archetype infrastructure (50/50) to maintain diversity.
        prov.infrastructure_rating = prov.infrastructure_rating * 0.50f + infra * 0.50f;

        // -----------------------------------------------------------------
        // §9.4 — Population adjustment from attractiveness.
        // -----------------------------------------------------------------
        float multiplier = p.multiplier_base + score * p.multiplier_range;
        multiplier *= (p.rng_variation_base + rng.next_float() * p.rng_variation_range);

        uint32_t new_pop = static_cast<uint32_t>(
            static_cast<float>(prov.demographics.total_population) * multiplier + 0.5f);
        prov.demographics.total_population = std::max(p.population_floor, new_pop);
    }
}

// ===========================================================================
// Stage 9.5 — Nation formation (WorldGen v0.18 §9.5)
// ===========================================================================

// Helper: compute terrain resistance for crossing a province link.
// Mountains, rivers, and maritime crossings raise resistance; open flatland is cheap.
// Uses NationFormationParams for all thresholds so the algorithm is tunable.
static float compute_terrain_resistance(
        const ProvinceLink& link, const Province& neighbor,
        const WorldGeneratorConfig::NationFormationParams& nfp) {
    float resistance = 1.0f;

    // Maritime crossing: ocean is slower than land conquest
    if (link.type == LinkType::Maritime) {
        resistance += nfp.maritime_resistance;
    }

    // Elevation change proxy: high transit_terrain_cost means steep terrain
    if (link.transit_terrain_cost > nfp.steep_terrain_threshold) {
        resistance *= (1.0f + link.transit_terrain_cost);
    }

    // River crossing barrier (river links represent major navigable rivers)
    if (link.type == LinkType::River) {
        resistance *= nfp.river_crossing_mult;
    }

    // Uninhabitable provinces are expensive to cross (deserts, ice caps)
    if (neighbor.settlement_attractiveness < 0.05f) {
        resistance *= nfp.uninhabitable_mult;
    }

    return resistance;
}

// Helper: classify nation size from province count.
static NationSize classify_nation_size(size_t province_count) {
    if (province_count <= 3) return NationSize::Microstate;
    if (province_count <= 12) return NationSize::Small;
    if (province_count <= 40) return NationSize::Medium;
    if (province_count <= 120) return NationSize::Large;
    return NationSize::Continental;
}

// Helper: NationSize to string for JSON serialization.
static const char* nation_size_str(NationSize s) {
    switch (s) {
        case NationSize::Microstate:   return "microstate";
        case NationSize::Small:        return "small";
        case NationSize::Medium:       return "medium";
        case NationSize::Large:        return "large";
        case NationSize::Continental:  return "continental";
    }
    return "small";
}

// Language families for V1 world generation. On non-Earth worlds all families
// get equal geographic affinity weight — the list provides naming diversity.
static const char* v1_language_families[] = {
    "germanic", "romance", "slavic", "sinitic", "arabic",
    "turkic",   "indic",   "bantu",  "austronesian", "quechuan",
};
static constexpr size_t v1_language_family_count = 10;

void WorldGenerator::form_nations(WorldState& world, DeterministicRNG& rng,
                                  const WorldGeneratorConfig& config) {
    const auto& provinces = world.provinces;
    const uint32_t prov_count = static_cast<uint32_t>(provinces.size());
    if (prov_count == 0) return;

    const auto& nfp = config.nation_formation;

    // -----------------------------------------------------------------------
    // Build h3_index → province_id lookup (O(1) neighbor resolution).
    // Built FIRST so all subsequent passes use O(1) lookups instead of O(n) scans.
    // -----------------------------------------------------------------------
    std::unordered_map<H3Index, uint32_t> h3_to_idx;
    h3_to_idx.reserve(prov_count);
    for (uint32_t i = 0; i < prov_count; ++i) {
        h3_to_idx[provinces[i].h3_index] = i;
    }

    // -----------------------------------------------------------------------
    // §9.5.1 — Nation Seed Placement
    // -----------------------------------------------------------------------
    // Collect habitable provinces: attractiveness > 0.10, not EF ice cap.
    // Provinces below uninhabitable_threshold (0.02) are excluded from nation
    // assignment entirely — they become unclaimed territory per spec.
    std::vector<uint32_t> habitable;
    habitable.reserve(prov_count);
    for (uint32_t i = 0; i < prov_count; ++i) {
        const auto& p = provinces[i];
        if (p.settlement_attractiveness > 0.10f &&
            p.climate.koppen_zone != KoppenZone::EF) {
            habitable.push_back(i);
        }
    }
    if (habitable.empty()) {
        // Degenerate world: all provinces are habitable for nation assignment.
        for (uint32_t i = 0; i < prov_count; ++i) habitable.push_back(i);
    }

    // Target nation count: sqrt(habitable) × scale, clamped [min, max].
    // For small worlds (habitable < min²/scale²), we gracefully reduce below
    // the spec minimum to avoid more nations than provinces. The spec [20,400]
    // targets Earth-scale (1000+ provinces); V1 with 6 provinces gets ~4.
    uint32_t raw_target = static_cast<uint32_t>(
        std::sqrt(static_cast<float>(habitable.size())) * nfp.seed_count_scale);
    uint32_t target_count = std::clamp(raw_target, nfp.seed_count_min, nfp.seed_count_max);
    // Never more nations than habitable provinces; minimum 2 for geopolitical tension.
    target_count = std::min(target_count, static_cast<uint32_t>(habitable.size()));
    target_count = std::max(target_count, std::min(2u, static_cast<uint32_t>(habitable.size())));

    // Build attractiveness² weights for seed selection bias.
    // Attractiveness² biases toward high-value inland/coastal cores, not thin margins.
    std::vector<float> weights(habitable.size());
    float total_weight = 0.0f;
    for (size_t i = 0; i < habitable.size(); ++i) {
        float a = provinces[habitable[i]].settlement_attractiveness;
        weights[i] = a * a;
        total_weight += weights[i];
    }

    // Track which provinces are available for seed selection.
    // Using a bool vector is O(1) per check vs O(log n) for unordered_set.
    std::vector<bool> available(prov_count, false);
    for (uint32_t pid : habitable) available[pid] = true;

    std::vector<uint32_t> seed_province_ids;
    seed_province_ids.reserve(target_count);

    // Minimum separation in graph hops (BFS distance through ProvinceLinks).
    // Spec §9.5.1: "no two seeds closer than 3 H3 grid-disks."
    // For small worlds (< seed_separation * 2 provinces), reduce to avoid
    // excluding all candidates.
    uint32_t effective_separation = nfp.seed_separation;
    if (habitable.size() < effective_separation * 2) {
        effective_separation = 0;  // small worlds: no separation constraint
    }

    // Weighted sampling loop. On each iteration we pick one seed, then
    // exclude neighbors within effective_separation via BFS.
    // Instead of recomputing total_weight from scratch each time (O(n)),
    // we subtract removed weights incrementally.
    uint32_t max_attempts = static_cast<uint32_t>(habitable.size()) * 3;
    for (uint32_t attempt = 0;
         seed_province_ids.size() < target_count && attempt < max_attempts;
         ++attempt) {
        if (total_weight <= 0.0f) break;

        // Weighted random selection.
        float roll = rng.next_float() * total_weight;
        float cumulative = 0.0f;
        uint32_t chosen = habitable[0];
        size_t chosen_idx = 0;
        for (size_t i = 0; i < habitable.size(); ++i) {
            if (!available[habitable[i]]) continue;
            cumulative += weights[i];
            if (cumulative >= roll) {
                chosen = habitable[i];
                chosen_idx = i;
                break;
            }
        }

        if (!available[chosen]) continue;

        seed_province_ids.push_back(chosen);
        available[chosen] = false;
        total_weight -= weights[chosen_idx];
        weights[chosen_idx] = 0.0f;

        // BFS exclusion zone: mark neighbors within effective_separation as unavailable.
        if (effective_separation > 0) {
            std::queue<std::pair<uint32_t, uint32_t>> bfs;
            bfs.push({chosen, 0});
            std::unordered_set<uint32_t> visited_bfs;
            visited_bfs.insert(chosen);
            while (!bfs.empty()) {
                auto [cur, dist] = bfs.front();
                bfs.pop();
                if (dist >= effective_separation) continue;
                for (const auto& link : provinces[cur].links) {
                    auto it = h3_to_idx.find(link.neighbor_h3);
                    if (it == h3_to_idx.end()) continue;
                    uint32_t nid = it->second;
                    if (visited_bfs.count(nid)) continue;
                    visited_bfs.insert(nid);
                    if (available[nid]) {
                        available[nid] = false;
                        // Find and zero this province's weight to keep total_weight accurate.
                        for (size_t i = 0; i < habitable.size(); ++i) {
                            if (habitable[i] == nid) {
                                total_weight -= weights[i];
                                weights[i] = 0.0f;
                                break;
                            }
                        }
                    }
                    bfs.push({nid, dist + 1});
                }
            }
        }
    }

    // Fallback: if no seeds were placed, use the highest-attractiveness province.
    if (seed_province_ids.empty()) {
        uint32_t best = 0;
        for (uint32_t i = 1; i < prov_count; ++i) {
            if (provinces[i].settlement_attractiveness >
                provinces[best].settlement_attractiveness) {
                best = i;
            }
        }
        seed_province_ids.push_back(best);
    }

    const uint32_t nation_count = static_cast<uint32_t>(seed_province_ids.size());

    // -----------------------------------------------------------------------
    // §9.5.2 — Voronoi Growth with Terrain Resistance
    // -----------------------------------------------------------------------
    // Identify uninhabitable provinces that should remain unclaimed.
    std::vector<bool> uninhabitable(prov_count, false);
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (provinces[i].settlement_attractiveness < nfp.uninhabitable_threshold &&
            provinces[i].climate.koppen_zone == KoppenZone::EF) {
            uninhabitable[i] = true;
        }
        // Deep desert with near-zero attractiveness also unclaimed.
        if (provinces[i].settlement_attractiveness < nfp.uninhabitable_threshold &&
            (provinces[i].climate.koppen_zone == KoppenZone::BWh ||
             provinces[i].climate.koppen_zone == KoppenZone::BWk)) {
            uninhabitable[i] = true;
        }
    }

    // Identify island provinces (all links are Maritime) for special post-pass assignment.
    std::vector<bool> is_island(prov_count, false);
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (provinces[i].links.empty()) continue;
        bool all_maritime = true;
        for (const auto& link : provinces[i].links) {
            if (link.type != LinkType::Maritime) {
                all_maritime = false;
                break;
            }
        }
        if (all_maritime) is_island[i] = true;
    }

    // Dijkstra-style priority queue: (cost, province_id, nation_index).
    using PQEntry = std::tuple<float, uint32_t, uint32_t>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

    std::vector<float> best_cost(prov_count, std::numeric_limits<float>::max());
    std::vector<int32_t> assignment(prov_count, -1);  // -1 = unassigned

    // Initialize seeds. Uninhabitable seeds shouldn't occur (filtered in habitable),
    // but guard anyway.
    for (uint32_t ni = 0; ni < nation_count; ++ni) {
        uint32_t pid = seed_province_ids[ni];
        best_cost[pid] = 0.0f;
        assignment[pid] = static_cast<int32_t>(ni);
        pq.push({0.0f, pid, ni});
    }

    // Main Voronoi flood-fill. Skip uninhabitable and island provinces (islands
    // get special assignment in the post-pass below).
    while (!pq.empty()) {
        auto [cost, current, nation_idx] = pq.top();
        pq.pop();

        if (cost > best_cost[current]) continue;

        for (const auto& link : provinces[current].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end()) continue;
            uint32_t neighbor_id = it->second;

            // Skip uninhabitable provinces — they remain unclaimed territory.
            if (uninhabitable[neighbor_id]) continue;

            // Skip islands during main pass — they get post-pass assignment.
            if (is_island[neighbor_id]) continue;

            if (assignment[neighbor_id] >= 0 &&
                best_cost[neighbor_id] <= cost) continue;

            float resistance = compute_terrain_resistance(link, provinces[neighbor_id], nfp);
            float new_cost = cost + resistance;

            if (new_cost < best_cost[neighbor_id]) {
                best_cost[neighbor_id] = new_cost;
                assignment[neighbor_id] = static_cast<int32_t>(nation_idx);
                pq.push({new_cost, neighbor_id, nation_idx});
            }
        }
    }

    // §9.5.2 post-pass: assign island provinces to the nation that owns the
    // maritime-adjacent province with the longest shared border.
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (!is_island[i]) continue;
        if (uninhabitable[i]) continue;

        int32_t best_nation = -1;
        float best_border_km = -1.0f;
        for (const auto& link : provinces[i].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end()) continue;
            uint32_t nid = it->second;
            if (assignment[nid] >= 0 && link.shared_border_km > best_border_km) {
                best_border_km = link.shared_border_km;
                best_nation = assignment[nid];
            }
        }
        // If no assigned neighbor found (all neighbors are also islands),
        // try any assigned neighbor via BFS at distance 2.
        if (best_nation < 0) {
            for (const auto& link : provinces[i].links) {
                auto it = h3_to_idx.find(link.neighbor_h3);
                if (it == h3_to_idx.end()) continue;
                uint32_t nid = it->second;
                for (const auto& link2 : provinces[nid].links) {
                    auto it2 = h3_to_idx.find(link2.neighbor_h3);
                    if (it2 == h3_to_idx.end()) continue;
                    if (assignment[it2->second] >= 0) {
                        best_nation = assignment[it2->second];
                        break;
                    }
                }
                if (best_nation >= 0) break;
            }
        }
        if (best_nation >= 0) {
            assignment[i] = best_nation;
        }
        // else: truly isolated island — remains unclaimed (rare).
    }

    // Provinces that remain unassigned (no links, not uninhabitable) → first nation.
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (assignment[i] < 0 && !uninhabitable[i]) {
            assignment[i] = 0;
        }
    }

    // -----------------------------------------------------------------------
    // Build Nation objects
    // -----------------------------------------------------------------------
    world.nations.clear();
    world.region_groups.clear();

    // Scalable name generation: combine prefix + root, cycling through roots.
    // 8 prefixes × 60 roots = 480 unique names, sufficient for max 400 nations.
    static const char* nation_prefixes[] = {
        "Republic of ", "Kingdom of ", "Federation of ", "Commonwealth of ",
        "State of ", "Dominion of ", "Principality of ", "Union of ",
    };
    static constexpr size_t n_prefixes = 8;
    static const char* nation_roots[] = {
        "Avalon",   "Corvana",  "Delvoria", "Estmarch",  "Fenwick",
        "Galdria",  "Haldane",  "Irindel",  "Jastova",   "Keldara",
        "Lorantia", "Morvaine", "Narthia",  "Ostenveld",  "Pelluria",
        "Quentara", "Rhedania", "Sylvarna", "Torvalis",   "Ulmendia",
        "Veldmark", "Wyverna",  "Xandria",  "Yelthar",   "Zaranthia",
        "Almuria",  "Brestova", "Calendra", "Damavand",  "Eldrathi",
        "Frostmark","Gelvaine", "Halvoria", "Istharan",  "Jorvald",
        "Korinth",  "Lynthia",  "Marvesta", "Novogard",  "Orelund",
        "Praethos", "Quelaria", "Rochfort", "Silvaine",  "Thalmund",
        "Umbravia", "Valdanis", "Westmark", "Xenthira",  "Yrvandia",
        "Zephyria", "Arcandia", "Belmoris", "Calderon",  "Dravenholm",
        "Elysar",   "Fyrnhold", "Grenmark", "Helmgaard", "Isolvar",
    };
    static constexpr size_t n_roots = 60;

    for (uint32_t ni = 0; ni < nation_count; ++ni) {
        Nation nation{};
        nation.id = ni;

        // Deterministic name from RNG + cycling through root/prefix pools.
        uint32_t prefix_idx = static_cast<uint32_t>(rng.next_float() * n_prefixes) % n_prefixes;
        uint32_t root_idx = ni % n_roots;
        nation.name = std::string(nation_prefixes[prefix_idx]) + nation_roots[root_idx];
        // Currency code: first 3 chars of root, uppercased.
        std::string root_str(nation_roots[root_idx]);
        nation.currency_code = root_str.substr(0, 3);
        for (auto& c : nation.currency_code) c = static_cast<char>(std::toupper(c));

        // Government type weighted by RNG roll.
        float gov_roll = rng.next_float();
        if (gov_roll < 0.50f) nation.government_type = GovernmentType::Democracy;
        else if (gov_roll < 0.75f) nation.government_type = GovernmentType::Federation;
        else if (gov_roll < 0.95f) nation.government_type = GovernmentType::Autocracy;
        else nation.government_type = GovernmentType::FailedState;

        nation.political_cycle = {0, 0.50f + rng.next_float() * 0.20f, false,
                                   365 * (3 + static_cast<uint32_t>(rng.next_float() * 3))};
        nation.corporate_tax_rate = 0.15f + rng.next_float() * 0.20f;
        nation.income_tax_rate_top_bracket = 0.20f + rng.next_float() * 0.30f;
        nation.trade_balance_fraction = 0.0f;
        nation.inflation_rate = 0.01f + rng.next_float() * 0.05f;
        nation.credit_rating = 0.40f + rng.next_float() * 0.50f;
        nation.tariff_schedule = nullptr;

        // First nation (player's home) is LOD 0; others are LOD 1.
        if (ni == 0) {
            nation.lod1_profile = std::nullopt;
        } else {
            Lod1NationProfile lod1{};
            lod1.export_margin = 0.10f + rng.next_float() * 0.15f;
            lod1.import_premium = 0.05f + rng.next_float() * 0.10f;
            lod1.trade_openness = 0.30f + rng.next_float() * 0.40f;
            lod1.tech_tier_modifier = 0.80f + rng.next_float() * 0.40f;
            lod1.population_modifier = 0.70f + rng.next_float() * 0.60f;
            lod1.research_investment = 0.0f;
            lod1.current_tier = 1;
            nation.lod1_profile = lod1;
        }

        // Collect member provinces.
        for (uint32_t pi = 0; pi < prov_count; ++pi) {
            if (assignment[pi] == static_cast<int32_t>(ni)) {
                nation.province_ids.push_back(pi);
            }
        }

        nation.size_class = classify_nation_size(nation.province_ids.size());

        world.nations.push_back(std::move(nation));
    }

    // Assign nation_id on each province. Unclaimed provinces get a sentinel
    // value equal to nation_count (no valid nation). For V1 runtime compat,
    // unclaimed provinces still get nation_id = 0 (player's nation handles them).
    for (uint32_t i = 0; i < prov_count; ++i) {
        if (assignment[i] >= 0) {
            world.provinces[i].nation_id = static_cast<uint32_t>(assignment[i]);
        } else {
            // Unclaimed territory: assign to nation 0 for V1 runtime compat.
            // Stage 10 can read uninhabitable[] to detect these.
            world.provinces[i].nation_id = 0;
        }
    }

    // Create one region per province (V1 simplification).
    for (uint32_t r = 0; r < prov_count; ++r) {
        Region region{};
        region.id = r;
        region.fictional_name = "Region_" + std::to_string(r);
        region.nation_id = world.provinces[r].nation_id;
        region.province_ids.push_back(r);
        world.region_groups.push_back(std::move(region));
    }

    // -----------------------------------------------------------------------
    // §9.5.3 — Language Family Assignment
    // -----------------------------------------------------------------------
    // Two-pass: initial geographic assignment, then neighbor propagation.

    // Pass 1: each nation's seed province gets a language family by geographic affinity.
    for (auto& nation : world.nations) {
        if (nation.province_ids.empty()) continue;
        // Use seed province (first province_id is the seed from Voronoi growth).
        uint32_t seed_pid = seed_province_ids[std::min(
            nation.id, static_cast<uint32_t>(seed_province_ids.size() - 1))];
        const auto& seed_prov = provinces[seed_pid];
        float abs_lat = std::fabs(seed_prov.geography.latitude);

        // Build affinity weights for each language family.
        float family_weights[v1_language_family_count];
        for (size_t fi = 0; fi < v1_language_family_count; ++fi) {
            family_weights[fi] = 1.0f;  // neutral default
        }
        // Germanic: mid-high latitude, wetter regions
        if (abs_lat > 45.0f && abs_lat < 65.0f) family_weights[0] = 1.5f;
        // Romance: mid latitude
        if (abs_lat > 25.0f && abs_lat < 50.0f) family_weights[1] = 1.4f;
        // Slavic: continental mid-high latitude
        if (abs_lat > 40.0f && abs_lat < 65.0f) family_weights[2] = 1.3f;
        // Sinitic: mid latitude East Asian analog
        if (abs_lat > 20.0f && abs_lat < 45.0f) family_weights[3] = 1.3f;
        // Arabic: low-mid latitude, arid zones
        if (abs_lat < 35.0f) family_weights[4] = 1.4f;
        // Turkic: continental steppe
        if (abs_lat > 30.0f && abs_lat < 55.0f) family_weights[5] = 1.2f;
        // Indic: tropical-subtropical
        if (abs_lat < 30.0f) family_weights[6] = 1.3f;
        // Bantu: tropical Africa analog
        if (abs_lat < 25.0f) family_weights[7] = 1.4f;
        // Austronesian: coastal tropical (island/maritime cultures)
        if (abs_lat < 20.0f && seed_prov.geography.coastal_length_km > 20.0f)
            family_weights[8] = 1.5f;
        // Quechuan: highland
        if (seed_prov.geography.elevation_avg_m > 2000.0f) family_weights[9] = 1.5f;

        float total = 0.0f;
        for (size_t fi = 0; fi < v1_language_family_count; ++fi) total += family_weights[fi];
        float roll = rng.next_float() * total;
        float cum = 0.0f;
        size_t chosen_fi = 0;
        for (size_t fi = 0; fi < v1_language_family_count; ++fi) {
            cum += family_weights[fi];
            if (cum >= roll) {
                chosen_fi = fi;
                break;
            }
        }
        nation.language_family_id = v1_language_families[chosen_fi];
    }

    // Pass 2: neighbor propagation — chance to inherit largest neighbor's language.
    // Process largest nations first (they set regional tone).
    std::vector<uint32_t> nation_order(nation_count);
    for (uint32_t ni = 0; ni < nation_count; ++ni) nation_order[ni] = ni;
    std::sort(nation_order.begin(), nation_order.end(),
              [&](uint32_t a, uint32_t b) {
                  return world.nations[a].province_ids.size() >
                         world.nations[b].province_ids.size();
              });

    // Build nation adjacency from province links.
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> nation_adj;
    for (uint32_t pi = 0; pi < prov_count; ++pi) {
        if (assignment[pi] < 0) continue;  // skip unclaimed
        for (const auto& link : provinces[pi].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end()) continue;
            if (assignment[it->second] < 0) continue;  // skip unclaimed neighbor
            uint32_t ni_a = world.provinces[pi].nation_id;
            uint32_t ni_b = world.provinces[it->second].nation_id;
            if (ni_a != ni_b) {
                nation_adj[ni_a].insert(ni_b);
                nation_adj[ni_b].insert(ni_a);
            }
        }
    }

    for (uint32_t ni : nation_order) {
        auto adj_it = nation_adj.find(ni);
        if (adj_it == nation_adj.end() || adj_it->second.empty()) continue;

        // Find largest neighbor.
        uint32_t largest_neighbor = *adj_it->second.begin();
        for (uint32_t nj : adj_it->second) {
            if (world.nations[nj].province_ids.size() >
                world.nations[largest_neighbor].province_ids.size()) {
                largest_neighbor = nj;
            }
        }

        if (rng.next_float() < nfp.language_propagation_chance) {
            world.nations[ni].language_family_id =
                world.nations[largest_neighbor].language_family_id;
        }

        // Secondary language: most common different-language neighbor.
        std::unordered_map<std::string, uint32_t> diff_lang_counts;
        for (uint32_t nj : adj_it->second) {
            if (world.nations[nj].language_family_id !=
                world.nations[ni].language_family_id) {
                diff_lang_counts[world.nations[nj].language_family_id]++;
            }
        }
        if (!diff_lang_counts.empty()) {
            auto best = std::max_element(
                diff_lang_counts.begin(), diff_lang_counts.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            world.nations[ni].secondary_language_id = best->first;
        }
    }

    // -----------------------------------------------------------------------
    // §9.5.4 — Border Change Seeding
    // -----------------------------------------------------------------------
    // Build a notable resource threshold: 75th percentile of all deposit quantities.
    std::vector<float> all_quantities;
    for (const auto& prov : world.provinces) {
        for (const auto& dep : prov.deposits) {
            all_quantities.push_back(dep.quantity);
        }
    }
    float notable_threshold = 0.50f;
    if (!all_quantities.empty()) {
        std::sort(all_quantities.begin(), all_quantities.end());
        notable_threshold = all_quantities[all_quantities.size() * 3 / 4];
    }

    for (auto& prov : world.provinces) {
        float instability = 0.0f;

        // Border location: frontier provinces are contested.
        bool is_border_prov = false;
        for (const auto& link : prov.links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end()) continue;
            if (link.type == LinkType::Land || link.type == LinkType::River) {
                if (world.provinces[it->second].nation_id != prov.nation_id) {
                    is_border_prov = true;
                    break;
                }
            }
        }
        if (is_border_prov) instability += nfp.border_instability;

        // Strategic value: notable resources.
        for (const auto& dep : prov.deposits) {
            if (dep.quantity > notable_threshold) {
                instability += nfp.resource_instability;
                break;
            }
        }

        // High settlement attractiveness attracts conquest.
        if (prov.settlement_attractiveness > 0.70f) instability += nfp.attractiveness_instability;

        // Mountain passes and plateaus are contested.
        if (prov.is_mountain_pass) instability += nfp.chokepoint_instability;

        // Infra gap: predicted vs actual infrastructure.
        float predicted_infra = prov.settlement_attractiveness * 0.70f;
        prov.infra_gap = prov.infrastructure_rating - predicted_infra;

        // Colonial signature: infra_gap > 0.20 suggests external development.
        if (prov.infra_gap > 0.20f) instability += nfp.colonial_instability;

        // Poisson draw: instability → expected count → actual count.
        float expected = instability * nfp.instability_to_expected;
        int32_t count = 0;
        float remaining = expected;
        while (remaining > 0.0f && count < nfp.max_border_changes) {
            float u = rng.next_float();
            if (u < 0.001f) u = 0.001f;
            remaining += std::log(u);  // log(u) is negative
            if (remaining > 0.0f) ++count;
        }
        prov.border_change_count = std::max(0, std::min(count, nfp.max_border_changes));

        // Colonial development flag.
        prov.has_colonial_development_event = (
            prov.infra_gap > 0.20f &&
            prov.border_change_count > 0 &&
            prov.infrastructure_rating > 0.50f);
    }

    // -----------------------------------------------------------------------
    // Aggregate nation-level fields
    // -----------------------------------------------------------------------
    for (auto& nation : world.nations) {
        if (nation.province_ids.empty()) continue;

        float sum_attract = 0.0f;
        float sum_infra = 0.0f;
        for (uint32_t pid : nation.province_ids) {
            sum_attract += world.provinces[pid].settlement_attractiveness;
            sum_infra += world.provinces[pid].infrastructure_rating;
        }
        float n = static_cast<float>(nation.province_ids.size());
        nation.gdp_index = std::clamp(sum_attract / n, 0.0f, 1.0f);
        float mean_infra = sum_infra / n;
        nation.governance_quality = std::clamp(
            mean_infra + (rng.next_float() - 0.5f) * 0.20f, 0.0f, 1.0f);

        for (uint32_t pid : nation.province_ids) {
            if (world.provinces[pid].has_colonial_development_event) {
                nation.is_colonial_power = true;
                break;
            }
        }
    }
}

// ===========================================================================
// Stage 9.6 — Nomadic population (WorldGen v0.18 §9.6)
// ===========================================================================

void WorldGenerator::seed_nomadic_population(WorldState& world, DeterministicRNG& rng,
                                              const WorldGeneratorConfig& config) {
    const float realisation = config.nation_formation.nomadic_realisation_factor;
    for (auto& prov : world.provinces) {
        float pastoral_cap = 0.0f;

        // Pastoral carrying capacity from Köppen zone.
        switch (prov.climate.koppen_zone) {
            case KoppenZone::BSk:
            case KoppenZone::BSh:
                pastoral_cap = 0.65f;  // steppe: classic pastoral zone
                break;
            case KoppenZone::BWh:
            case KoppenZone::BWk:
                pastoral_cap = 0.20f;  // desert: sparse; oases anchor movement
                break;
            case KoppenZone::Aw:
                pastoral_cap = 0.45f;  // savanna: seasonal transhumance
                break;
            case KoppenZone::ET:
                pastoral_cap = 0.30f;  // tundra: reindeer pastoralism
                break;
            case KoppenZone::Dfc:
            case KoppenZone::Dfd:
                pastoral_cap = 0.15f;  // taiga fringe; limited
                break;
            default:
                pastoral_cap = 0.0f;
                break;
        }

        // Reduce by terrain roughness (mountains break up grazing range).
        pastoral_cap *= (1.0f - prov.geography.terrain_roughness * 0.50f);

        // Reduce where agricultural productivity already supports dense settlement.
        pastoral_cap *= std::max(0.0f, 1.0f - prov.agricultural_productivity * 1.5f);

        prov.pastoral_carrying_capacity = std::clamp(pastoral_cap, 0.0f, 1.0f);

        // Nomadic fraction: highest where pastoral capacity is high and settled capacity low.
        if (pastoral_cap > 0.10f) {
            prov.nomadic_population_fraction = pastoral_cap * realisation;
        } else {
            prov.nomadic_population_fraction = 0.0f;
        }
    }
}

// ===========================================================================
// Stage 9.7 — Nation capital seeding (WorldGen v0.18 §9.7)
// ===========================================================================

void WorldGenerator::seed_nation_capitals(WorldState& world,
                                           const WorldGeneratorConfig& config) {
    (void)config;  // available for future tuning
    for (auto& nation : world.nations) {
        if (nation.province_ids.empty()) continue;

        // Filter eligible provinces: no continuous permafrost, elevation < 4000m,
        // not badlands/tundra/glacial terrain.
        std::vector<uint32_t> eligible;
        for (uint32_t pid : nation.province_ids) {
            const auto& p = world.provinces[pid];
            if (p.has_permafrost &&
                p.climate.koppen_zone == KoppenZone::EF) continue;
            if (p.geography.elevation_avg_m >= 4000.0f) continue;
            if (p.has_badlands) continue;
            if (p.is_glacial_scoured &&
                p.agricultural_productivity < 0.10f) continue;
            eligible.push_back(pid);
        }
        if (eligible.empty()) {
            eligible = nation.province_ids;  // fallback: no constraint
        }

        // Select highest settlement_attractiveness province.
        uint32_t capital_pid = eligible[0];
        float best_attract = world.provinces[capital_pid].settlement_attractiveness;
        for (uint32_t pid : eligible) {
            float a = world.provinces[pid].settlement_attractiveness;
            if (a > best_attract) {
                best_attract = a;
                capital_pid = pid;
            }
        }

        world.provinces[capital_pid].is_nation_capital = true;
        nation.capital_province_id = capital_pid;
    }
}

// ===========================================================================
// Stage 10.0 — Province archetype classification (24-archetype taxonomy)
// ===========================================================================

// Helper: classify resource class from ResourceType.
static const char* classify_resource_class(ResourceType rt) {
    switch (rt) {
        case ResourceType::CrudeOil:
        case ResourceType::NaturalGas:
        case ResourceType::Coal:
            return "hydrocarbons";
        case ResourceType::Gold:
        case ResourceType::PlatinumGroupMetals:
            return "metals_precious";
        case ResourceType::IronOre:
        case ResourceType::Copper:
        case ResourceType::Bauxite:
        case ResourceType::Lithium:
            return "metals_industrial";
        case ResourceType::Uranium:
            return "energy_nuclear";
        case ResourceType::Cotton:
            return "agricultural_commodity";
        default:
            return "other";
    }
}

std::string WorldGenerator::classify_province_archetype(const Province& prov) {
    // Priority order: most specific conditions first; general fallbacks last.

    // --- Devastated / traumatic states ---
    if (prov.historical_trauma_index > 0.75f && prov.infrastructure_rating < 0.30f) {
        return "war_scar";
    }
    if (prov.historical_trauma_index > 0.65f &&
        prov.demographics.total_population < 50000) {
        return "hollow_land";
    }

    // --- Resource-dominant ---
    float notable_threshold = 0.50f;
    if (!prov.deposits.empty()) {
        const ResourceDeposit* dominant = &prov.deposits[0];
        for (const auto& d : prov.deposits) {
            if (d.quantity > dominant->quantity) dominant = &d;
        }
        if (dominant->quantity > notable_threshold * 1.5f) {
            const char* rclass = classify_resource_class(dominant->type);
            if (std::strcmp(rclass, "hydrocarbons") == 0) {
                return (prov.infrastructure_rating > 0.60f) ? "oil_capital" : "oil_frontier";
            }
            if (std::strcmp(rclass, "metals_precious") == 0) return "gold_rush";
            if (std::strcmp(rclass, "metals_industrial") == 0) return "mining_district";
            if (std::strcmp(rclass, "energy_nuclear") == 0) return "uranium_territory";
            if (std::strcmp(rclass, "agricultural_commodity") == 0) return "plantation_economy";
        }
    }

    // --- Coastal and maritime character ---
    if (prov.geography.port_capacity > 0.70f && prov.infrastructure_rating > 0.65f) {
        return "major_port";
    }
    if (prov.is_atoll || (prov.island_isolation && prov.geography.coastal_length_km > 200.0f)) {
        return "island_enclave";
    }
    if (prov.geography.port_capacity > 0.40f && prov.climate.cold_current_adjacent) {
        return "fishing_port";
    }

    // --- Agricultural character ---
    if (prov.agricultural_productivity > 0.80f &&
        (prov.soil_type == SoilType::Alluvial || prov.soil_type == SoilType::Mollisol)) {
        return "breadbasket";
    }
    if (prov.agricultural_productivity > 0.60f &&
        (prov.climate.koppen_zone == KoppenZone::Cfb ||
         prov.climate.koppen_zone == KoppenZone::Cfa ||
         prov.climate.koppen_zone == KoppenZone::Dfa ||
         prov.climate.koppen_zone == KoppenZone::Dfb)) {
        return "agrarian_interior";
    }
    if (prov.agricultural_productivity > 0.50f &&
        (prov.climate.koppen_zone == KoppenZone::BSk ||
         prov.climate.koppen_zone == KoppenZone::BSh)) {
        return "dryland_farm";
    }

    // --- Landscape character ---
    if (prov.geography.elevation_avg_m > 2500.0f &&
        prov.geography.terrain_roughness > 0.40f) {
        return "high_plateau";
    }
    if (prov.is_glacial_scoured) {
        return "lake_district";
    }
    if ((prov.climate.koppen_zone == KoppenZone::BWh ||
         prov.climate.koppen_zone == KoppenZone::BWk) &&
        !prov.geography.has_artesian_spring) {
        return "true_desert";
    }
    if (prov.geography.is_oasis) {
        return "oasis_settlement";
    }
    if ((prov.climate.koppen_zone == KoppenZone::BSk ||
         prov.climate.koppen_zone == KoppenZone::BSh ||
         prov.climate.koppen_zone == KoppenZone::Aw ||
         prov.climate.koppen_zone == KoppenZone::ET) &&
        prov.nomadic_population_fraction > 0.30f) {
        return "pastoral_steppe";
    }

    // --- Urban and industrial character ---
    if (prov.infrastructure_rating > 0.75f &&
        prov.demographics.total_population > 500000) {
        return "industrial_heartland";
    }
    if (prov.infrastructure_rating > 0.70f &&
        prov.infra_gap > 0.15f &&
        prov.has_colonial_development_event) {
        return "colonial_remnant";
    }

    // --- Frontier and peripheral ---
    if (prov.infrastructure_rating < 0.25f && !prov.deposits.empty()) {
        const ResourceDeposit* dominant = &prov.deposits[0];
        for (const auto& d : prov.deposits) {
            if (d.quantity > dominant->quantity) dominant = &d;
        }
        if (dominant->quantity > 0.20f) {
            return "resource_frontier";
        }
    }
    if (prov.infrastructure_rating < 0.20f &&
        prov.demographics.total_population < 50000) {
        return "marginal_periphery";
    }

    return "ordinary_interior";
}

// ===========================================================================
// Stage 10.1 — Named feature detection
// ===========================================================================

// Feature name generation: deterministic from seed + feature index.
// Uses language-family morphemes for plausible-sounding names.
static std::string generate_feature_name(FeatureType type, DeterministicRNG& rng,
                                          const std::string& language_family_id) {
    // Root morphemes by language family.
    static const char* germanic_roots[] = {
        "Stein", "Wald", "Berg", "Feld", "Mark", "Grund", "Horn", "Bach",
        "Thal", "Eis", "Kalt", "Hoch", "Schwarz", "Grau", "Eisen", "Gold",
    };
    static const char* romance_roots[] = {
        "Mont", "Val", "Rio", "Lago", "Serra", "Costa", "Camp", "Font",
        "Sol", "Vent", "Alto", "Bel", "Grand", "Oscur", "Clar", "Fior",
    };
    static const char* slavic_roots[] = {
        "Gora", "Reka", "Pole", "Ozero", "Step", "Dol", "Grad", "Holm",
        "Bor", "Kamen", "Bel", "Chern", "Vod", "Star", "Nov", "Vel",
    };
    static const char* generic_roots[] = {
        "Thar", "Ven", "Kael", "Morn", "Asha", "Drev", "Hael", "Zorn",
        "Yren", "Bael", "Torn", "Veld", "Skar", "Lum", "Krev", "Daan",
    };

    // Type suffixes.
    static const char* mountain_suffixes[] = {"fjeld", "horn", "peak", "ridge", "klippe"};
    static const char* river_suffixes[]    = {"wasser", "flod", "beck", "strom", "burn"};
    static const char* lake_suffixes[]     = {"mere", "tarn", "see", "loch", "vann"};
    static const char* desert_suffixes[]   = {"waste", "sands", "steppe", "erg", "hamada"};
    static const char* plain_suffixes[]    = {"plain", "feld", "pampa", "veldt", "savanna"};
    static const char* generic_suffixes[]  = {"land", "reach", "mark", "vale", "ward"};

    const char** roots = generic_roots;
    size_t root_count = 16;
    if (language_family_id == "germanic" || language_family_id == "nordic") {
        roots = germanic_roots; root_count = 16;
    } else if (language_family_id == "romance") {
        roots = romance_roots; root_count = 16;
    } else if (language_family_id == "slavic") {
        roots = slavic_roots; root_count = 16;
    }

    const char** suffixes = generic_suffixes;
    size_t suffix_count = 5;
    switch (type) {
        case FeatureType::MountainRange: case FeatureType::Plateau:
            suffixes = mountain_suffixes; suffix_count = 5; break;
        case FeatureType::River:
            suffixes = river_suffixes; suffix_count = 5; break;
        case FeatureType::Lake:
            suffixes = lake_suffixes; suffix_count = 5; break;
        case FeatureType::Desert:
            suffixes = desert_suffixes; suffix_count = 5; break;
        case FeatureType::Plain: case FeatureType::Forest:
            suffixes = plain_suffixes; suffix_count = 5; break;
        default: break;
    }

    uint32_t ri = static_cast<uint32_t>(rng.next_float() * root_count) % root_count;
    uint32_t si = static_cast<uint32_t>(rng.next_float() * suffix_count) % suffix_count;
    return std::string(roots[ri]) + suffixes[si];
}

static const char* feature_type_str(FeatureType ft) {
    switch (ft) {
        case FeatureType::MountainRange: return "mountain_range";
        case FeatureType::River:         return "river";
        case FeatureType::Lake:          return "lake";
        case FeatureType::Desert:        return "desert";
        case FeatureType::Basin:         return "basin";
        case FeatureType::Strait:        return "strait";
        case FeatureType::Bay:           return "bay";
        case FeatureType::Island:        return "island";
        case FeatureType::Plateau:       return "plateau";
        case FeatureType::Cape:          return "cape";
        case FeatureType::Crater:        return "crater";
        case FeatureType::Plain:         return "plain";
        case FeatureType::Forest:        return "forest";
        case FeatureType::Peninsula:     return "peninsula";
        case FeatureType::Archipelago:   return "archipelago";
    }
    return "unknown";
}

void WorldGenerator::detect_named_features(WorldState& world, DeterministicRNG& rng,
                                            const WorldGeneratorConfig& /*config*/) {
    world.named_features.clear();
    uint64_t next_id = 1;

    // Build h3_to_idx for neighbor lookup.
    std::unordered_map<H3Index, uint32_t> h3_to_idx;
    for (uint32_t i = 0; i < world.provinces.size(); ++i) {
        h3_to_idx[world.provinces[i].h3_index] = i;
    }

    // Track which provinces are already assigned to features to avoid duplication.
    std::unordered_set<uint32_t> assigned_mountain;
    std::unordered_set<uint32_t> assigned_desert;
    std::unordered_set<uint32_t> assigned_plain;

    for (uint32_t pi = 0; pi < world.provinces.size(); ++pi) {
        const auto& prov = world.provinces[pi];
        const auto& nation = world.nations[prov.nation_id];

        // --- Mountain ranges: high roughness + elevation ---
        if (prov.geography.terrain_roughness > 0.55f &&
            prov.geography.elevation_avg_m > 800.0f &&
            assigned_mountain.find(pi) == assigned_mountain.end()) {
            NamedFeature f{};
            f.id = next_id++;
            f.type = FeatureType::MountainRange;
            f.extent.push_back(prov.h3_index);
            f.peak_elevation_m = prov.geography.elevation_avg_m;
            assigned_mountain.insert(pi);

            // Expand to adjacent high-terrain provinces.
            for (const auto& link : prov.links) {
                auto it = h3_to_idx.find(link.neighbor_h3);
                if (it == h3_to_idx.end()) continue;
                const auto& np = world.provinces[it->second];
                if (np.geography.terrain_roughness > 0.50f &&
                    np.geography.elevation_avg_m > 600.0f &&
                    assigned_mountain.find(it->second) == assigned_mountain.end()) {
                    f.extent.push_back(np.h3_index);
                    assigned_mountain.insert(it->second);
                    if (np.geography.elevation_avg_m > f.peak_elevation_m)
                        f.peak_elevation_m = np.geography.elevation_avg_m;
                }
            }

            f.significance = std::min(1.0f, f.peak_elevation_m / 5000.0f);
            f.name = generate_feature_name(FeatureType::MountainRange, rng,
                                            nation.language_family_id);
            // Check if feature crosses national border.
            for (H3Index h3 : f.extent) {
                auto it2 = h3_to_idx.find(h3);
                if (it2 != h3_to_idx.end() &&
                    world.provinces[it2->second].nation_id != prov.nation_id) {
                    f.is_disputed = true;
                    // Generate local_name from the other nation's language.
                    f.local_name = generate_feature_name(
                        FeatureType::MountainRange, rng,
                        world.nations[world.provinces[it2->second].nation_id]
                            .language_family_id);
                    break;
                }
            }
            world.named_features.push_back(std::move(f));
        }

        // --- Rivers: high river_access ---
        if (prov.geography.river_access > 0.50f) {
            NamedFeature f{};
            f.id = next_id++;
            f.type = FeatureType::River;
            f.extent.push_back(prov.h3_index);
            f.is_navigable = (prov.geography.river_access > 0.60f);
            f.significance = prov.geography.river_access;
            f.name = generate_feature_name(FeatureType::River, rng,
                                            nation.language_family_id);
            world.named_features.push_back(std::move(f));
        }

        // --- Deserts: arid + low ag ---
        if ((prov.climate.koppen_zone == KoppenZone::BWh ||
             prov.climate.koppen_zone == KoppenZone::BWk) &&
            prov.agricultural_productivity < 0.15f &&
            assigned_desert.find(pi) == assigned_desert.end()) {
            NamedFeature f{};
            f.id = next_id++;
            f.type = FeatureType::Desert;
            f.extent.push_back(prov.h3_index);
            f.area_km2 = prov.geography.area_km2;
            assigned_desert.insert(pi);

            for (const auto& link : prov.links) {
                auto it = h3_to_idx.find(link.neighbor_h3);
                if (it == h3_to_idx.end()) continue;
                const auto& np = world.provinces[it->second];
                if ((np.climate.koppen_zone == KoppenZone::BWh ||
                     np.climate.koppen_zone == KoppenZone::BWk) &&
                    np.agricultural_productivity < 0.15f &&
                    assigned_desert.find(it->second) == assigned_desert.end()) {
                    f.extent.push_back(np.h3_index);
                    f.area_km2 += np.geography.area_km2;
                    assigned_desert.insert(it->second);
                }
            }

            f.significance = std::min(1.0f, f.area_km2 / 50000.0f);
            f.name = generate_feature_name(FeatureType::Desert, rng,
                                            nation.language_family_id);
            world.named_features.push_back(std::move(f));
        }

        // --- Impact craters ---
        if (prov.has_impact_crater) {
            NamedFeature f{};
            f.id = next_id++;
            f.type = FeatureType::Crater;
            f.extent.push_back(prov.h3_index);
            f.area_km2 = 3.14159f * (prov.impact_crater_diameter_km / 2.0f) *
                         (prov.impact_crater_diameter_km / 2.0f);
            f.significance = std::min(1.0f, prov.impact_crater_diameter_km / 200.0f);
            f.name = generate_feature_name(FeatureType::Crater, rng,
                                            nation.language_family_id);
            world.named_features.push_back(std::move(f));
        }

        // --- Endorheic lakes/basins ---
        if (prov.geography.is_endorheic && prov.geography.river_access > 0.20f) {
            NamedFeature f{};
            f.id = next_id++;
            f.type = FeatureType::Lake;
            f.extent.push_back(prov.h3_index);
            f.area_km2 = prov.geography.area_km2 * 0.15f;  // lake fraction of basin
            f.significance = 0.40f + prov.geography.river_access * 0.30f;
            f.name = generate_feature_name(FeatureType::Lake, rng,
                                            nation.language_family_id);
            world.named_features.push_back(std::move(f));
        }

        // --- Plains: flat + moderate ag + not desert ---
        if (prov.geography.terrain_roughness < 0.20f &&
            prov.agricultural_productivity > 0.40f &&
            prov.climate.koppen_zone != KoppenZone::BWh &&
            prov.climate.koppen_zone != KoppenZone::BWk &&
            assigned_plain.find(pi) == assigned_plain.end()) {
            NamedFeature f{};
            f.id = next_id++;
            f.type = FeatureType::Plain;
            f.extent.push_back(prov.h3_index);
            f.area_km2 = prov.geography.area_km2;
            assigned_plain.insert(pi);
            f.significance = 0.30f + prov.agricultural_productivity * 0.30f;
            f.name = generate_feature_name(FeatureType::Plain, rng,
                                            nation.language_family_id);
            world.named_features.push_back(std::move(f));
        }
    }
}

// ===========================================================================
// Stage 10.2 — Province history generation
// ===========================================================================

static const char* historical_event_type_str(HistoricalEventType t) {
    switch (t) {
        case HistoricalEventType::FoundingEvent:              return "FoundingEvent";
        case HistoricalEventType::TradeRouteEstablished:      return "TradeRouteEstablished";
        case HistoricalEventType::ResourceDiscovery:          return "ResourceDiscovery";
        case HistoricalEventType::PortDevelopment:            return "PortDevelopment";
        case HistoricalEventType::ColonialDevelopment:        return "ColonialDevelopment";
        case HistoricalEventType::MigrationInflux:            return "MigrationInflux";
        case HistoricalEventType::PopulationCollapse:         return "PopulationCollapse";
        case HistoricalEventType::InfrastructureDestruction:  return "InfrastructureDestruction";
        case HistoricalEventType::ResourceDepletion:          return "ResourceDepletion";
        case HistoricalEventType::EnvironmentalDisaster:      return "EnvironmentalDisaster";
        case HistoricalEventType::EconomicCollapse:           return "EconomicCollapse";
        case HistoricalEventType::ForcedRelocation:           return "ForcedRelocation";
        case HistoricalEventType::Famine:                     return "Famine";
        case HistoricalEventType::BorderChange:               return "BorderChange";
        case HistoricalEventType::OccupationHistory:          return "OccupationHistory";
        case HistoricalEventType::IndependenceEvent:          return "IndependenceEvent";
        case HistoricalEventType::CivilConflict:              return "CivilConflict";
        case HistoricalEventType::TreatyProvision:            return "TreatyProvision";
        case HistoricalEventType::ImpactEvent:                return "ImpactEvent";
        case HistoricalEventType::VolcanicEvent:              return "VolcanicEvent";
        case HistoricalEventType::FloodEvent:                 return "FloodEvent";
        case HistoricalEventType::ClimateShift:               return "ClimateShift";
    }
    return "Unknown";
}

// Trauma weight per event type (from spec §10.2).
static float trauma_weight(HistoricalEventType t) {
    switch (t) {
        case HistoricalEventType::ForcedRelocation:           return 0.90f;
        case HistoricalEventType::Famine:                     return 0.80f;
        case HistoricalEventType::CivilConflict:              return 0.70f;
        case HistoricalEventType::PopulationCollapse:         return 0.65f;
        case HistoricalEventType::OccupationHistory:          return 0.60f;
        case HistoricalEventType::InfrastructureDestruction:  return 0.40f;
        case HistoricalEventType::EconomicCollapse:           return 0.35f;
        case HistoricalEventType::ResourceDepletion:          return 0.20f;
        default:                                              return 0.0f;
    }
}

void WorldGenerator::generate_province_histories(WorldState& world, DeterministicRNG& rng,
                                                   const WorldGeneratorConfig& /*config*/) {
    for (auto& prov : world.provinces) {
        ProvinceHistory& hist = prov.history;
        hist.events.clear();

        // --- Classify archetype ---
        hist.province_archetype_label = classify_province_archetype(prov);

        // --- Geology always generates a founding event ---
        {
            HistoricalEvent e{};
            e.type = HistoricalEventType::FoundingEvent;
            e.years_before_game_start = -static_cast<int32_t>(
                200 + rng.next_float() * 800);  // 200-1000 years ago
            e.magnitude = 0.30f + rng.next_float() * 0.30f;
            e.headline = "The region was first settled by communities drawn to its "
                         "natural advantages.";
            e.description = "Early settlement patterns were determined by the "
                            "availability of fresh water, arable land, and defensible terrain.";
            e.lasting_effect = "The initial settlement sites still anchor the province's "
                               "population distribution.";
            e.has_living_memory = false;
            hist.events.push_back(std::move(e));
        }

        // --- Explain infrastructure anomalies ---
        float predicted = prov.settlement_attractiveness * 0.70f;
        float gap = prov.infrastructure_rating - predicted;

        if (gap > 0.15f) {
            // Better than formula predicts — something built it up.
            float roll = rng.next_float();
            HistoricalEventType cause;
            if (roll < 0.30f) cause = HistoricalEventType::TradeRouteEstablished;
            else if (roll < 0.55f) cause = HistoricalEventType::ResourceDiscovery;
            else if (roll < 0.75f) cause = HistoricalEventType::ColonialDevelopment;
            else if (roll < 0.90f) cause = HistoricalEventType::PortDevelopment;
            else cause = HistoricalEventType::MigrationInflux;

            HistoricalEvent e{};
            e.type = cause;
            e.years_before_game_start = -static_cast<int32_t>(
                30 + rng.next_float() * 120);
            e.magnitude = std::min(1.0f, gap * 3.0f);
            e.headline = "External investment or strategic interest drove infrastructure "
                         "development beyond what local conditions would normally sustain.";
            e.description = "The province's infrastructure rating exceeds what its "
                            "geographic attractiveness alone would predict, indicating "
                            "deliberate development driven by trade, resources, or "
                            "colonial administration.";
            e.lasting_effect = "Current infrastructure rating reflects this historical "
                               "over-investment.";
            e.has_living_memory = (e.years_before_game_start > -80);
            hist.events.push_back(std::move(e));
        } else if (gap < -0.15f) {
            // Worse than formula predicts — something damaged or suppressed it.
            float roll = rng.next_float();
            HistoricalEventType cause;
            if (roll < 0.25f) cause = HistoricalEventType::InfrastructureDestruction;
            else if (roll < 0.45f) cause = HistoricalEventType::EconomicCollapse;
            else if (roll < 0.65f) cause = HistoricalEventType::ResourceDepletion;
            else if (roll < 0.80f) cause = HistoricalEventType::PopulationCollapse;
            else if (roll < 0.90f) cause = HistoricalEventType::ForcedRelocation;
            else cause = HistoricalEventType::CivilConflict;

            HistoricalEvent e{};
            e.type = cause;
            e.years_before_game_start = -static_cast<int32_t>(
                20 + rng.next_float() * 100);
            e.magnitude = std::min(1.0f, std::fabs(gap) * 3.0f);
            e.headline = "A disruptive event suppressed development below the "
                         "province's natural potential.";
            e.description = "The province's infrastructure lags behind what its "
                            "geographic advantages would normally produce, suggesting "
                            "historical disruption, conflict, or economic failure.";
            e.lasting_effect = "The infrastructure deficit persists into the present day.";
            e.has_living_memory = (e.years_before_game_start > -80);
            hist.events.push_back(std::move(e));
        }

        // --- Impact crater history ---
        if (prov.has_impact_crater) {
            HistoricalEvent e{};
            e.type = HistoricalEventType::ImpactEvent;
            e.years_before_game_start = -static_cast<int32_t>(
                1000000 + rng.next_float() * 500000000);  // millions of years
            e.magnitude = std::min(1.0f, prov.impact_crater_diameter_km / 200.0f);
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "An asteroid impact created a crater %d km in diameter.",
                static_cast<int>(prov.impact_crater_diameter_km));
            e.headline = buf;
            e.description = "The impact structure concentrated platinum group metals "
                            "and nickel sulfides in the surrounding rock through "
                            "hydrothermal alteration.";
            e.lasting_effect = "The crater's mineral signature remains economically "
                               "significant.";
            e.has_living_memory = false;
            hist.events.push_back(std::move(e));
        }

        // --- Volcanic event for subduction/hotspot provinces ---
        if ((prov.tectonic_context == TectonicContext::Subduction ||
             prov.tectonic_context == TectonicContext::HotSpot) &&
            rng.next_float() < 0.30f) {
            HistoricalEvent e{};
            e.type = HistoricalEventType::VolcanicEvent;
            e.years_before_game_start = -static_cast<int32_t>(
                50 + rng.next_float() * 500);
            e.magnitude = 0.30f + rng.next_float() * 0.50f;
            e.headline = "A significant volcanic eruption shaped the local terrain "
                         "and enriched soils with mineral-laden ash.";
            e.description = "Volcanic activity deposited fertile ash soils and created "
                            "geothermal features, while also posing ongoing hazard risks.";
            e.lasting_effect = "Volcanic soils support above-average agricultural "
                               "productivity in the vicinity.";
            e.has_living_memory = (e.years_before_game_start > -80);
            hist.events.push_back(std::move(e));
        }

        // --- Resource discovery ---
        if (!prov.deposits.empty()) {
            const ResourceDeposit* best = &prov.deposits[0];
            for (const auto& d : prov.deposits) {
                if (d.quantity > best->quantity) best = &d;
            }
            if (best->quantity > 0.50f && best->era_unlock == 1) {
                HistoricalEvent e{};
                e.type = HistoricalEventType::ResourceDiscovery;
                e.years_before_game_start = -static_cast<int32_t>(
                    40 + rng.next_float() * 160);
                e.magnitude = std::min(1.0f, best->quantity);
                e.headline = "Significant mineral or energy deposits were confirmed "
                             "in the province.";
                e.description = "The discovery attracted prospectors, investment capital, "
                                "and infrastructure development to the region.";
                e.lasting_effect = "Extraction industries remain a defining feature of "
                                   "the provincial economy.";
                e.has_living_memory = (e.years_before_game_start > -80);
                hist.events.push_back(std::move(e));
            }
        }

        // --- Flood history ---
        if (prov.climate.flood_vulnerability > 0.70f) {
            HistoricalEvent e{};
            e.type = HistoricalEventType::FloodEvent;
            e.years_before_game_start = -static_cast<int32_t>(
                10 + rng.next_float() * 90);
            e.magnitude = prov.climate.flood_vulnerability;
            e.headline = "Catastrophic flooding affected the province.";
            e.description = "The province's position in a floodplain or low-lying "
                            "delta makes it vulnerable to periodic inundation.";
            e.lasting_effect = "Flood infrastructure remains a critical investment "
                               "priority.";
            e.has_living_memory = (e.years_before_game_start > -80);
            hist.events.push_back(std::move(e));
        }

        // --- Border change history ---
        if (prov.border_change_count > 0) {
            HistoricalEvent e{};
            e.type = HistoricalEventType::BorderChange;
            e.years_before_game_start = -static_cast<int32_t>(
                15 + rng.next_float() * 135);
            e.magnitude = std::min(1.0f, prov.border_change_count * 0.20f);
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "The province changed national administration %d time%s in the "
                "pre-game period.",
                prov.border_change_count,
                prov.border_change_count > 1 ? "s" : "");
            e.headline = buf;
            e.description = "Contested borders and changing sovereignty have left "
                            "cultural and institutional legacies.";
            e.lasting_effect = "Border instability affects institutional trust and "
                               "political volatility.";
            e.has_living_memory = (e.years_before_game_start > -80);
            hist.events.push_back(std::move(e));
        }

        // --- Sort events chronologically ---
        std::sort(hist.events.begin(), hist.events.end(),
                  [](const HistoricalEvent& a, const HistoricalEvent& b) {
                      return a.years_before_game_start < b.years_before_game_start;
                  });

        // --- Compute historical_trauma_index from events ---
        float trauma = 0.0f;
        for (const auto& e : hist.events) {
            float tw = trauma_weight(e.type);
            float recency = 1.0f / (1.0f + std::fabs(
                static_cast<float>(e.years_before_game_start)) / 50.0f);
            trauma += tw * e.magnitude * recency;
        }
        prov.historical_trauma_index = std::clamp(trauma, 0.0f, 1.0f);

        // --- Generate summary from events ---
        std::string summary;
        for (size_t i = 0; i < std::min(hist.events.size(), size_t(3)); ++i) {
            if (!summary.empty()) summary += " ";
            summary += hist.events[i].headline;
        }
        hist.summary = summary;

        // --- Current character from archetype ---
        hist.current_character = "A " + hist.province_archetype_label +
                                  " province defined by its " +
                                  (prov.infrastructure_rating > 0.60f
                                       ? "developed infrastructure"
                                       : "natural landscape") +
                                  " and " +
                                  (prov.agricultural_productivity > 0.50f
                                       ? "productive agricultural base."
                                       : "extractive or service economy.");
    }
}

// ===========================================================================
// Stage 10.3 — Pre-game event extraction
// ===========================================================================

void WorldGenerator::seed_pre_game_events(WorldState& world, DeterministicRNG& rng,
                                            const WorldGeneratorConfig& /*config*/) {
    world.pre_game_events.clear();

    // Scan all province histories for events within 40 years of game start.
    // years_before_game_start is stored as negative, so within-40 means > -40.
    for (const auto& prov : world.provinces) {
        for (const auto& e : prov.history.events) {
            if (e.years_before_game_start >= -40 && e.years_before_game_start < 0) {
                PreGameEvent pge{};
                pge.type = e.type;
                pge.years_before_start = -e.years_before_game_start;  // positive 1–40
                pge.epicenter_province = prov.h3_index;
                pge.affected_provinces.push_back(prov.h3_index);
                pge.magnitude = e.magnitude;
                pge.description = e.description;

                // Derive active economic effects from event type and magnitude.
                switch (e.type) {
                    case HistoricalEventType::InfrastructureDestruction:
                    case HistoricalEventType::EnvironmentalDisaster:
                    case HistoricalEventType::FloodEvent:
                    case HistoricalEventType::VolcanicEvent: {
                        // Infrastructure damage decays: residual = magnitude * (years / 40)
                        float decay = 1.0f - static_cast<float>(pge.years_before_start) / 50.0f;
                        pge.infrastructure_damage = std::clamp(
                            e.magnitude * std::max(0.0f, decay), 0.0f, 1.0f);
                        break;
                    }
                    case HistoricalEventType::PopulationCollapse:
                    case HistoricalEventType::ForcedRelocation:
                    case HistoricalEventType::Famine: {
                        float recovery = 1.0f - static_cast<float>(pge.years_before_start) / 60.0f;
                        pge.population_displacement = std::clamp(
                            e.magnitude * 0.5f * std::max(0.0f, recovery), 0.0f, 1.0f);
                        break;
                    }
                    case HistoricalEventType::BorderChange:
                    case HistoricalEventType::IndependenceEvent:
                    case HistoricalEventType::OccupationHistory:
                        pge.has_active_claim = (pge.years_before_start <= 30 &&
                                                e.magnitude > 0.40f);
                        break;
                    default:
                        break;
                }

                // Living witnesses: events within 40 years always have them.
                pge.has_living_witnesses = true;

                // Add affected neighbors for high-magnitude events.
                if (e.magnitude > 0.60f) {
                    for (const auto& link : prov.links) {
                        pge.affected_provinces.push_back(link.neighbor_h3);
                    }
                }

                world.pre_game_events.push_back(std::move(pge));
            }
        }
    }

    // Sort by years_before_start descending (oldest first).
    std::sort(world.pre_game_events.begin(), world.pre_game_events.end(),
              [](const PreGameEvent& a, const PreGameEvent& b) {
                  return a.years_before_start > b.years_before_start;
              });
}

// ===========================================================================
// Stage 10.4 — Loading screen commentary
// ===========================================================================

// Helper: find the named feature with the highest significance of a given type.
static const NamedFeature* find_best_feature(const WorldState& world, FeatureType type) {
    const NamedFeature* best = nullptr;
    for (const auto& f : world.named_features) {
        if (f.type == type && (!best || f.significance > best->significance)) {
            best = &f;
        }
    }
    return best;
}

void WorldGenerator::generate_loading_commentary(WorldState& world, DeterministicRNG& rng,
                                                   const WorldGeneratorConfig& /*config*/) {
    LoadingCommentary& lc = world.loading_commentary;

    const uint32_t prov_count = static_cast<uint32_t>(world.provinces.size());

    // --- Stage texts: world-specific, not generic ---

    // Stage 1 — Tectonics
    {
        uint32_t subduction_count = 0;
        for (const auto& p : world.provinces) {
            if (p.tectonic_context == TectonicContext::Subduction) ++subduction_count;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "The continental plates are settling. %u of %u provinces sit on active "
            "subduction zones where new mountains are still being built.",
            subduction_count, prov_count);
        lc.stage_1_text = buf;
    }

    // Stage 2 — Erosion
    {
        const NamedFeature* river = find_best_feature(world, FeatureType::River);
        if (river) {
            lc.stage_2_text = "The " + river->name + " is cutting through the landscape. "
                "By the time civilisation arrives, the valley will define trade routes "
                "for centuries.";
        } else {
            lc.stage_2_text = "Wind and water are shaping the terrain. Valleys deepen, "
                "plateaus erode, and the landscape takes its final form.";
        }
    }

    // Stage 3 — Hydrology
    {
        uint32_t river_count = 0;
        for (const auto& f : world.named_features) {
            if (f.type == FeatureType::River) ++river_count;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%u named river systems are establishing their courses. "
            "Where they reach the sea, deltas and estuaries form.",
            river_count);
        lc.stage_3_text = buf;
    }

    // Stage 4 — Atmosphere
    {
        uint32_t monsoon_count = 0;
        for (const auto& p : world.provinces) {
            if (p.climate.is_monsoon) ++monsoon_count;
        }
        if (monsoon_count > 0) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "Monsoon patterns are emerging across %u provinces. "
                "Seasonal rains will define agriculture and flood risk.",
                monsoon_count);
            lc.stage_4_text = buf;
        } else {
            lc.stage_4_text = "The atmosphere is stabilising. Rain shadows form behind "
                "mountain ranges, and ocean currents begin to shape coastal climates.";
        }
    }

    // Stage 5 — Soils
    {
        uint32_t fertile = 0;
        for (const auto& p : world.provinces) {
            if (p.agricultural_productivity > 0.60f) ++fertile;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%u provinces develop fertile soils capable of sustaining large populations. "
            "The rest will depend on trade, industry, or the sea.",
            fertile);
        lc.stage_5_text = buf;
    }

    // Stage 6 — Biomes
    {
        uint32_t forested = 0;
        for (const auto& p : world.provinces) {
            if (p.geography.forest_coverage > 0.50f) ++forested;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Forest covers %u provinces. Grasslands and savannas fill the gaps "
            "between the treeline and the deserts.",
            forested);
        lc.stage_6_text = buf;
    }

    // Stage 7 — Features
    {
        const NamedFeature* mtn = find_best_feature(world, FeatureType::MountainRange);
        if (mtn && mtn->peak_elevation_m > 0.0f) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "The %s rise to %.0f metres, dividing climates and cultures. "
                "Passes through the range will become strategic chokepoints.",
                mtn->name.c_str(), mtn->peak_elevation_m);
            lc.stage_7_text = buf;
        } else {
            lc.stage_7_text = "Special geographic features are forming: karst caves, "
                "fjords, atolls, and impact craters, each with unique economic potential.";
        }
    }

    // Stage 8 — Resources
    {
        uint32_t deposit_count = 0;
        for (const auto& p : world.provinces) {
            deposit_count += static_cast<uint32_t>(p.deposits.size());
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%u resource deposits are embedded in the geology. "
            "Some will be found early. Others will wait for the technology to reach them.",
            deposit_count);
        lc.stage_8_text = buf;
    }

    // Stage 9 — Population
    {
        // Find the highest-attractiveness province.
        const Province* best = &world.provinces[0];
        for (const auto& p : world.provinces) {
            if (p.settlement_attractiveness > best->settlement_attractiveness) best = &p;
        }
        lc.stage_9_text = best->fictional_name + " emerges as the most attractive "
            "settlement site. Early communities cluster around fresh water, arable "
            "land, and defensible terrain.";
    }

    // Stage 10 — History
    {
        if (!world.pre_game_events.empty()) {
            const auto& pge = world.pre_game_events[0];
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "%u events within living memory will shape the world at game start. "
                "The oldest occurred %d years before the present.",
                static_cast<uint32_t>(world.pre_game_events.size()),
                pge.years_before_start);
            lc.stage_10_text = buf;
        } else {
            lc.stage_10_text = "The pre-game period was remarkably stable. No major "
                "disruptions within living memory.";
        }
    }

    // Stage 11 — Final
    lc.stage_11_text = "The world is ready.";

    // --- Sidebar facts: 8–15 short, true-to-this-world statements ---
    lc.sidebar_facts.clear();

    // Facts from named features.
    for (const auto& f : world.named_features) {
        if (lc.sidebar_facts.size() >= 15) break;
        char buf[256];
        switch (f.type) {
            case FeatureType::River:
                if (f.length_km > 0.0f) {
                    std::snprintf(buf, sizeof(buf),
                        "The %s stretches %.0f km. %s",
                        f.name.c_str(), f.length_km,
                        f.is_navigable ? "It is navigable by barge."
                                       : "Its rapids prevent navigation.");
                    lc.sidebar_facts.emplace_back(buf);
                }
                break;
            case FeatureType::MountainRange:
                if (f.peak_elevation_m > 0.0f) {
                    std::snprintf(buf, sizeof(buf),
                        "The %s reach %.0f metres at their highest point.",
                        f.name.c_str(), f.peak_elevation_m);
                    lc.sidebar_facts.emplace_back(buf);
                }
                break;
            case FeatureType::Desert:
                if (f.area_km2 > 0.0f) {
                    std::snprintf(buf, sizeof(buf),
                        "The %s covers %.0f km\u00B2 of arid terrain.",
                        f.name.c_str(), f.area_km2);
                    lc.sidebar_facts.emplace_back(buf);
                }
                break;
            case FeatureType::Lake:
                std::snprintf(buf, sizeof(buf),
                    "The %s has no outlet to the sea. Its waters grow more saline "
                    "with each passing century.",
                    f.name.c_str());
                lc.sidebar_facts.emplace_back(buf);
                break;
            case FeatureType::Crater:
                std::snprintf(buf, sizeof(buf),
                    "The %s is the remnant of an asteroid strike. "
                    "Platinum group metals concentrate in its rim.",
                    f.name.c_str());
                lc.sidebar_facts.emplace_back(buf);
                break;
            default:
                if (f.significance > 0.60f) {
                    std::snprintf(buf, sizeof(buf),
                        "The %s is a significant geographic feature of this world.",
                        f.name.c_str());
                    lc.sidebar_facts.emplace_back(buf);
                }
                break;
        }
    }

    // Facts from province data.
    for (const auto& p : world.provinces) {
        if (lc.sidebar_facts.size() >= 15) break;
        if (p.has_permafrost) {
            lc.sidebar_facts.emplace_back(
                p.fictional_name + " is locked in permafrost. Oil and gas deposits "
                "here cannot be accessed until thawing technology arrives.");
            break;  // One permafrost fact is enough.
        }
    }
    for (const auto& p : world.provinces) {
        if (lc.sidebar_facts.size() >= 15) break;
        if (p.geography.is_oasis) {
            lc.sidebar_facts.emplace_back(
                p.fictional_name + " exists because of a single underground spring. "
                "Without it, this would be empty desert.");
            break;
        }
    }
    for (const auto& p : world.provinces) {
        if (lc.sidebar_facts.size() >= 15) break;
        if (p.is_nation_capital && p.settlement_attractiveness > 0.60f) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "%s was chosen as a national capital for its strategic position "
                "and natural advantages.",
                p.fictional_name.c_str());
            lc.sidebar_facts.emplace_back(buf);
            break;
        }
    }

    // Pad to minimum 8 with general world stats.
    {
        char buf[256];
        if (lc.sidebar_facts.size() < 8) {
            uint32_t nation_count = static_cast<uint32_t>(world.nations.size());
            std::snprintf(buf, sizeof(buf),
                "This world contains %u nations across %u provinces.",
                nation_count, prov_count);
            lc.sidebar_facts.emplace_back(buf);
        }
        if (lc.sidebar_facts.size() < 8) {
            uint32_t total_pop = 0;
            for (const auto& p : world.provinces) {
                total_pop += p.demographics.total_population;
            }
            std::snprintf(buf, sizeof(buf),
                "The total population is approximately %u.", total_pop);
            lc.sidebar_facts.emplace_back(buf);
        }
        if (lc.sidebar_facts.size() < 8) {
            uint32_t feature_count = static_cast<uint32_t>(world.named_features.size());
            std::snprintf(buf, sizeof(buf),
                "%u geographic features have been named.", feature_count);
            lc.sidebar_facts.emplace_back(buf);
        }
        if (lc.sidebar_facts.size() < 8) {
            uint32_t deposit_count = 0;
            for (const auto& p : world.provinces) {
                deposit_count += static_cast<uint32_t>(p.deposits.size());
            }
            std::snprintf(buf, sizeof(buf),
                "Geologists have mapped %u resource deposits across the world.",
                deposit_count);
            lc.sidebar_facts.emplace_back(buf);
        }
        if (lc.sidebar_facts.size() < 8) {
            uint32_t coastal = 0;
            for (const auto& p : world.provinces) {
                if (p.geography.coastal_length_km > 10.0f) ++coastal;
            }
            std::snprintf(buf, sizeof(buf),
                "%u provinces have significant coastline.", coastal);
            lc.sidebar_facts.emplace_back(buf);
        }
        if (lc.sidebar_facts.size() < 8) {
            uint32_t landlocked = 0;
            for (const auto& p : world.provinces) {
                if (p.geography.is_landlocked) ++landlocked;
            }
            std::snprintf(buf, sizeof(buf),
                "%u provinces are landlocked, dependent on overland trade.",
                landlocked);
            lc.sidebar_facts.emplace_back(buf);
        }
        if (lc.sidebar_facts.size() < 8) {
            std::snprintf(buf, sizeof(buf),
                "World seed: %lu. Same seed, same world, every time.",
                static_cast<unsigned long>(world.world_seed));
            lc.sidebar_facts.emplace_back(buf);
        }
        if (lc.sidebar_facts.size() < 8) {
            lc.sidebar_facts.emplace_back(
                "Every province has a unique geological history.");
        }
    }
}

// ===========================================================================
// Stage 10.5 — Encyclopedia JSON output
// ===========================================================================

static const char* pre_game_event_type_str(HistoricalEventType t) {
    return historical_event_type_str(t);
}

nlohmann::json WorldGenerator::to_encyclopedia_json(const WorldState& world,
                                                     const WorldGeneratorConfig& config) {
    using json = nlohmann::json;
    json root;

    root["schema_version"] = "1.0";
    root["world_seed"] = world.world_seed;
    root["generated_at"] = "2000-01-01T00:00:00Z";  // game start date
    root["planet_name"] = world.nations.empty() ? "Unknown" : "World-" +
                          std::to_string(world.world_seed);

    // --- Provinces ---
    json provinces_obj = json::object();
    for (const auto& prov : world.provinces) {
        json p;
        p["province_id"] = prov.h3_index;
        p["province_name"] = prov.fictional_name;
        p["archetype"] = prov.history.province_archetype_label;
        p["current_character"] = prov.history.current_character;

        if (config.commentary_depth == CommentaryDepth::full) {
            p["summary"] = prov.history.summary;

            json hist;
            json events_arr = json::array();
            for (const auto& e : prov.history.events) {
                events_arr.push_back({
                    {"type", historical_event_type_str(e.type)},
                    {"years_before_game_start", e.years_before_game_start},
                    {"headline", e.headline},
                    {"description", e.description},
                    {"magnitude", e.magnitude},
                    {"lasting_effect", e.lasting_effect},
                    {"has_living_memory", e.has_living_memory},
                });
            }
            hist["events"] = std::move(events_arr);
            hist["historical_trauma_index"] = prov.historical_trauma_index;
            p["history"] = std::move(hist);
        }

        // Sidebar facts per province (1–3).
        json sidebar = json::array();
        if (prov.has_impact_crater) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "A %d km impact crater dominates the terrain.",
                static_cast<int>(prov.impact_crater_diameter_km));
            sidebar.push_back(buf);
        }
        if (prov.geography.is_oasis) {
            sidebar.push_back("An underground spring sustains life in this arid region.");
        }
        if (prov.is_nation_capital) {
            sidebar.push_back("This province serves as a national capital.");
        }
        if (sidebar.empty()) {
            sidebar.push_back("A " + prov.history.province_archetype_label + " province.");
        }
        p["sidebar_facts"] = std::move(sidebar);

        provinces_obj[std::to_string(prov.h3_index)] = std::move(p);
    }
    root["provinces"] = std::move(provinces_obj);

    // --- Named features ---
    json features_arr = json::array();
    for (const auto& f : world.named_features) {
        json fj;
        fj["feature_id"] = std::to_string(f.id);
        fj["type"] = feature_type_str(f.type);
        fj["name"] = f.name;

        json provinces_list = json::array();
        for (auto h : f.extent) provinces_list.push_back(h);
        fj["provinces"] = std::move(provinces_list);

        // Headline fact.
        char buf[256];
        if (f.type == FeatureType::River && f.length_km > 0.0f) {
            std::snprintf(buf, sizeof(buf),
                "The %s stretches %.0f km through the region.",
                f.name.c_str(), f.length_km);
        } else if (f.type == FeatureType::MountainRange && f.peak_elevation_m > 0.0f) {
            std::snprintf(buf, sizeof(buf),
                "The %s reach %.0f metres at their highest.",
                f.name.c_str(), f.peak_elevation_m);
        } else {
            std::snprintf(buf, sizeof(buf),
                "The %s is a notable %s.",
                f.name.c_str(), feature_type_str(f.type));
        }
        fj["headline_fact"] = buf;
        fj["description"] = std::string("A ") + feature_type_str(f.type) +
                             " spanning " + std::to_string(f.extent.size()) +
                             " province(s).";
        fj["is_disputed"] = f.is_disputed;
        if (f.is_disputed) {
            fj["diplomatic_context_entry"] = {
                {"present", true},
                {"summary", "This feature spans a national border with competing claims."},
            };
        } else {
            fj["diplomatic_context_entry"] = {{"present", false}, {"summary", nullptr}};
        }
        features_arr.push_back(std::move(fj));
    }
    root["named_features"] = std::move(features_arr);

    // --- Pre-game events ---
    json pge_arr = json::array();
    for (size_t i = 0; i < world.pre_game_events.size(); ++i) {
        const auto& pge = world.pre_game_events[i];
        json ej;
        ej["event_id"] = std::to_string(world.world_seed) + "_pge_" + std::to_string(i);
        ej["type"] = pre_game_event_type_str(pge.type);
        ej["years_before_start"] = pge.years_before_start;
        ej["epicenter_province"] = pge.epicenter_province;
        json affected = json::array();
        for (auto h : pge.affected_provinces) affected.push_back(h);
        ej["affected_provinces"] = std::move(affected);
        ej["magnitude"] = pge.magnitude;
        ej["description"] = pge.description;
        ej["infrastructure_damage"] = pge.infrastructure_damage;
        ej["population_displacement"] = pge.population_displacement;
        ej["has_active_claim"] = pge.has_active_claim;
        ej["has_living_witnesses"] = pge.has_living_witnesses;
        pge_arr.push_back(std::move(ej));
    }
    root["pre_game_events"] = std::move(pge_arr);

    // --- Loading commentary ---
    {
        const auto& lc = world.loading_commentary;
        json lcj;
        lcj["stage_texts"] = {
            {"stage_1", lc.stage_1_text},
            {"stage_2", lc.stage_2_text},
            {"stage_3", lc.stage_3_text},
            {"stage_4", lc.stage_4_text},
            {"stage_5", lc.stage_5_text},
            {"stage_6", lc.stage_6_text},
            {"stage_7", lc.stage_7_text},
            {"stage_8", lc.stage_8_text},
            {"stage_9", lc.stage_9_text},
            {"stage_10", lc.stage_10_text},
            {"stage_11", lc.stage_11_text},
        };
        json facts = json::array();
        for (const auto& f : lc.sidebar_facts) facts.push_back(f);
        lcj["sidebar_facts"] = std::move(facts);
        root["loading_commentary"] = std::move(lcj);
    }

    // --- World statistics ---
    {
        json stats;
        stats["total_province_count"] = static_cast<int>(world.provinces.size());

        uint32_t habitable = 0, ocean = 0;
        float highest_trauma = 0.0f;
        H3Index highest_trauma_h3 = 0;
        std::unordered_map<std::string, uint32_t> archetype_counts;

        for (const auto& p : world.provinces) {
            if (p.settlement_attractiveness > 0.15f) ++habitable;
            if (p.geography.coastal_length_km > 100.0f &&
                p.geography.is_landlocked == false &&
                p.settlement_attractiveness < 0.05f) {
                ++ocean;
            }
            if (p.historical_trauma_index > highest_trauma) {
                highest_trauma = p.historical_trauma_index;
                highest_trauma_h3 = p.h3_index;
            }
            archetype_counts[p.history.province_archetype_label]++;
        }

        stats["habitable_province_count"] = habitable;
        stats["ocean_province_count"] = ocean;
        stats["total_named_features"] = static_cast<int>(world.named_features.size());
        stats["total_pre_game_events"] = static_cast<int>(world.pre_game_events.size());

        // Most common archetype.
        std::string most_common;
        uint32_t max_count = 0;
        for (const auto& [label, count] : archetype_counts) {
            if (count > max_count) { max_count = count; most_common = label; }
        }
        stats["most_common_archetype"] = most_common;
        stats["highest_trauma_province"] = highest_trauma_h3;

        // Largest river and desert.
        float largest_river = 0.0f;
        float largest_desert = 0.0f;
        for (const auto& f : world.named_features) {
            if (f.type == FeatureType::River && f.length_km > largest_river)
                largest_river = f.length_km;
            if (f.type == FeatureType::Desert && f.area_km2 > largest_desert)
                largest_desert = f.area_km2;
        }
        stats["largest_named_river_length_km"] = largest_river;
        stats["largest_named_desert_area_km2"] = largest_desert;

        root["world_statistics"] = std::move(stats);
    }

    return root;
}

void WorldGenerator::write_encyclopedia_json(const WorldState& world,
                                               const WorldGeneratorConfig& config,
                                               const std::string& path) {
    auto j = to_encyclopedia_json(world, config);
    std::ofstream out(path);
    if (!out.is_open()) return;
    out << j.dump(2);
}

// ===========================================================================
// Stage 10 — World commentary generation
// ===========================================================================

void WorldGenerator::generate_commentary(WorldState& world, DeterministicRNG& rng) {
    // Geological origin sentences indexed by TectonicContext (2 variants for variety).
    static const char* geo_sentences[][2] = {
        // Subduction (0)
        {"Formed at a convergent plate margin where oceanic crust plunges beneath the "
         "continent, this province sits on volcanic arc terrain studded with porphyry "
         "copper and epithermal gold systems that drew prospectors inland for centuries.",
         "Millennia of volcanic activity along a subduction zone have built rugged terrain "
         "and deposited the metal-rich hydrothermal veins that first attracted settlement "
         "to the highland mineral districts."},
        // ContinentalCollision (1)
        {"Ancient continental collision has folded and buckled this province into a series "
         "of limestone ridges and metamorphic massifs, their peaks still rising slowly under "
         "ongoing tectonic compression.",
         "Where two continents collided over two hundred million years ago, the resulting "
         "mountain belt left behind marble outcrops, gem-bearing schists, and terrain that "
         "challenged every infrastructure project attempted here."},
        // RiftZone (2)
        {"The province occupies an active rift valley where the crust is slowly pulling "
         "apart, allowing deep geothermal heat to rise toward the surface through networks "
         "of fault-controlled volcanic vents and mineral springs.",
         "Sitting astride a continental rift system, this province's elongated valley floor "
         "and escarpment walls are the surface expression of crust being stretched toward "
         "eventual separation by diverging plate motion."},
        // TransformFault (3)
        {"A major transform fault runs through this province, its linear valleys and offset "
         "ridgelines recording millions of years of crustal blocks sliding past one another "
         "in a geology that prioritizes seismic preparation over architectural ambition.",
         "The province lies along an active strike-slip fault zone whose frequent earthquakes "
         "have shaped both the terrain and the strict building standards that govern every "
         "major structure constructed here."},
        // HotSpot (4)
        {"A mantle hot spot has driven repeated volcanic episodes through this province, "
         "building a basaltic plateau whose geothermal heat potential remains largely "
         "untapped by modern energy infrastructure.",
         "The province owes its landforms to a plume of upwelling mantle material that "
         "punched through the overlying plate, leaving a trail of volcanic landforms, "
         "sulfur deposits, and geothermal fields."},
        // PassiveMargin (5)
        {"Occupying an ancient passive margin far removed from any active plate boundary, "
         "the province's gently shelving coastal plain and wide continental shelf have "
         "accumulated thick sedimentary sequences over hundreds of millions of years.",
         "This province sits on old rifted crust that has cooled and subsided over geological "
         "time, its sedimentary layers now hosting the offshore petroleum and natural gas "
         "deposits that underpin the regional energy sector."},
        // CratonInterior (6)
        {"This province rests on one of the region's most ancient geological formations — "
         "shield rock of granites and gneisses that has resisted major deformation for "
         "billions of years while surrounding territories were repeatedly folded and faulted.",
         "The ancient crystalline basement beneath this province has remained largely stable "
         "since the Precambrian, its exposed outcrops and thin soils testifying to immense "
         "erosive time operating on terrain that was once a towering mountain range."},
        // SedimentaryBasin (7)
        {"A broad interior sedimentary basin underlies the province, its flat terrain the "
         "product of millions of years of river and lacustrine deposition above a slowly "
         "subsiding crustal foundation.",
         "The province occupies a subsided interior basin where ancient swamp environments "
         "left behind thick coal seams and source rock for the petroleum deposits that later "
         "exploration confirmed at depth across the region."},
    };

    // Climate context sentences by Köppen zone.
    auto climate_sentence = [](KoppenZone z) -> const char* {
        switch (z) {
            case KoppenZone::Af:
            case KoppenZone::Am:
            case KoppenZone::Aw:
                return "A tropical climate sustains dense natural vegetation and intensive "
                       "year-round agriculture, though seasonal flooding and waterborne disease "
                       "remain persistent challenges for provincial health infrastructure.";
            case KoppenZone::BWh:
            case KoppenZone::BWk:
                return "Hyperarid conditions concentrate settlement and agriculture around the "
                       "few reliable water sources, making water infrastructure the single most "
                       "consequential investment in provincial development.";
            case KoppenZone::BSh:
            case KoppenZone::BSk:
                return "Semi-arid conditions limit reliable agriculture to drought-tolerant "
                       "crops and irrigated river valleys, while pastoralism and extractive "
                       "industries have historically dominated the provincial economy.";
            case KoppenZone::Cfa:
            case KoppenZone::Cwa:
                return "A humid subtropical climate with warm summers and mild winters has "
                       "historically supported productive mixed farming and a dense rural "
                       "population, though summer heat waves present ongoing challenges.";
            case KoppenZone::Cfb:
            case KoppenZone::Cfc:
                return "The temperate oceanic climate — mild, reliable, and well-watered "
                       "year-round — has proven one of the most hospitable environments for "
                       "agriculture, dense settlement, and sustained industrial development.";
            case KoppenZone::Csa:
            case KoppenZone::Csb:
                return "A Mediterranean climate with dry summers and mild wet winters "
                       "historically supported olive cultivation, viticulture, and maritime "
                       "commerce, attracting early settlement to the coastal lowlands.";
            case KoppenZone::Dfa:
            case KoppenZone::Dfb:
                return "Continental summers warm enough for grain cultivation alternate with "
                       "long winters that have historically directed capital toward "
                       "cold-climate industries and heated infrastructure.";
            case KoppenZone::Dfc:
            case KoppenZone::Dfd:
                return "Subarctic conditions limit agriculture to a brief summer window and "
                       "concentrate economic activity in extractive industries tolerant of "
                       "extreme winter operating conditions.";
            case KoppenZone::ET:
            case KoppenZone::EF:
                return "Polar conditions restrict agricultural activity almost entirely, "
                       "making the province structurally dependent on resource extraction "
                       "industries and external food supply chains.";
            default:
                return "The temperate climate supports moderate agricultural productivity "
                       "and year-round habitability for the provincial population.";
        }
    };

    // Economic character sentences indexed by province_archetype_index.
    static const char* econ_sentences[] = {
        // 0: industrial_hub
        "Today the province is the nation's manufacturing heartland, its industrial "
        "districts converting raw material inputs into finished goods for both domestic "
        "consumption and export markets.",
        // 1: agricultural
        "The province has long been the agricultural breadbasket of the nation, its deep "
        "soils and reliable rainfall sustaining grain, oilseed, and livestock operations "
        "at a scale that feeds neighboring urban provinces.",
        // 2: resource_rich
        "Extraction industries define the provincial economy, with mining and energy "
        "operations providing the raw material flows that feed the nation's industrial "
        "capacity and generating substantial royalties for the provincial government.",
        // 3: coastal_trade
        "Maritime commerce has defined the province for generations, its port facilities "
        "and warehousing districts connecting regional producers to international shipping "
        "lanes and serving as the nation's principal import entry point.",
        // 4: financial_center
        "The province has evolved into the nation's financial and professional services "
        "hub, its educated workforce, deep capital markets, and established legal "
        "institutions attracting corporate headquarters and investment from across the country.",
        // 5: mixed_economy
        "A diversified provincial economy balancing manufacturing, agriculture, and "
        "services has given the region resilience against commodity price cycles and "
        "positioned it as a reliable supplier across multiple sectors.",
    };

    for (auto& prov : world.provinces) {
        uint8_t ctx_idx =
            static_cast<uint8_t>(prov.tectonic_context) < 8
                ? static_cast<uint8_t>(prov.tectonic_context)
                : 6;  // fallback to CratonInterior
        uint8_t variant = rng.next_uint(2);

        const char* geo    = geo_sentences[ctx_idx][variant];
        const char* climate = climate_sentence(prov.climate.koppen_zone);
        const char* econ   = econ_sentences[prov.province_archetype_index < 6
                                                ? prov.province_archetype_index
                                                : 5];

        std::string lore = std::string(geo) + " " + climate + " " + econ;

        // Hydrology commentary (Stage 3 features).
        if (prov.geography.is_delta) {
            lore += " The province encompasses a major river delta whose fertile alluvial "
                    "soils and extensive waterway network have supported dense agricultural "
                    "settlement since antiquity, though seasonal flooding remains a constant "
                    "challenge for infrastructure.";
        } else if (prov.geography.is_oasis) {
            lore += " In an otherwise inhospitable desert landscape, artesian springs "
                    "sustain a cluster of settlements whose existence depends entirely on "
                    "ancient groundwater reserves recharged by distant mountain precipitation.";
        } else if (prov.geography.snowmelt_fed) {
            lore += " The province's rivers are sustained by snowmelt from distant mountain "
                    "ranges, providing reliable irrigation water through the growing season "
                    "even as local rainfall remains sparse.";
        } else if (prov.geography.is_endorheic) {
            lore += " Situated in a closed drainage basin with no outlet to the sea, the "
                    "province's inland waters have concentrated mineral salts over millennia, "
                    "creating economically significant brine deposits.";
        }

        if (prov.has_estuary) {
            lore += " A sheltered estuary where river meets tide provides natural harbour "
                    "conditions and productive fisheries.";
        } else if (prov.has_ria_coast) {
            lore += " Drowned river valleys along the coast create a series of natural "
                    "deep-water harbours ideal for maritime commerce.";
        }

        if (prov.has_impact_crater) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                " A circular geological formation %d km across marks the site of an ancient "
                "asteroid impact whose hydrothermal aftermath concentrated platinum group metals "
                "and nickel sulfides in the surrounding rock.",
                static_cast<int>(prov.impact_crater_diameter_km));
            lore += buf;
        }

        if (prov.has_badlands) {
            lore += " Arid erosion has carved the soft sedimentary rock into a spectacular "
                    "network of ravines, hoodoos, and exposed geological strata — terrain of "
                    "scientific and archaeological interest but near-zero agricultural value.";
        }

        if (prov.has_loess) {
            lore += " Windblown glacial silt deposited over millennia has created deep loess "
                    "soils of exceptional fertility, the foundation of the province's "
                    "agricultural abundance.";
        }

        if (prov.is_glacial_scoured) {
            lore += " Ancient ice sheets stripped the bedrock clean, leaving a landscape of "
                    "thin soils dotted with thousands of lakes — poor for farming but rich in "
                    "freshwater, timber, and exposed ancient mineral formations.";
        }

        prov.province_lore = std::move(lore);
    }
}

// ===========================================================================
// Stage 11 — World JSON output (world.json serialization)
// ===========================================================================

// Enum-to-string helpers (inline anonymous namespace).
namespace {

static const char* tectonic_context_str(TectonicContext tc) {
    switch (tc) {
        case TectonicContext::Subduction:          return "Subduction";
        case TectonicContext::ContinentalCollision: return "ContinentalCollision";
        case TectonicContext::RiftZone:            return "RiftZone";
        case TectonicContext::TransformFault:      return "TransformFault";
        case TectonicContext::HotSpot:             return "HotSpot";
        case TectonicContext::PassiveMargin:       return "PassiveMargin";
        case TectonicContext::CratonInterior:      return "CratonInterior";
        case TectonicContext::SedimentaryBasin:    return "SedimentaryBasin";
    }
    return "Unknown";
}

static const char* rock_type_str(RockType rt) {
    switch (rt) {
        case RockType::Igneous:     return "Igneous";
        case RockType::Sedimentary: return "Sedimentary";
        case RockType::Metamorphic: return "Metamorphic";
        case RockType::Mixed:       return "Mixed";
    }
    return "Unknown";
}

static const char* geology_type_str(GeologyType gt) {
    switch (gt) {
        case GeologyType::VolcanicArc:        return "VolcanicArc";
        case GeologyType::GraniteShield:      return "GraniteShield";
        case GeologyType::GreenstoneBelt:     return "GreenstoneBelt";
        case GeologyType::SedimentarySequence: return "SedimentarySequence";
        case GeologyType::CarbonateSequence:  return "CarbonateSequence";
        case GeologyType::MetamorphicCore:    return "MetamorphicCore";
        case GeologyType::BasalticPlateau:    return "BasalticPlateau";
        case GeologyType::AlluvialFill:       return "AlluvialFill";
    }
    return "Unknown";
}

static const char* koppen_zone_str(KoppenZone kz) {
    switch (kz) {
        case KoppenZone::Af:  return "Af";
        case KoppenZone::Am:  return "Am";
        case KoppenZone::Aw:  return "Aw";
        case KoppenZone::BWh: return "BWh";
        case KoppenZone::BWk: return "BWk";
        case KoppenZone::BSh: return "BSh";
        case KoppenZone::BSk: return "BSk";
        case KoppenZone::Cfa: return "Cfa";
        case KoppenZone::Cfb: return "Cfb";
        case KoppenZone::Cfc: return "Cfc";
        case KoppenZone::Csa: return "Csa";
        case KoppenZone::Csb: return "Csb";
        case KoppenZone::Cwa: return "Cwa";
        case KoppenZone::Dfa: return "Dfa";
        case KoppenZone::Dfb: return "Dfb";
        case KoppenZone::Dfc: return "Dfc";
        case KoppenZone::Dfd: return "Dfd";
        case KoppenZone::ET:  return "ET";
        case KoppenZone::EF:  return "EF";
    }
    return "Unknown";
}

static const char* resource_type_str(ResourceType rt) {
    switch (rt) {
        case ResourceType::IronOre:        return "IronOre";
        case ResourceType::Copper:         return "Copper";
        case ResourceType::Bauxite:        return "Bauxite";
        case ResourceType::Lithium:        return "Lithium";
        case ResourceType::Coal:           return "Coal";
        case ResourceType::CrudeOil:       return "CrudeOil";
        case ResourceType::NaturalGas:     return "NaturalGas";
        case ResourceType::LimestoneSilica: return "LimestoneSilica";
        case ResourceType::Wheat:          return "Wheat";
        case ResourceType::Corn:           return "Corn";
        case ResourceType::Soybeans:       return "Soybeans";
        case ResourceType::Cotton:         return "Cotton";
        case ResourceType::Timber:         return "Timber";
        case ResourceType::Fish:           return "Fish";
        case ResourceType::SolarPotential: return "SolarPotential";
        case ResourceType::WindPotential:  return "WindPotential";
        case ResourceType::Gold:           return "Gold";
        case ResourceType::Geothermal:     return "Geothermal";
        case ResourceType::Uranium:        return "Uranium";
        case ResourceType::Potash:         return "Potash";
        case ResourceType::Sand:           return "Sand";
        case ResourceType::Aggregate:      return "Aggregate";
        case ResourceType::PlatinumGroupMetals: return "PlatinumGroupMetals";
    }
    return "Unknown";
}

static const char* fishing_access_str(FishingAccessType fa) {
    switch (fa) {
        case FishingAccessType::NoAccess:   return "None";
        case FishingAccessType::Inshore:    return "Inshore";
        case FishingAccessType::Offshore:   return "Offshore";
        case FishingAccessType::Pelagic:    return "Pelagic";
        case FishingAccessType::Freshwater: return "Freshwater";
        case FishingAccessType::Upwelling:  return "Upwelling";
    }
    return "Unknown";
}

static const char* soil_type_str(SoilType st) {
    switch (st) {
        case SoilType::Mollisol:  return "Mollisol";
        case SoilType::Oxisol:    return "Oxisol";
        case SoilType::Aridisol:  return "Aridisol";
        case SoilType::Vertisol:  return "Vertisol";
        case SoilType::Spodosol:  return "Spodosol";
        case SoilType::Histosol:  return "Histosol";
        case SoilType::Alluvial:  return "Alluvial";
        case SoilType::Andisol:   return "Andisol";
        case SoilType::Cryosol:   return "Cryosol";
        case SoilType::Entisol:   return "Entisol";
    }
    return "Unknown";
}

static const char* river_flow_regime_str(RiverFlowRegime rfr) {
    switch (rfr) {
        case RiverFlowRegime::RainfedPerennial:   return "RainfedPerennial";
        case RiverFlowRegime::SnowmeltPerennial:  return "SnowmeltPerennial";
        case RiverFlowRegime::SnowmeltEphemeral:  return "SnowmeltEphemeral";
        case RiverFlowRegime::RainfedEphemeral:   return "RainfedEphemeral";
        case RiverFlowRegime::Glacierfed:         return "Glacierfed";
        case RiverFlowRegime::None:               return "None";
    }
    return "Unknown";
}

static const char* link_type_str(LinkType lt) {
    switch (lt) {
        case LinkType::Land:     return "Land";
        case LinkType::Maritime: return "Maritime";
        case LinkType::River:    return "River";
    }
    return "Unknown";
}

static const char* lod_str(SimulationLOD lod) {
    switch (lod) {
        case SimulationLOD::full:        return "full";
        case SimulationLOD::simplified:  return "simplified";
        case SimulationLOD::statistical: return "statistical";
    }
    return "unknown";
}

static const char* government_type_str(GovernmentType gt) {
    switch (gt) {
        case GovernmentType::Democracy:    return "Democracy";
        case GovernmentType::Autocracy:    return "Autocracy";
        case GovernmentType::Federation:   return "Federation";
        case GovernmentType::FailedState:  return "FailedState";
    }
    return "Unknown";
}

static const char* archetype_str(uint8_t idx) {
    static const char* names[] = {
        "industrial_hub", "agricultural", "resource_rich",
        "coastal_trade", "financial_center", "mixed_economy",
    };
    return idx < 6 ? names[idx] : "mixed_economy";
}

}  // anonymous namespace

nlohmann::json WorldGenerator::to_world_json(const WorldState& world) {
    using json = nlohmann::json;
    json root;

    root["schema_version"] = world.current_schema_version;
    root["world_seed"] = world.world_seed;
    root["current_tick"] = world.current_tick;

    // --- Nations ---
    json nations_arr = json::array();
    for (const auto& nation : world.nations) {
        json n;
        n["id"] = nation.id;
        n["name"] = nation.name;
        n["currency_code"] = nation.currency_code;
        n["government_type"] = government_type_str(nation.government_type);
        n["province_ids"] = nation.province_ids;
        n["corporate_tax_rate"] = nation.corporate_tax_rate;
        n["income_tax_rate_top_bracket"] = nation.income_tax_rate_top_bracket;
        n["trade_balance_fraction"] = nation.trade_balance_fraction;
        n["inflation_rate"] = nation.inflation_rate;
        n["credit_rating"] = nation.credit_rating;
        n["political_cycle"] = {
            {"current_administration_tick", nation.political_cycle.current_administration_tick},
            {"national_approval", nation.political_cycle.national_approval},
            {"election_campaign_active", nation.political_cycle.election_campaign_active},
            {"next_election_tick", nation.political_cycle.next_election_tick},
        };
        // WorldGen v0.18 §9.5 fields
        n["capital_province_id"] = nation.capital_province_id;
        n["language_family_id"] = nation.language_family_id;
        if (!nation.secondary_language_id.empty()) {
            n["secondary_language_id"] = nation.secondary_language_id;
        }
        n["gdp_index"] = nation.gdp_index;
        n["governance_quality"] = nation.governance_quality;
        n["size_class"] = nation_size_str(nation.size_class);
        n["is_colonial_power"] = nation.is_colonial_power;

        nations_arr.push_back(std::move(n));
    }
    root["nations"] = std::move(nations_arr);

    // --- Provinces ---
    json provinces_arr = json::array();
    for (const auto& prov : world.provinces) {
        json p;
        p["id"] = prov.id;
        p["h3_index"] = prov.h3_index;
        p["is_pentagon"] = prov.is_pentagon;
        p["neighbor_count"] = prov.neighbor_count;
        p["fictional_name"] = prov.fictional_name;
        p["archetype"] = archetype_str(prov.province_archetype_index);
        p["province_archetype_index"] = prov.province_archetype_index;
        p["region_id"] = prov.region_id;
        p["nation_id"] = prov.nation_id;
        p["lod_level"] = lod_str(prov.lod_level);

        // Geography
        p["geography"] = {
            {"latitude", prov.geography.latitude},
            {"longitude", prov.geography.longitude},
            {"elevation_avg_m", prov.geography.elevation_avg_m},
            {"terrain_roughness", prov.geography.terrain_roughness},
            {"forest_coverage", prov.geography.forest_coverage},
            {"arable_land_fraction", prov.geography.arable_land_fraction},
            {"coastal_length_km", prov.geography.coastal_length_km},
            {"is_landlocked", prov.geography.is_landlocked},
            {"port_capacity", prov.geography.port_capacity},
            {"river_access", prov.geography.river_access},
            {"area_km2", prov.geography.area_km2},
            // Hydrology (Stage 3)
            {"is_endorheic", prov.geography.is_endorheic},
            {"is_delta", prov.geography.is_delta},
            {"snowmelt_fed", prov.geography.snowmelt_fed},
            {"has_alluvial_fan", prov.geography.has_alluvial_fan},
            {"has_artesian_spring", prov.geography.has_artesian_spring},
            {"is_oasis", prov.geography.is_oasis},
            {"groundwater_reserve", prov.geography.groundwater_reserve},
            {"snowpack_contribution", prov.geography.snowpack_contribution},
            {"spring_flow_index", prov.geography.spring_flow_index},
            {"river_flow_regime", river_flow_regime_str(prov.geography.river_flow_regime)},
        };

        // Climate
        p["climate"] = {
            {"koppen_zone", koppen_zone_str(prov.climate.koppen_zone)},
            {"temperature_avg_c", prov.climate.temperature_avg_c},
            {"temperature_min_c", prov.climate.temperature_min_c},
            {"temperature_max_c", prov.climate.temperature_max_c},
            {"precipitation_mm", prov.climate.precipitation_mm},
            {"precipitation_seasonality", prov.climate.precipitation_seasonality},
            {"drought_vulnerability", prov.climate.drought_vulnerability},
            {"flood_vulnerability", prov.climate.flood_vulnerability},
            {"wildfire_vulnerability", prov.climate.wildfire_vulnerability},
            {"climate_stress_current", prov.climate.climate_stress_current},
            // Atmosphere (Stage 4)
            {"continentality", prov.climate.continentality},
            {"enso_susceptibility", prov.climate.enso_susceptibility},
            {"geographic_vulnerability", prov.climate.geographic_vulnerability},
            {"cold_current_adjacent", prov.climate.cold_current_adjacent},
            {"is_monsoon", prov.climate.is_monsoon},
        };

        // Tectonic geology
        p["tectonic"] = {
            {"tectonic_context", tectonic_context_str(prov.tectonic_context)},
            {"rock_type", rock_type_str(prov.rock_type)},
            {"geology_type", geology_type_str(prov.geology_type)},
            {"tectonic_stress", prov.tectonic_stress},
            {"plate_age", prov.plate_age},
        };

        // Terrain flags and special features
        p["is_mountain_pass"] = prov.is_mountain_pass;
        p["island_isolation"] = prov.island_isolation;
        p["has_permafrost"] = prov.has_permafrost;
        p["has_fjord"] = prov.has_fjord;
        p["has_karst"] = prov.has_karst;
        p["has_estuary"] = prov.has_estuary;
        p["has_ria_coast"] = prov.has_ria_coast;
        p["is_atoll"] = prov.is_atoll;
        p["has_badlands"] = prov.has_badlands;
        p["facility_concealment_bonus"] = prov.facility_concealment_bonus;
        p["has_impact_crater"] = prov.has_impact_crater;
        if (prov.has_impact_crater) {
            p["impact_crater_diameter_km"] = prov.impact_crater_diameter_km;
            p["impact_mineral_signal"] = prov.impact_mineral_signal;
        }
        p["has_loess"] = prov.has_loess;
        p["is_glacial_scoured"] = prov.is_glacial_scoured;
        p["is_salt_flat"] = prov.is_salt_flat;

        // Fisheries (Stage 6)
        if (prov.fisheries.access_type != FishingAccessType::NoAccess) {
            p["fisheries"] = {
                {"access_type", fishing_access_str(prov.fisheries.access_type)},
                {"carrying_capacity", prov.fisheries.carrying_capacity},
                {"current_stock", prov.fisheries.current_stock},
                {"max_sustainable_yield", prov.fisheries.max_sustainable_yield},
                {"intrinsic_growth_rate", prov.fisheries.intrinsic_growth_rate},
                {"seasonal_closure", prov.fisheries.seasonal_closure},
                {"is_migratory", prov.fisheries.is_migratory},
            };
        }

        // Soil & irrigation (Stage 5)
        p["soil_type"] = soil_type_str(prov.soil_type);
        p["irrigation_potential"] = prov.irrigation_potential;
        p["irrigation_cost_index"] = prov.irrigation_cost_index;
        p["salinisation_risk"] = prov.salinisation_risk;
        p["water_availability"] = prov.water_availability;

        // Settlement (Stage 9)
        p["settlement_attractiveness"] = prov.settlement_attractiveness;
        p["disease_burden"] = prov.disease_burden;

        // Nation formation (Stage 9.5)
        p["border_change_count"] = prov.border_change_count;
        p["infra_gap"] = prov.infra_gap;
        p["has_colonial_development_event"] = prov.has_colonial_development_event;

        // Nomadic population (Stage 9.6)
        p["nomadic_population_fraction"] = prov.nomadic_population_fraction;
        p["pastoral_carrying_capacity"] = prov.pastoral_carrying_capacity;

        // Capital (Stage 9.7)
        p["is_nation_capital"] = prov.is_nation_capital;

        // Resource deposits
        json deposits_arr = json::array();
        for (const auto& d : prov.deposits) {
            deposits_arr.push_back({
                {"id", d.id},
                {"type", resource_type_str(d.type)},
                {"quantity", d.quantity},
                {"quality", d.quality},
                {"depth", d.depth},
                {"accessibility", d.accessibility},
                {"depletion_rate", d.depletion_rate},
                {"quantity_remaining", d.quantity_remaining},
                {"era_unlock", d.era_unlock},
            });
        }
        p["deposits"] = std::move(deposits_arr);

        // Demographics
        p["demographics"] = {
            {"total_population", prov.demographics.total_population},
            {"median_age", prov.demographics.median_age},
            {"education_level", prov.demographics.education_level},
            {"income_low_fraction", prov.demographics.income_low_fraction},
            {"income_middle_fraction", prov.demographics.income_middle_fraction},
            {"income_high_fraction", prov.demographics.income_high_fraction},
            {"political_lean", prov.demographics.political_lean},
        };

        // Economy
        p["infrastructure_rating"] = prov.infrastructure_rating;
        p["agricultural_productivity"] = prov.agricultural_productivity;
        p["energy_cost_baseline"] = prov.energy_cost_baseline;
        p["trade_openness"] = prov.trade_openness;
        p["historical_trauma_index"] = prov.historical_trauma_index;

        // Community
        p["community"] = {
            {"cohesion", prov.community.cohesion},
            {"grievance_level", prov.community.grievance_level},
            {"institutional_trust", prov.community.institutional_trust},
            {"resource_access", prov.community.resource_access},
            {"response_stage", prov.community.response_stage},
        };

        // Political
        p["political"] = {
            {"governing_office_id", prov.political.governing_office_id},
            {"approval_rating", prov.political.approval_rating},
            {"election_due_tick", prov.political.election_due_tick},
            {"corruption_index", prov.political.corruption_index},
        };

        // Conditions
        p["conditions"] = {
            {"stability_score", prov.conditions.stability_score},
            {"inequality_index", prov.conditions.inequality_index},
            {"crime_rate", prov.conditions.crime_rate},
            {"addiction_rate", prov.conditions.addiction_rate},
            {"criminal_dominance_index", prov.conditions.criminal_dominance_index},
            {"formal_employment_rate", prov.conditions.formal_employment_rate},
            {"regulatory_compliance_index", prov.conditions.regulatory_compliance_index},
            {"drought_modifier", prov.conditions.drought_modifier},
            {"flood_modifier", prov.conditions.flood_modifier},
        };

        // Province links
        json links_arr = json::array();
        for (const auto& link : prov.links) {
            links_arr.push_back({
                {"neighbor_h3", link.neighbor_h3},
                {"type", link_type_str(link.type)},
                {"shared_border_km", link.shared_border_km},
                {"transit_terrain_cost", link.transit_terrain_cost},
                {"infrastructure_bonus", link.infrastructure_bonus},
            });
        }
        p["links"] = std::move(links_arr);

        // Commentary
        if (!prov.province_lore.empty()) {
            p["province_lore"] = prov.province_lore;
        }

        // Province history (Stage 10.2)
        {
            json hist;
            hist["province_archetype_label"] = prov.history.province_archetype_label;
            hist["current_character"] = prov.history.current_character;
            hist["summary"] = prov.history.summary;
            json events_arr = json::array();
            for (const auto& e : prov.history.events) {
                events_arr.push_back({
                    {"type", historical_event_type_str(e.type)},
                    {"years_before_game_start", e.years_before_game_start},
                    {"headline", e.headline},
                    {"magnitude", e.magnitude},
                    {"lasting_effect", e.lasting_effect},
                    {"has_living_memory", e.has_living_memory},
                });
            }
            hist["events"] = std::move(events_arr);
            p["history"] = std::move(hist);
        }

        provinces_arr.push_back(std::move(p));
    }
    root["provinces"] = std::move(provinces_arr);

    // --- Regions ---
    json regions_arr = json::array();
    for (const auto& region : world.region_groups) {
        regions_arr.push_back({
            {"id", region.id},
            {"fictional_name", region.fictional_name},
            {"nation_id", region.nation_id},
            {"province_ids", region.province_ids},
        });
    }
    root["regions"] = std::move(regions_arr);

    // --- Named Features (Stage 10.1) ---
    json features_arr = json::array();
    for (const auto& f : world.named_features) {
        json fj;
        fj["id"] = f.id;
        fj["type"] = feature_type_str(f.type);
        fj["name"] = f.name;
        if (!f.local_name.empty()) fj["local_name"] = f.local_name;
        fj["language_family_id"] = f.language_family_id;
        fj["significance"] = f.significance;
        fj["extent"] = f.extent;
        if (f.length_km > 0.0f) fj["length_km"] = f.length_km;
        if (f.area_km2 > 0.0f) fj["area_km2"] = f.area_km2;
        if (f.peak_elevation_m > 0.0f) fj["peak_elevation_m"] = f.peak_elevation_m;
        fj["is_navigable"] = f.is_navigable;
        fj["is_disputed"] = f.is_disputed;
        fj["is_chokepoint"] = f.is_chokepoint;
        features_arr.push_back(std::move(fj));
    }
    root["named_features"] = std::move(features_arr);

    return root;
}

void WorldGenerator::write_world_json(const WorldState& world, const std::string& path) {
    auto j = to_world_json(world);
    std::ofstream out(path);
    if (!out.is_open()) return;
    out << j.dump(2);
}

}  // namespace econlife
