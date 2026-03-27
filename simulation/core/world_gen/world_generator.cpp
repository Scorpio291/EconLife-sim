// WorldGenerator — V1 world generation implementation.
// See world_generator.h for class documentation.
//
// Produces economically diverse, deterministic worlds from seed + CSV data.
// Province archetypes drive geographic, economic, and demographic variation.
// All random draws go through DeterministicRNG for full reproducibility.

#include "core/world_gen/world_generator.h"

#include <algorithm>
#include <cmath>

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

    // Step 1: Nation
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

    // Step 3b2: Stage 4a — Province geography refinement (WorldGen v0.18).
    // Scales elevation from terrain_roughness and applies elevation lapse rate to
    // temperature. MUST run before detect_terrain_flags so mountain pass detection
    // uses physically consistent elevation values.
    refine_province_geography(world);

    // Step 3c: Stage 2 derived — Terrain flag detection (WorldGen v0.18).
    // Detects mountain passes (chokepoints) and island isolation.
    // Runs after refine_province_geography() so corrected elevation is used.
    detect_terrain_flags(world);

    // Step 3d: Stage 5+6 — Soils and Biomes (WorldGen v0.18).
    // Adjusts agricultural_productivity from geology_type + climate soil fertility model.
    // Refines forest_coverage, drought/flood vulnerability from KoppenZone.
    // Runs after generate_plates() (geology_type available) and apply_archetype()
    // (KoppenZone and baseline agricultural_productivity set).
    derive_soils_and_biomes(world, rng);

    // Step 3e: Stage 7 — Special terrain features (WorldGen v0.18).
    // Sets has_permafrost, has_fjord. Locks CrudeOil/NaturalGas accessibility for
    // permafrost provinces. Applies fjord transit cost to Maritime links.
    // Runs after derive_soils_and_biomes() (permafrost reduces ag_productivity further).
    detect_special_features(world);

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

    // Step 3f: Stage 4b — Economic geography seeding (WorldGen v0.18).
    // Derives trade_openness for all province archetypes (fixes the bug where only
    // coastal_trade sets it). Refines wildfire_vulnerability from KoppenZone. Adjusts
    // formal_employment_rate for infrastructure and corruption. MUST run after
    // detect_special_features() so island_isolation flag is available.
    seed_economic_geography(world);

    // Step 7b: Stage 9 — Population attractiveness (WorldGen v0.18).
    // Adjusts total_population from geology/climate attractiveness score (bounded ±40%).
    // Runs after detect_special_features() so permafrost and island flags are set.
    seed_population_attractiveness(world, rng);

    // Step 7c: Stage 10 — World commentary (WorldGen v0.18).
    // Generates province_lore strings from tectonic context, climate, and archetype.
    // Runs last so all province fields are populated.
    generate_commentary(world, rng);

    // Step 8: Technology — load catalog and seed initial holdings.
    TechnologyCatalog tech_catalog;
    if (!config.technology_directory.empty()) {
        std::string nodes_path = config.technology_directory + "/technology_nodes.csv";
        std::string ceilings_path = config.technology_directory + "/maturation_ceilings.csv";
        tech_catalog.load_nodes_csv(nodes_path);
        tech_catalog.load_ceilings_csv(ceilings_path);
    }
    seed_technology(world, rng, tech_catalog, config);

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

    auto add_deposit = [&](ResourceType type, float qty_base, float quality_base) {
        ResourceDeposit d{};
        d.id = deposit_id_base++;
        d.type = type;
        d.quantity = qty_base * richness * (0.7f + rng.next_float() * 0.6f);
        d.quality = std::min(1.0f, quality_base + rng.next_float() * 0.3f);
        d.depth = rng.next_float() * 0.8f;
        d.accessibility = 0.3f + rng.next_float() * 0.5f;
        d.depletion_rate = 0.0001f + rng.next_float() * 0.0002f;
        d.quantity_remaining = d.quantity;
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

    auto add = [&](ResourceType type, float qty_base, float quality_base) {
        ResourceDeposit d{};
        d.id = deposit_id_base++;
        d.type = type;
        d.quantity = qty_base * richness * (0.7f + rng.next_float() * 0.6f);
        d.quality = std::min(1.0f, quality_base + rng.next_float() * 0.25f);
        d.depth = 0.2f + rng.next_float() * 0.6f;
        d.accessibility = 0.3f + rng.next_float() * 0.5f;
        d.depletion_rate = 0.0001f + rng.next_float() * 0.0002f;
        d.quantity_remaining = d.quantity;
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
// Stage 2 derived — Terrain flag detection
// ===========================================================================

void WorldGenerator::detect_terrain_flags(WorldState& world) {
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
        if (prov.geography.terrain_roughness > 0.65f &&
            prov.geography.elevation_avg_m > 350.0f) {
            float my_elev = prov.geography.elevation_avg_m;
            uint32_t low_neighbor_count = 0;

            for (const auto& link : prov.links) {
                auto it = world.h3_province_map.find(link.neighbor_h3);
                if (it == world.h3_province_map.end()) continue;
                const auto& nb = world.provinces[it->second];
                // "Low" neighbor: elevation < 60% of this province's elevation.
                if (nb.geography.elevation_avg_m < my_elev * 0.60f) {
                    ++low_neighbor_count;
                }
            }

            // A mountain pass connects at least 2 different lower areas.
            if (low_neighbor_count >= 2) {
                prov.is_mountain_pass = true;
                // Reduce transit cost on pass routes by 40% relative to the full mountain cost.
                // Pass routes are still expensive (mountain terrain), just less so than
                // going through the range at a non-pass point.
                for (auto& link : prov.links) {
                    auto it = world.h3_province_map.find(link.neighbor_h3);
                    if (it == world.h3_province_map.end()) continue;
                    const auto& nb = world.provinces[it->second];
                    if (nb.geography.elevation_avg_m < my_elev * 0.60f) {
                        link.transit_terrain_cost = std::max(0.10f, link.transit_terrain_cost * 0.60f);
                    }
                }
            }
        }
    }
}

// ===========================================================================
// Stage 4a — Province geography refinement (elevation + temperature lapse rate)
// ===========================================================================

void WorldGenerator::refine_province_geography(WorldState& world) {
    for (auto& prov : world.provinces) {
        // ---- Elevation-roughness correlation ----
        // apply_archetype seeds a random elevation in [50, 850] m uniformly.
        // Mountainous provinces (high terrain_roughness) should realistically be much
        // higher. Scale the archetype-set value by a roughness-driven factor so the
        // distribution becomes physically plausible.
        //
        //   roughness 0.00 → factor 0.30  (coastal flat; 50-850 → 15-255 m)
        //   roughness 0.30 → factor 0.75  (rolling; 50-850 → 38-638 m)
        //   roughness 0.50 → factor 1.05  (hilly; roughly unchanged)
        //   roughness 0.65 → factor 1.28  (high terrain; 50-850 → 64-1085 m)
        //   roughness 1.00 → factor 1.80  (alpine; 50-850 → 90-1530 m)
        float roughness_factor = 0.30f + prov.geography.terrain_roughness * 1.50f;
        prov.geography.elevation_avg_m =
            std::max(5.0f, prov.geography.elevation_avg_m * roughness_factor);

        // ---- Elevation lapse rate on temperature ----
        // Environmental lapse rate: ~6.5 °C per 1 000 m. Apply uniformly to all three
        // temperature fields. Uses the corrected elevation computed above.
        float lapse_c = prov.geography.elevation_avg_m * 0.0065f;
        prov.climate.temperature_avg_c -= lapse_c;
        prov.climate.temperature_min_c -= lapse_c;
        prov.climate.temperature_max_c -= lapse_c;

        // Clamp to physically plausible global extremes.
        prov.climate.temperature_avg_c =
            std::max(-50.0f, std::min(50.0f, prov.climate.temperature_avg_c));
        prov.climate.temperature_min_c =
            std::max(-65.0f, std::min(prov.climate.temperature_avg_c,
                                      prov.climate.temperature_min_c));
        prov.climate.temperature_max_c =
            std::max(prov.climate.temperature_avg_c,
                     std::min(60.0f, prov.climate.temperature_max_c));
    }
}

// ===========================================================================
// Stage 4b — Economic geography seeding (trade openness, wildfire, employment)
// ===========================================================================

void WorldGenerator::seed_economic_geography(WorldState& world) {
    for (auto& prov : world.provinces) {
        // ---- Trade openness from geography ----
        // Only coastal_trade archetype sets trade_openness in apply_archetype; all
        // other archetypes leave it at 0.0 (zero-initialised Province). Fix this by
        // computing a geography-driven baseline and blending with any existing value.
        //
        // Drivers:
        //   Port capacity   — primary gateway for international goods flow
        //   River access    — inland waterway corridors
        //   Infrastructure  — road/rail density reduces border friction
        //   Landlocked      — geographic trade barrier
        //   Island isolation — extra logistics cost
        float geo_openness = prov.geography.port_capacity * 0.50f
                           + prov.geography.river_access   * 0.15f
                           + prov.infrastructure_rating    * 0.10f;
        if (prov.geography.is_landlocked) geo_openness -= 0.10f;
        if (prov.island_isolation)        geo_openness -= 0.05f;
        geo_openness = std::max(0.08f, std::min(0.85f, geo_openness));

        if (prov.trade_openness < 0.01f) {
            // Not yet set by archetype — use the geography-derived value directly.
            prov.trade_openness = geo_openness;
        } else {
            // coastal_trade (and possibly others) already set a value; blend to keep
            // archetype flavour while anchoring to physical geography.
            prov.trade_openness = std::max(0.10f, std::min(0.95f,
                prov.trade_openness * 0.65f + geo_openness * 0.35f));
        }

        // ---- Wildfire vulnerability from climate zone ----
        // apply_archetype sets a generic 0.05–0.25 regardless of climate. Override
        // with a climate-zone-informed value and amplify by drought vulnerability.
        float base_wildfire;
        switch (prov.climate.koppen_zone) {
            case KoppenZone::Csa:
            case KoppenZone::Csb:
                // Mediterranean: long dry summer + dry scrub = highest wildfire risk.
                base_wildfire = 0.50f;
                break;
            case KoppenZone::BSh:
            case KoppenZone::BSk:
            case KoppenZone::Aw:
                // Steppe / tropical dry: grass fires; moderate-high.
                base_wildfire = 0.35f;
                break;
            case KoppenZone::BWh:
            case KoppenZone::BWk:
                // Desert: very dry but almost no fuel → moderate despite aridity.
                base_wildfire = 0.20f;
                break;
            case KoppenZone::Cfa:
            case KoppenZone::Cfb:
            case KoppenZone::Cfc:
            case KoppenZone::Cwa:
            case KoppenZone::Dfa:
            case KoppenZone::Dfb:
                // Humid temperate / continental: occasional; wet season limits spread.
                base_wildfire = 0.15f;
                break;
            case KoppenZone::Dfc:
            case KoppenZone::Dfd:
                // Subarctic boreal: sporadic but large when they occur.
                base_wildfire = 0.20f;
                break;
            case KoppenZone::Af:
            case KoppenZone::Am:
                // Wet tropical: very high moisture; rarely dries enough to burn.
                base_wildfire = 0.06f;
                break;
            case KoppenZone::ET:
            case KoppenZone::EF:
                // Polar: no combustible vegetation.
                base_wildfire = 0.02f;
                break;
            default:
                base_wildfire = 0.15f;
                break;
        }
        // Drought amplifies fire risk: double drought_vulnerability → +50% wildfire.
        float drought_amp = 1.0f + prov.climate.drought_vulnerability * 0.50f;
        prov.climate.wildfire_vulnerability =
            std::max(0.01f, std::min(0.95f, base_wildfire * drought_amp));

        // ---- Formal employment rate from infrastructure and governance ----
        // apply_archetype sets 0.6–0.85 generically. Adjust downward for high
        // corruption and inequality; upward for strong infrastructure.
        float infra_bonus       = (prov.infrastructure_rating   - 0.50f) * 0.15f;
        float corruption_penalty = prov.political.corruption_index      * 0.18f;
        float inequality_penalty = prov.conditions.inequality_index     * 0.10f;
        prov.conditions.formal_employment_rate =
            std::max(0.20f, std::min(0.95f,
                prov.conditions.formal_employment_rate
                    + infra_bonus - corruption_penalty - inequality_penalty));
    }
}

// ===========================================================================
// Stage 5+6 — Soils and Biomes (simplified WorldGen v0.18 pass)
// ===========================================================================

void WorldGenerator::derive_soils_and_biomes(WorldState& world, DeterministicRNG& rng) {
    for (auto& prov : world.provinces) {
        // ---- Stage 5: Soil fertility from geology_type ----
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

        // Apply soil fertility to agricultural_productivity (bounded to [0.02, 1.0]).
        prov.agricultural_productivity =
            std::max(0.02f, std::min(1.0f,
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
        // Blend: 60% archetype, 40% climate expectation.
        prov.geography.forest_coverage =
            std::max(0.0f, std::min(1.0f,
                prov.geography.forest_coverage * 0.60f + expected_forest * 0.40f));

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
        // Blend 50/50 with existing archetype values to preserve scenario tuning.
        prov.climate.drought_vulnerability =
            std::max(0.0f, std::min(1.0f,
                prov.climate.drought_vulnerability * 0.50f + climate_drought * 0.50f));
        prov.climate.flood_vulnerability =
            std::max(0.0f, std::min(1.0f,
                prov.climate.flood_vulnerability * 0.50f + climate_flood * 0.50f));
    }
}

// ===========================================================================
// Stage 7 — Special terrain features (complete WorldGen v0.18 pass)
// ===========================================================================

void WorldGenerator::detect_special_features(WorldState& world) {
    for (auto& prov : world.provinces) {
        const float lat = prov.geography.latitude;
        const KoppenZone kz = prov.climate.koppen_zone;

        // ---- Permafrost ----
        // Arctic Circle threshold (66.5°) or polar Koppen zones.
        bool permafrost_condition =
            (lat > 66.5f) ||
            (kz == KoppenZone::ET) ||
            (kz == KoppenZone::EF);

        if (permafrost_condition) {
            prov.has_permafrost = true;

            // Lock CrudeOil and NaturalGas accessibility until arctic_drilling tech + thaw.
            // This implements the ResourceDeposit::accessibility override described in the
            // ResourceDeposit comment in geography.h.
            for (auto& deposit : prov.deposits) {
                if (deposit.type == ResourceType::CrudeOil ||
                    deposit.type == ResourceType::NaturalGas) {
                    deposit.accessibility = 0.0f;
                }
            }

            // Permafrost severely limits agriculture (frozen ground, short growing season).
            prov.agricultural_productivity =
                std::max(0.02f, prov.agricultural_productivity * 0.40f);
        }

        // ---- Fjord ----
        // High-relief glacially-carved coastline: requires coastal access, significant
        // roughness, and northern latitude (glacial origin).
        bool fjord_condition =
            !prov.geography.is_landlocked &&
            (prov.geography.coastal_length_km > 100.0f) &&
            (prov.geography.terrain_roughness > 0.55f) &&
            (lat > 50.0f);

        if (fjord_condition) {
            prov.has_fjord = true;

            // Fjord channels are scenic but navigable only with care: raise Maritime link cost.
            for (auto& link : prov.links) {
                if (link.type == LinkType::Maritime) {
                    link.transit_terrain_cost =
                        std::min(1.0f, link.transit_terrain_cost + 0.15f);
                }
            }
        }

        // ---- Permafrost precludes karst ----
        // Permafrost seals surface drainage; karst dissolution requires liquid water.
        if (prov.has_permafrost) {
            prov.has_karst = false;
        }
    }
}

// ===========================================================================
// Stage 9 — Population attractiveness (simplified WorldGen v0.18 pass)
// ===========================================================================

void WorldGenerator::seed_population_attractiveness(WorldState& world, DeterministicRNG& rng) {
    for (auto& prov : world.provinces) {
        // Compute an attractiveness score in [0, 1].
        // Baseline 0.5 = no change. Above 0.5 = population bonus; below = penalty.
        float score = 0.50f;

        // Fertile soils and arable land attract farmers and settlers.
        score += prov.agricultural_productivity * 0.12f;

        // High infrastructure already signals a developed, attractive province.
        score += prov.infrastructure_rating * 0.12f;

        // River access: historically the primary settlement driver.
        score += prov.geography.river_access * 0.08f;

        // Arable fraction: direct land availability.
        score += prov.geography.arable_land_fraction * 0.06f;

        // Active tectonic hazards (earthquakes, eruptions) deter dense settlement.
        score -= prov.tectonic_stress * 0.12f;

        // Drought vulnerability: arid regions hold fewer people sustainably.
        score -= prov.climate.drought_vulnerability * 0.10f;

        // Permafrost: very hostile to settlement; small populations in resource outposts only.
        if (prov.has_permafrost) {
            score -= 0.20f;
        }

        // Island isolation: historically isolated; limited carrying capacity.
        if (prov.island_isolation) {
            score -= 0.08f;
        }

        // Clamp score to a valid range.
        score = std::max(0.10f, std::min(0.90f, score));

        // Map score to a population multiplier centered on 1.0 at score = 0.5.
        // score 0.5 → multiplier 1.0 (no change from archetype baseline)
        // score 0.1 → multiplier 0.68
        // score 0.9 → multiplier 1.32
        float multiplier = 0.60f + score * 0.80f;

        // Add small RNG variation (±5%) so identical-archetype provinces diverge slightly.
        multiplier *= (0.95f + rng.next_float() * 0.10f);

        // Apply, keeping population in a meaningful range.
        uint32_t new_pop = static_cast<uint32_t>(
            static_cast<float>(prov.demographics.total_population) * multiplier + 0.5f);
        prov.demographics.total_population = std::max(10000u, new_pop);
    }
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

        prov.province_lore = std::string(geo) + " " + climate + " " + econ;
    }
}

}  // namespace econlife
