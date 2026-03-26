#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace econlife {

// Forward declarations for types defined in other domain headers
struct TariffSchedule;     // Section 18
struct RegionCohortStats;  // Section 15 (demographics)

// ---------------------------------------------------------------------------
// H3Index
// ---------------------------------------------------------------------------
// Uber H3 hexagonal spatial index. 64-bit cell identifier encoding resolution
// and position on the globe. Resolution 4 = province scale (~1,770 km2);
// resolution 9 = facility placement (~0.1 km2).
// Parent/child/neighbor relationships are O(1) from the index -- no extra storage.
// Named constants (simulation_config.json -> world):
//   h3_province_resolution  = 4   // Province-scale cells; ~1,770 km2 avg area
//   h3_facility_resolution  = 9   // Facility placement precision; ~0.1 km2 avg area
using H3Index = uint64_t;

// ---------------------------------------------------------------------------
// LinkType
// ---------------------------------------------------------------------------
enum class LinkType : uint8_t {
    Land = 0,      // shared land border
    Maritime = 1,  // sea or ocean crossing; no shared land border
    River = 2      // navigable river corridor; distinct transit cost profile
};

// ---------------------------------------------------------------------------
// ProvinceLink
// ---------------------------------------------------------------------------
// Rich adjacency descriptor. Replaces bare adjacent_province_ids on the
// Province struct. Encodes how two provinces connect and the cost of
// transiting between them.
struct ProvinceLink {
    H3Index neighbor_h3;  // H3 res-4 cell index of the neighboring province
    LinkType type;
    float shared_border_km;      // 0.0 for Maritime links (no physical border)
    float transit_terrain_cost;  // 0.0-1.0; 0.0 = highway flat land; 1.0 = mountain or swamp
    float infrastructure_bonus;  // 0.0-1.0; road/rail quality; reduces effective transit cost
};

// ---------------------------------------------------------------------------
// WorldGenParameters
// ---------------------------------------------------------------------------
// Input parameters for the procedural world generation pipeline
// (WorldGen v0.17 Pipeline). Passed to WorldGenPipeline::run() at new-game
// creation; not present at runtime. Written by the scenario editor; consumed
// once before WorldLoadParameters takes over.
// Named constants (simulation_config.json -> worldgen):
//   worldgen.v1_target_province_count   = 6
//   worldgen.resource_richness_min      = 0.5
//   worldgen.resource_richness_max      = 2.0
//   worldgen.climate_volatility_min     = 0.5
//   worldgen.climate_volatility_max     = 2.0
struct WorldGenParameters {
    uint64_t seed;                     // deterministic RNG seed for all 11 pipeline stages
    uint8_t target_province_count;     // V1 target: 6
    float resource_richness;           // 0.5-2.0; global multiplier on deposit quantity seeding
    float climate_volatility;          // 0.5-2.0; precipitation and temperature variance multiplier
    float corruption_baseline;         // 0.0-1.0; Province.political.corruption floor at world load
    float criminal_activity_baseline;  // 0.0-1.0; informal market share floor at world load
    std::string output_world_file;     // path for generated world.json; consumed by
                                       // WorldLoadParameters.world_file
};

// ---------------------------------------------------------------------------
// WorldLoadParameters
// ---------------------------------------------------------------------------
struct WorldLoadParameters {
    std::string world_file;     // path to base_world.json (required)
    std::string scenario_file;  // path to .scenario file (required)
    uint64_t random_seed;       // deterministic seed for stochastic elements
    bool debug_all_lod0;        // force all provinces to LOD 0; debug mode only
};

// ---------------------------------------------------------------------------
// ResourceType / ResourceDeposit
// ---------------------------------------------------------------------------
enum class ResourceType : uint8_t {
    IronOre = 0,
    Copper,
    Bauxite,
    Lithium,
    Coal,
    CrudeOil,
    NaturalGas,
    LimestoneSilica,
    Wheat,
    Corn,
    Soybeans,
    Cotton,
    Timber,
    Fish,
    SolarPotential,
    WindPotential
    // [EX] full deposit list from GDD Section 8.3 expanded in later passes
};

struct ResourceDeposit {
    uint32_t id;
    ResourceType type;
    float quantity;
    float quality;  // 0.0-1.0; affects processing conversion efficiency
    float depth;    // 0.0-1.0; affects extraction cost
    float
        accessibility;  // 0.0-1.0; infrastructure requirement before extraction is viable.
                        // For permafrost-locked deposits (ResourceType::CrudeOil or
                        // NaturalGas in provinces with KoppenZone ET or EF, or
                        // GeographyProfile.latitude > config.resources.arctic_latitude_threshold):
                        //   accessibility is overridden to 0.0 until BOTH conditions hold:
                        //     (1) province.climate.climate_stress_current
                        //             > config.resources.permafrost_thaw_threshold
                        //     (2) actor_tech_state.has_researched("arctic_drilling") == true
                        //   When both conditions are met, accessibility is restored to its
                        //   seeded base value. Evaluated each tick in tick step 1 (production)
                        //   before extraction output is computed.
                        //   Constants in resources.json:
                        //     permafrost_thaw_threshold = 0.40  // climate_stress_current 0.0-1.0
                        //     scale arctic_latitude_threshold  = 66.5  // degrees; Arctic Circle
    float depletion_rate;  // fraction depleted per tick at full production
    float quantity_remaining;
};

// ---------------------------------------------------------------------------
// KoppenZone
// ---------------------------------------------------------------------------
enum class KoppenZone : uint8_t {
    Af = 0,
    Am = 1,
    Aw = 2,  // Tropical
    BWh = 3,
    BWk = 4,
    BSh = 5,
    BSk = 6,  // Arid
    Cfa = 7,
    Cfb = 8,
    Cfc = 9,  // Temperate/oceanic
    Csa = 10,
    Csb = 11,
    Cwa = 12,  // Mediterranean/subtropical
    Dfa = 13,
    Dfb = 14,
    Dfc = 15,
    Dfd = 16,  // Continental
    ET = 17,
    EF = 18,  // Polar
};

// ---------------------------------------------------------------------------
// SimulationLOD
// ---------------------------------------------------------------------------
enum class SimulationLOD : uint8_t {
    full = 0,         // 27-step tick; full NPC model; detailed market clearing
    simplified = 1,   // Monthly LOD 1 update; archetype NPCs; aggregated markets
    statistical = 2,  // Annual batch update only; contributes to price index
};

// ---------------------------------------------------------------------------
// GeographyProfile
// ---------------------------------------------------------------------------
struct GeographyProfile {
    float latitude;
    float longitude;
    float elevation_avg_m;
    float terrain_roughness;     // 0.0 (flat) to 1.0 (mountainous)
    float forest_coverage;       // 0.0-1.0; from ESA WorldCover
    float arable_land_fraction;  // 0.0-1.0; from FAO GAEZ
    float coastal_length_km;
    bool is_landlocked;
    float port_capacity;  // 0.0 (none) to 1.0 (major port)
    float river_access;   // 0.0-1.0; navigable river density
    float area_km2;
};

// ---------------------------------------------------------------------------
// ClimateProfile
// ---------------------------------------------------------------------------
struct ClimateProfile {
    KoppenZone koppen_zone;
    float temperature_avg_c;
    float temperature_min_c;
    float temperature_max_c;
    float precipitation_mm;
    float precipitation_seasonality;
    float drought_vulnerability;   // 0.0-1.0
    float flood_vulnerability;     // 0.0-1.0
    float wildfire_vulnerability;  // 0.0-1.0
    float climate_stress_current;  // runtime; accumulation updated each tick (tiny delta per tick)
                                   // downstream effects (agricultural_productivity,
                                   // community_state) batched every 7 ticks via DeferredWorkQueue
                                   // (WorkType::climate_downstream_batch)
};

// ---------------------------------------------------------------------------
// RegionDemographics
// ---------------------------------------------------------------------------
struct RegionDemographics {
    uint32_t total_population;
    float median_age;
    float education_level;  // 0.0-1.0
    float income_low_fraction;
    float income_middle_fraction;
    float income_high_fraction;
    float political_lean;  // -1.0 (hard left) to 1.0 (hard right)
};

// ---------------------------------------------------------------------------
// CommunityState
// ---------------------------------------------------------------------------
struct CommunityState {
    float cohesion;             // 0.0-1.0
    float grievance_level;      // 0.0-1.0; primary driver of escalation stage
    float institutional_trust;  // 0.0-1.0
    float resource_access;      // 0.0-1.0; gate on upper escalation stages
    uint8_t response_stage;     // 0-6; see Section 14
};

// ---------------------------------------------------------------------------
// RegionalPoliticalState
// ---------------------------------------------------------------------------
struct RegionalPoliticalState {
    uint32_t governing_office_id;
    float approval_rating;  // 0.0-1.0
    uint32_t election_due_tick;
    float corruption_index;  // 0.0-1.0
};

// ---------------------------------------------------------------------------
// RegionConditions
// ---------------------------------------------------------------------------
struct RegionConditions {
    float stability_score;
    float inequality_index;
    float crime_rate;
    float addiction_rate;
    float criminal_dominance_index;
    float formal_employment_rate;       // 0.0-1.0; fraction of working-age population in formal
                                        // (declared, taxed) employment. Updated monthly via
                                        // DeferredWorkQueue. High criminal_dominance_index
                                        // suppresses this over time as legitimate businesses
                                        // exit or are extorted.
    float regulatory_compliance_index;  // 0.0-1.0; mean(1.0 -
                                        // facility.scrutiny_meter.current_level)
                                        // across all facilities in province where
                                        // criminal_sector == false.
                                        // Recomputed each tick step 13.
                                        // 1.0 = all formal facilities clean;
                                        // 0.0 = pervasive enforcement actions.

    // --- Agricultural event scalars (read by ComputeOutputQuality, farm recipes) ---
    // These are runtime event multipliers, not static exposure factors.
    // Static exposure factors (drought_vulnerability, flood_vulnerability) live on ClimateProfile.
    // These scalars are updated by the climate downstream batch
    // (WorkType::climate_downstream_batch).
    float drought_modifier;  // 0.0-1.0; 1.0 = no active drought; 0.3 = severe drought.
                             // Set by drought event onset; recovers toward 1.0 each tick
                             // at config.climate.drought_recovery_rate when event is inactive.
    float flood_modifier;    // 0.0-1.0; 1.0 = no active flood; 0.0 = crops inundated.
                             // Set by flood event onset; recovers toward 1.0 when event
                             // clears. Flood events are shorter-duration than droughts.
    // NOTE: soil_health is per-facility (farm), not per-province. It lives on the Facility struct
    // for farm facilities, updated by the agricultural management system (tick step 1, production).
    // See farm recipe handling in Commodities & Factories ComputeOutputQuality.
};

// ---------------------------------------------------------------------------
// Province
// ---------------------------------------------------------------------------
struct Province {
    // Identity
    H3Index h3_index;    // canonical spatial identifier (H3 res 4); external-stable
                         // across saves and used by WorldGen pipeline; O(1) neighbor/
                         // parent queries. Distinct from id: id is the runtime array
                         // index in WorldState.provinces for O(1) in-simulation lookups.
    bool    is_pentagon    = false;  // true for H3's 12 fixed pentagons (5 neighbors, not 6)
    uint8_t neighbor_count = 6;      // 5 for pentagons, 6 for regular hexagons
    uint32_t id;
    std::string fictional_name;
    std::string real_world_reference;  // pipeline internal; in world.json; not shown in UI

    // Geography
    GeographyProfile geography;
    ClimateProfile climate;

    // Resources
    std::vector<ResourceDeposit> deposits;  // seeded from USGS data at world load

    // Economy
    RegionDemographics demographics;
    float infrastructure_rating;      // 0.0-1.0; from OSM road/rail density
    float agricultural_productivity;  // 0.0-1.0; runtime: modified by climate_stress
    float energy_cost_baseline;
    float trade_openness;  // 0.0-1.0; affects LOD 1 trade offer generation

    // Simulation
    SimulationLOD lod_level;  // set by scenario file at load
    CommunityState community;
    RegionalPoliticalState political;
    RegionConditions conditions;

    // NPCs (LOD 0 only; empty at LOD 1/2)
    std::vector<uint32_t> significant_npc_ids;
    RegionCohortStats* cohort_stats;  // aggregated; all LOD levels (forward-declared)

    // Terrain modifiers (set by GIS pipeline from world.json at load; static at runtime)
    bool has_karst;  // true = province contains karst terrain (cave systems,
                     // sinkholes, underground drainage). Load-bearing for V1
                     // criminal systems (WorldGen v0.17 Stage 7).
                     // Effects:
                     //   route concealment: RouteProfile.concealment_bonus +=
                     //     config.routes.karst_concealment_bonus for routes
                     //     traversing this province.
                     //   facility signals: FacilitySignals.scrutiny_mitigation +=
                     //     config.opsec.karst_mitigation_bonus for underground or
                     //     partially-concealed facilities in this province.
                     // Populated by the GIS pipeline from WorldGen karst
                     // probability layer. Provinces with KoppenZone Csa, Csb,
                     // Cfa, BWh frequently have karst (limestone karst belt);
                     // ET/EF provinces never do.
    float
        historical_trauma_index;  // 0.0-1.0; static province-level measure of accumulated
                                  // historical grievance from past conflicts, colonialism,
                                  // displacement, or institutional failure. Set at world gen;
                                  // does not change at runtime.
                                  // Effect on community response (Section 14):
                                  //   CommunityState.grievance_level base floor at province load:
                                  //     grievance_level = max(grievance_level,
                                  //       historical_trauma_index *
                                  //       config.community.trauma_grievance_floor_scale)
                                  //   CommunityState.institutional_trust ceiling at province load:
                                  //     institutional_trust = min(institutional_trust,
                                  //       1.0 - historical_trauma_index *
                                  //       config.community.trauma_trust_ceiling_scale)
                                  // These are applied once at world load to initial CommunityState.
                                  // Named constants (simulation_config.json -> community):
                                  //   trauma_grievance_floor_scale = 0.25
                                  //   trauma_trust_ceiling_scale   = 0.30

    // Relationships
    uint32_t region_id;
    uint32_t nation_id;
    std::vector<ProvinceLink> links;   // rich adjacency descriptors; replaces bare
                                       // adjacent_province_ids; encodes LinkType, shared border,
                                       // terrain cost, infrastructure bonus
    std::vector<uint32_t> market_ids;  // LOD 0 only
};

// ---------------------------------------------------------------------------
// Region
// ---------------------------------------------------------------------------
// Thin grouping layer; no simulation state.
struct Region {
    uint32_t id;
    std::string fictional_name;
    uint32_t nation_id;
    std::vector<uint32_t> province_ids;
};

// ---------------------------------------------------------------------------
// GovernmentType
// ---------------------------------------------------------------------------
enum class GovernmentType : uint8_t {
    Democracy = 0,
    Autocracy = 1,
    Federation = 2,
    FailedState = 3
};

// ---------------------------------------------------------------------------
// NationPoliticalCycleState
// ---------------------------------------------------------------------------
struct NationPoliticalCycleState {
    uint32_t current_administration_tick;
    float national_approval;
    bool election_campaign_active;
    uint32_t next_election_tick;
};

// ---------------------------------------------------------------------------
// Lod1NationProfile
// ---------------------------------------------------------------------------
// Embedded in Nation as std::optional<Lod1NationProfile> lod1_profile.
// If null, the nation is LOD 0 (player-controlled, full simulation).
// If populated, the nation runs the LOD 1 monthly update (see Section 20.3).
// Fields are archetype-driven and set from the nation data file; some are
// overridable by the scenario file.
struct Lod1NationProfile {
    // Trade behavior -- archetype-driven; set from nation data file
    float export_margin;   // markup over base production cost on exports
    float import_premium;  // premium the nation will pay above base cost for imports
    float trade_openness;  // 0.0-1.0; scales export quantity offered

    // Production capacity
    float tech_tier_modifier;   // multiplier on base production output (1.0 = current era baseline)
    float population_modifier;  // multiplier on base consumption (scales with population)

    // R&D / technology advancement (simplified -- no full R&D tree)
    float
        research_investment;  // accumulated investment; compared to tier_advance_cost[] to advance
    uint8_t current_tier;     // current technology tier; 1-5

    // Political stability tracking
    float stability_delta_this_month;  // change in stability this month; absolute value checked
                                       // against threshold

    // Climate
    float climate_stress_aggregate;  // accumulated climate stress for this nation
    float climate_vulnerability;     // 0.0-1.0; scales climate_stress delta from GlobalCO2Index

    // Geography (for LOD 1 import transit time calculation)
    float geographic_centroid_lat;  // real-world centroid latitude; pipeline-seeded from world.json
    float geographic_centroid_lon;  // real-world centroid longitude
    float lod1_transit_variability_multiplier;  // 1.0-1.3; seeded at world load; models port
                                                // congestion and customs delay variation; stored in
                                                // world.json

    // LOD 1 archetype -- set in nation data file; overridable by scenario file
    // Determines behavioral profile in LOD 1 monthly update (see Section 20.3)
    // "aggressive_exporter" | "protectionist" | "resource_dependent" | "industrial_hub"
    std::string archetype;
};

// ---------------------------------------------------------------------------
// Nation
// ---------------------------------------------------------------------------
// Invariant: V1 ships with exactly 1 LOD 0 nation (the player's home nation;
// lod1_profile is nullopt). All other nations have lod1_profile populated.
// Expansion enables additional LOD 0 nations.
struct Nation {
    uint32_t id;
    std::string name;
    std::string currency_code;
    GovernmentType government_type;
    NationPoliticalCycleState political_cycle;
    std::vector<uint32_t> province_ids;
    float corporate_tax_rate;
    float income_tax_rate_top_bracket;

    // --- Macro-economic indicators (updated monthly by government_budget) ---
    float trade_balance_fraction;  // net exports as fraction of GDP proxy; -1.0 to 1.0
    float inflation_rate;          // current inflation differential; 0.0-1.0
    float credit_rating;           // sovereign creditworthiness; 0.0 (junk) to 1.0 (AAA)
    std::map<uint32_t, float> diplomatic_relations;  // [EX] nation_id -> -1.0 to 1.0; empty in V1
    TariffSchedule* tariff_schedule;                 // see Section 18 (forward-declared)
    std::optional<Lod1NationProfile> lod1_profile;   // nullopt -> LOD 0 (player's home nation,
                                                     // full simulation); populated -> LOD 1
                                                     // (simplified monthly update; see Section 20)
};

}  // namespace econlife
