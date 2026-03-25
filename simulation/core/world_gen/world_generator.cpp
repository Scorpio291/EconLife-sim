// WorldGenerator — V1 world generation implementation.
// See world_generator.h for class documentation.
//
// Produces economically diverse, deterministic worlds from seed + CSV data.
// Province archetypes drive geographic, economic, and demographic variation.
// All random draws go through DeterministicRNG for full reproducibility.

#include "core/world_gen/world_generator.h"

#include <algorithm>
#include <cmath>

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
    for (uint32_t p = 0; p < config.province_count; ++p) {
        Province province{};
        province.id = p;
        province.h3_index = static_cast<H3Index>(0x840000000ULL + p);
        province.region_id = p;
        province.nation_id = 0;
        province.lod_level = SimulationLOD::full;
        province.cohort_stats = nullptr;

        ProvinceArchetype archetype = assign_archetype(rng, p, config.province_count);

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

void WorldGenerator::create_province_links(WorldState& world) {
    const uint32_t n = static_cast<uint32_t>(world.provinces.size());
    if (n <= 1)
        return;

    // Create a ring topology: each province connects to its neighbors.
    // Plus cross-links for provinces > 4 to create a richer graph.
    for (uint32_t p = 0; p < n; ++p) {
        auto add_link = [&](uint32_t neighbor_idx, LinkType type) {
            ProvinceLink link{};
            link.neighbor_h3 = world.provinces[neighbor_idx].h3_index;
            link.type = type;
            link.shared_border_km =
                (type == LinkType::Maritime) ? 0.0f : 40.0f + static_cast<float>(p) * 5.0f;
            link.transit_terrain_cost =
                0.2f + world.provinces[p].geography.terrain_roughness * 0.3f;
            link.infrastructure_bonus =
                std::min(world.provinces[p].infrastructure_rating,
                         world.provinces[neighbor_idx].infrastructure_rating);
            world.provinces[p].links.push_back(link);
        };

        // Forward neighbor (ring).
        uint32_t next = (p + 1) % n;
        bool either_coastal = !world.provinces[p].geography.is_landlocked ||
                              !world.provinces[next].geography.is_landlocked;
        LinkType link_type = (world.provinces[p].geography.is_landlocked &&
                              world.provinces[next].geography.is_landlocked)
                                 ? LinkType::Land
                                 : LinkType::Land;
        add_link(next, link_type);

        // Backward neighbor (ring).
        uint32_t prev = (p + n - 1) % n;
        add_link(prev, LinkType::Land);

        // Cross-links for larger maps: connect p to p+2 if maritime route exists.
        if (n > 4 && !world.provinces[p].geography.is_landlocked) {
            uint32_t cross = (p + 2) % n;
            if (!world.provinces[cross].geography.is_landlocked) {
                add_link(cross, LinkType::Maritime);
            }
        }

        // River links for provinces with high river access.
        if (n > 3 && world.provinces[p].geography.river_access > 0.5f) {
            uint32_t river_neighbor = (p + (n / 2)) % n;
            if (world.provinces[river_neighbor].geography.river_access > 0.4f) {
                // Only add if not already linked.
                bool already_linked = false;
                for (const auto& existing : world.provinces[p].links) {
                    if (existing.neighbor_h3 == world.provinces[river_neighbor].h3_index) {
                        already_linked = true;
                        break;
                    }
                }
                if (!already_linked) {
                    add_link(river_neighbor, LinkType::River);
                }
            }
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

}  // namespace econlife
