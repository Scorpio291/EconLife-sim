# EconLife — World Map & Geography Specification
*Companion document to GDD v1.7, Technical Design Document v23, R&D & Technology v2.2, Commodities & Factories v2.3*
*Version 1.1 — reviewed and refined*

---

## Purpose

This document specifies how the real world becomes the EconLife game world. It covers:

1. **Architecture philosophy** — the LOD model that makes a real-scale world simulatable
2. **Data sources** — which real-world datasets feed which game systems, and how
3. **World hierarchy** — Province → Region → Nation → World, and what lives at each level
4. **Fictional naming** — the layer that separates game identity from legal/political exposure
5. **Scenario system** — how the world is defined in data files, enabling modders to change anything
6. **TDD integration** — what changes in Technical Design to support this
7. **Required GDD and Feature Tier List updates** — cross-document conflicts this decision creates

---

## ✅ Cross-Document Conflicts — Resolved

*This section previously contained conflict alerts. Both conflicts have been resolved.*

### Conflict 1 — Game Setting / Time Period — **RESOLVED (GDD v1.1/v1.2)**

GDD Section 2 "The Setting" has been rewritten to describe a January 2000 starting world. The 2040s-like conditions are framed as emergent outcomes of 25 years of simulation, not starting conditions. The Feature Tier List has been updated to "Historical-to-present simulation (Year 2000–Era 5) — V1". The R&D document remains authoritative on the time period.

### Conflict 2 — Procedural World Generation — **RESOLVED (FTL, TDD v3+)**

The Feature Tier List now specifies "GIS-seeded real-world map (Option B) — V1". `WorldGenParameters` has been replaced by `WorldLoadParameters` in TDD v3+. Procedural generation is available as a modding pipeline tool but the base game world is real-world-derived.

---

## Part 1 — Architecture Philosophy

### The World Scale Problem

The current Technical Design specifies V1 as 1 nation, 4 regions. This was a scope decision to control tick budget. A real-world map has ~195 nations and ~800+ meaningful geographic provinces. Naively simulating all of them at the same fidelity as the V1 model would require ~200× more compute — completely infeasible.

The solution is **Level of Detail (LOD) simulation**:

**LOD 0 — Full simulation** (the player's home nation only, V1)
Every tick module in the 27-step tick runs in full. Significant NPCs are individually modeled. Regional markets clear with full supply/demand calculation. Political cycles, community response, criminal OPSEC — all active. V1 ships with one nation at LOD 0 covering 6 provinces (see Part 10 for province count decision).

**LOD 1 — Simplified simulation** (nations with significant trade relationships to the player's nation)
Markets aggregate to national level. No significant individual NPCs — archetype behavioral profiles drive national decisions (invest in manufacturing, raise tariffs, open trade routes). Political events fire on schedule with simplified outcomes. These nations have real deposit data and real trade relationships that propagate price signals into LOD 0 markets. LOD 1 runs a simplified tick; see Part 8 for specification requirements.

**LOD 2 — Statistical simulation** (rest of world)
These nations exist as economic statistics updated once per simulated year. They produce and consume goods according to baseline rates. They respond to major global events (era transitions, climate disasters, resource price spikes) through their contribution to the `GlobalCommodityPriceIndex`. They never tick individually. Their sole function: providing realistic global supply/demand context so LOD 0 prices are not isolated from world conditions.

**V1 scope is preserved.** The player plays in one nation at LOD 0. The rest of the world provides context, not simulation load.

**Expansion path.** Promoting a nation from LOD 1 to LOD 0 is the mechanism for expansion content. A "playable Europe" expansion changes a group of nations from LOD 1 to LOD 0 for the duration of that playthrough.

### Why This Matters for Moddability

The world is defined in data files. A modder who wants to:
- Change resource distributions → edits province deposit overrides
- Run a scarcity scenario → applies a global resource scale factor
- Start in a different historical year → sets `starting_year` and adjusts `starting_era`
- Start in 1970s Cold War → creates scenario file with appropriate tech tiers, trade bloc configs, and era baseline
- Play on a different planet → runs the pipeline tool on that planet's geology data; generates a world.json the engine treats identically

None of these require engine code changes.

---

## Part 2 — Data Sources

### What We Need, What Exists, What It Costs

| Data type | Source | License | Format | Quality |
|---|---|---|---|---|
| Political boundaries (nations, provinces) | Natural Earth | Public domain | Shapefile, GeoJSON | Excellent |
| Coastlines, rivers, lakes | Natural Earth | Public domain | Shapefile | Excellent |
| Terrain elevation | USGS / NASA SRTM | Public domain | GeoTIFF raster | Excellent |
| Land cover (forest, farmland, desert, urban) | ESA WorldCover | CC-BY | GeoTIFF | Very good |
| Mineral deposits (metals, coal) | USGS MRDS | Public domain | Shapefile, CSV | Good (global; last major update 2011; sufficient for game purposes) |
| Oil & gas reserves by basin | USGS World Petroleum Assessment | Public domain | Shapefile | Good |
| Agricultural productivity | FAO GAEZ | Free for any use | GeoTIFF | Excellent |
| Historical climate (year 2000 baseline) | WorldClim v2.1 (1970–2000 avg) | CC-BY | GeoTIFF at 1km | Excellent |
| Population density | NASA SEDAC / GPW v4 | Free | GeoTIFF | Excellent |
| GDP by country (year 2000 baselines) | World Bank Open Data | CC-BY | CSV | Excellent |
| Trade flows | UN Comtrade (2000–2005 avg) | Free, rate-limited API | JSON/CSV | Very good |
| Road/rail infrastructure | OpenStreetMap | ODbL (attribution required) | PBF, Shapefile | Very good |
| Port locations and capacities | World Bank / UNCTAD | Free | CSV | Good |
| CO₂ emissions by country (year 2000) | Our World in Data / IEA | CC-BY | CSV | Excellent |

**Total data acquisition cost: $0.** Engineering time to build the pipeline: ~5 weeks (parallel to game development).

**License note:** All primary sources are public domain or CC-BY. OpenStreetMap is ODbL — attribution required; EconLife processes OSM data into proprietary structs rather than redistributing OSM databases, satisfying ODbL. ESA WorldCover is CC-BY 4.0, requiring attribution. Both satisfied by credits.

**Note on `real_world_reference` visibility:** Province and nation records contain a `real_world_reference` string (e.g., "Germany — Ruhr analog") for internal pipeline use. This string ships in `world.json` and will be readable to any player who opens the file. This is accepted — the fictional naming system is not security-through-obscurity. Players who open the data files can see the mapping; the point of fictional names is to create legal and narrative separation in gameplay, not to prevent discovery.

### Data Pipeline Overview

```
[GIS Sources] → [Python pipeline] → [world.json] → [C++ loader] → [WorldState]

Step 1: DOWNLOAD
  - Natural Earth shapefiles (boundaries, terrain, rivers)
  - USGS MRDS deposit database (CSV + shapefile)
  - WorldClim v2.1 historical climate rasters (1970–2000 average = year 2000 baseline)
  - NASA SEDAC GPW v4 population rasters
  - World Bank GDP and economic indicators (year 2000)
  - FAO GAEZ agricultural productivity rasters
  - UN Comtrade trade flow data (2000–2005 average)

Step 2: PROCESS (Python + geopandas)
  - Define province boundaries by merging/splitting Natural Earth admin-1 features
  - Assign each province to a parent region and nation
  - Spatial join: USGS deposits → provinces
  - Spatial join: WorldClim climate averages → provinces
  - Spatial join: NASA SEDAC population totals → provinces
  - Spatial join: FAO GAEZ agricultural productivity → provinces
  - Spatial join: OSM road/rail density → infrastructure_rating per province
  - Extract GDP and trade flow data per nation from World Bank / Comtrade
  - Apply fictional name mapping (nations.csv, regions.csv, provinces.csv)
  - Assign LOD defaults per nation (see Part 10)
  - Output: world.json

Step 3: VALIDATE
  - All provinces have a parent region; all regions have a parent nation
  - Deposit count per province within expected range (0 is valid for some provinces)
  - Population totals per nation within 5% of World Bank figures
  - Climate zones geographically coherent (no tundra at equator)
  - Flag incomplete deposit records (reserves_known = false) for manual review
  - No province has agricultural_productivity > 1.0

Step 4: LOAD (C++ at game startup)
  - Parse world.json into Province, Region, Nation structs
  - Assign lod_level per province from scenario file
  - Seed RegionalMarket structs for each LOD 0 province
  - Build NationalTradeOffer baseline for each LOD 1 nation
  - Build GlobalCommodityPriceIndex baseline from LOD 2 nation statistics
```

The pipeline runs once to produce `base_world.json`. Scenario mods ship as `.scenario` files that apply overrides at load time; they do not ship modified world.json files unless they introduce fundamentally different geography (Level 4 modding).

---

## Part 3 — World Hierarchy

### Four Levels

```
World
  └── Nations (~195, one per real country, with fictional names)
        └── Regions (3–8 per nation; geographic/economic zones for grouping and political aggregation)
              └── Provinces (2–6 per region; fundamental simulation unit)
                    └── Tiles [EX] — sub-province granularity for facility placement; not V1
```

**Province** is the fundamental simulation unit. It maps to something like a US state, a German Bundesland, a Chinese province, or a Nigerian state — a geographically coherent unit with distinct economic character. Each LOD 0 province runs its own `RegionalMarket`, `CommunityState`, `RegionalPoliticalState`, and NPC population.

**Region** groups provinces into culturally and economically coherent zones within a nation. Examples: an industrial heartland region, an agricultural belt region, a coastal trade hub region. Region serves as the grouping layer for UI, for political aggregation in multi-province elections, and for NPC mobility cost calculations (NPCs move between provinces within a region more cheaply than across regions). Region has no simulation state of its own — all state lives at Province level.

**Nation** maps 1:1 to a real country, with a fictional name. It holds government type, currency, tax regime, and diplomatic relationships with other nations. Trade occurs between provinces; tariffs apply at national borders.

### Province Data Structure

The existing `Region` struct in TDD v2 Section 12 is renamed `Province`. A new, thinner `Region` struct is added as a grouping layer. All existing `Region` fields are preserved — this is a rename and restructure, not a data loss.

```cpp
enum class KoppenZone : uint8_t {
    // Group A — Tropical
    Af  = 0,   // Tropical rainforest
    Am  = 1,   // Tropical monsoon
    Aw  = 2,   // Tropical savanna
    // Group B — Arid
    BWh = 3,   // Hot desert
    BWk = 4,   // Cold desert
    BSh = 5,   // Hot semi-arid
    BSk = 6,   // Cold semi-arid
    // Group C — Temperate
    Cfa = 7,   // Humid subtropical
    Cfb = 8,   // Oceanic
    Cfc = 9,   // Subpolar oceanic
    Csa = 10,  // Hot-summer Mediterranean
    Csb = 11,  // Warm-summer Mediterranean
    Cwa = 12,  // Monsoon-influenced humid subtropical
    // Group D — Continental
    Dfa = 13,  // Hot-summer humid continental
    Dfb = 14,  // Warm-summer humid continental
    Dfc = 15,  // Subarctic
    Dfd = 16,  // Extremely cold subarctic
    // Group E — Polar
    ET  = 17,  // Tundra
    EF  = 18,  // Ice cap
};

struct GeographyProfile {
    float      latitude;                // -90 to 90; source: province centroid
    float      longitude;               // -180 to 180; source: province centroid
    float      elevation_avg_m;         // metres; source: SRTM raster average
    float      terrain_roughness;       // 0.0 (flat) to 1.0 (mountainous); derived from elevation std dev
    float      forest_coverage;         // 0.0–1.0; source: ESA WorldCover
    float      arable_land_fraction;    // 0.0–1.0; source: FAO GAEZ cropland suitability
    float      coastal_length_km;       // km of coastline; source: Natural Earth coastlines
    bool       is_landlocked;           // true if no coastal border
    float      port_capacity;           // 0.0 (none) to 1.0 (major port); source: UNCTAD port data
    float      river_access;            // 0.0–1.0; navigable river network density
    float      area_km2;                // province area; source: shapefile
};

struct ClimateProfile {
    KoppenZone koppen_zone;             // primary climate classification; source: WorldClim
    float      temperature_avg_c;       // °C annual average; source: WorldClim bio01
    float      temperature_min_c;       // °C coldest month average; source: WorldClim bio06
    float      temperature_max_c;       // °C hottest month average; source: WorldClim bio05
    float      precipitation_mm;        // mm/year; source: WorldClim bio12
    float      precipitation_seasonality; // 0.0 = uniform; 1.0 = highly seasonal
    float      drought_vulnerability;   // 0.0–1.0; derived from WorldClim aridity index
    float      flood_vulnerability;     // 0.0–1.0; from EM-DAT historical flood frequency
    float      wildfire_vulnerability;  // 0.0–1.0; from Global Fire Emissions Database
    float      climate_stress_current;  // runtime field; updated by climate system each tick; init from baseline
};

struct Province {
    uint32_t    id;
    std::string fictional_name;
    std::string real_world_reference;   // pipeline internal; shipped in world.json but not shown in UI

    // --- Geography and Climate ---
    GeographyProfile  geography;
    ClimateProfile    climate;

    // --- Resources ---
    std::vector<ResourceDeposit> deposits;      // seeded from USGS data at world load

    // --- Economy ---
    RegionDemographics demographics;            // source: NASA SEDAC + World Bank
    float infrastructure_rating;               // 0.0–1.0; source: OSM road/rail density
    float agricultural_productivity;           // 0.0–1.0; source: FAO GAEZ; modified by climate_stress
    float energy_cost_baseline;                // $/MWh equivalent; source: IEA country data
    float trade_openness;                      // 0.0–1.0; affects LOD 1 trade offer generation

    // --- Simulation State ---
    SimulationLOD     lod_level;               // set by scenario file at load; can change at runtime
    CommunityState    community;
    RegionalPoliticalState political;
    RegionConditions  conditions;

    // --- NPC Population (LOD 0 only; empty at LOD 1/2) ---
    std::vector<uint32_t> significant_npc_ids;
    RegionCohortStats     cohort_stats;         // aggregated background population; all LOD levels

    // --- Relationships ---
    uint32_t region_id;                         // parent region
    uint32_t nation_id;                         // grandparent nation
    std::vector<uint32_t> adjacent_province_ids; // shared-border provinces; transport adjacency
    std::vector<uint32_t> market_ids;           // LOD 0 only
};

enum class SimulationLOD : uint8_t {
    full        = 0,  // 27-step tick; full NPC model; detailed market clearing
    simplified  = 1,  // simplified tick (see Part 8); archetype NPCs; aggregated markets
    statistical = 2,  // annual batch update only; no per-tick processing
};

struct Region {
    uint32_t id;
    std::string fictional_name;
    uint32_t nation_id;
    std::vector<uint32_t> province_ids;
    // No simulation state — state lives at Province level
    // Region serves UI grouping, political aggregation, and NPC mobility cost calculations only
};
```

---

## Part 4 — Resource Deposit Seeding

### From USGS Data to ResourceDeposit Records

The USGS MRDS database contains point locations for mineral deposits worldwide with commodity type, production status, estimated reserves, grade, and size class. The pipeline converts these into `ResourceDeposit` records and spatially joins them to provinces.

Record quality is variable — some records have precise reserve estimates, others only confirm a deposit exists. The pipeline handles both:

```python
# Pipeline pseudocode — deposit seeding
for deposit in usgs_mrds_records:
    province = spatial_join(deposit.coordinates, province_boundaries)

    if deposit.reserves_known:
        quantity = deposit.reserves_tonnes * QUANTITY_SCALE_FACTOR
    else:
        quantity = estimate_from_deposit_type(deposit.type, deposit.size_class) * QUANTITY_SCALE_FACTOR

    if deposit.grade_known:
        quality = deposit.grade_pct / MAX_GRADE[deposit.commodity]
    else:
        quality = regional_average_grade(deposit.commodity, province.geology_type)

    game_deposit = ResourceDeposit(
        type           = map_commodity_to_resource_type(deposit.commodity),
        quantity       = quantity,
        quality        = clamp(quality, 0.0, 1.0),
        depth          = infer_depth(deposit.type),          # surface=0.0; deep=1.0
        accessibility  = compute_accessibility(
                             province.infrastructure_rating,
                             province.geography.terrain_roughness),
        depletion_rate = DEFAULT_DEPLETION_RATE[deposit.type]
    )
    province.deposits.append(game_deposit)
```

**QUANTITY_SCALE_FACTOR** is a global tuning parameter in the scenario file, normalizing real-world reserve estimates (millions of tonnes) to game tick production units. Modders can scale global resource abundance up or down by adjusting this value.

### Petroleum Basin Mapping

Oil and gas exist in sedimentary basins covering large areas — not point deposits. The USGS World Petroleum Assessment covers ~900 geologic provinces with estimated recoverable resources.

The pipeline aggregates petroleum basins to province level:
- Province overlapping a productive basin: oil/gas deposits proportional to `basin_resources × province_overlap_fraction × recovery_factor`
- Offshore basins: assigned to the nearest coastal province with `depth` = 0.8–1.0 (deep extraction; high cost)
- Unconventional resources (shale, oil sands): seeded as deposits with `era_available = 2` (technology threshold; not extractable in Era 1)

This correctly places massive petroleum resources in the Middle East analog, North Sea analog, Gulf of Mexico analog, and Siberia analog — with the strategic implications of each.

### Agricultural Productivity Seeding

FAO GAEZ provides per-cell agricultural productivity for major crop types under current climate conditions. The pipeline:

1. Averages GAEZ values across each province's cells per crop type
2. Assigns `agricultural_productivity` (0.0–1.0) as a weighted composite across viable crop types
3. Sets which crop keys are viable in each province based on `KoppenZone`
4. Seeds `ClimateProfile.climate_stress_current` from zero at world load; the climate system modifies it forward from the starting CO₂ baseline

The breadbaskets of the world produce grain at realistic rates. Tropical provinces grow tropical crops. Mediterranean climate zones grow the right Mediterranean crops. The productivity gradient from the Nile Delta analog to the Sahara analog is encoded in the data.

---

## Part 5 — Fictional Naming System

### Architecture

Every geographic entity has two name fields in the data:

```cpp
struct Province {
    std::string fictional_name;       // shown in all player-facing UI: "Veldra"
    std::string real_world_reference; // pipeline internal: "Germany — Ruhr analog"
                                      // ships in world.json; not shown in game UI
};
```

The `real_world_reference` is used by the pipeline, developer tooling, and modders writing scenario descriptions. It ships in `world.json` (not obfuscated) because preventing discovery is not the goal — the goal is narrative and legal separation in the game experience itself.

### Name Files

```
/data/world/names/
  nations.csv      — nation_key, fictional_name, culture_archetype, government_type_default, geo_analog
  regions.csv      — region_key, fictional_name, parent_nation_key
  provinces.csv    — province_key, fictional_name, parent_region_key
  cities.csv       — city_key, fictional_name, parent_province_key, population_tier
```

Example rows from `nations.csv`:
```csv
nation_key,fictional_name,culture_archetype,government_type_default,geo_analog
valdoria,Valdoria,Germanic,Democracy,Germany
corrath,Corrath,Anglo-Saxon,Democracy,United Kingdom
merevant,Merevant,Slavic,Autocracy,Russia
solara,Solara,Arabic-Gulf,Autocracy,Saudi Arabia
kavandri,Kavandri,South Asian,Democracy,India
zanthar,Zanthar,West African,Federation,Nigeria
korelu,Korelu,Central African,FailedState,DRC
```

The `culture_archetype` field drives:
- NPC name generation (procedural names use phoneme tables per archetype)
- Architectural style sets in the facility design system (EX)
- Starting corruption and institutional trust baselines in `RegionalPoliticalState`
- Language of NPC text in scene cards (localization hook)

The `government_type_default` sets the starting `GovernmentType` enum value for the nation. This is a default — scenario files can override it. An EX scenario could model a unified Germany that never split, represented by a `Federation` starting type instead of `Democracy`.

### The "Historical Names" Mod

The base game ships with fictional names. A first-party opt-in mod (`historical_names`) ships alongside it, swapping fictional names for real-world equivalents through name file overrides. The mod:
- Is free and opt-in, not bundled by default
- Carries a clear warning: "This mod uses real-world country and place names and references real geopolitical situations. The simulation will diverge from history based on player actions."
- Is hosted in the mod loader alongside community mods
- Does not change any game mechanics — only the name display strings

This structure keeps the base game safe for markets sensitive to political depiction in games, while giving players who want explicit real-world framing the option. The warning on the historical names mod also sets expectation that the simulation will diverge from history — which is a feature, not a bug.

---

## Part 6 — Scenario System

### What a Scenario Is

A scenario is a named, data-file-defined world configuration specifying starting conditions for a playthrough. The world geometry (provinces, their geography, their deposits) is fixed from `world.json` — scenarios change the *state* the world starts in, not the geographic facts.

A scenario specifies:
- `base_world` — which world.json to load (default: the real-geography world)
- `starting_year` — simulated calendar year (determines which era enum value)
- `starting_era` — explicit era override (optional; derived from starting_year if omitted)
- `starting_co2_index` — initial global CO₂ index (1.0 = year 2000 baseline = 370ppm)
- `player_start_options` — which provinces the player may choose to start in
- `lod_assignments` — which nations start at LOD 1 vs LOD 2 (provinces within a nation inherit)
- `resource_overrides` — scale factors on deposits globally or per resource type
- `technology_overrides` — starting tech tier per nation
- `unlocked_nodes_override` — technology nodes already researched at game start
- `economic_overrides` — GDP scale factors per province or nation
- `regulation_overrides` — which climate regulations are already enacted at game start
- `preloaded_events` — global events treated as already-resolved (e.g., "the 2008 financial crisis has already occurred")

```
/data/scenarios/
  base_game.scenario              — default start: Jan 2000; player chooses province
  resource_scarcity.scenario      — battery minerals reduced 80%
  climate_accelerated.scenario    — CO₂ index 1.4 (year 2020 equivalent); Era 4 pressure early
  deglobalization.scenario        — trade costs doubled; regional self-sufficiency rewarded
  blank_slate.scenario            — all deposits removed; modder places resources manually
  [EX] cold_war.scenario          — start 1975; bipolar trade blocs; different tech tree baseline
  [EX] postcrisis.scenario        — start Jan 2009; financial crisis already resolved; recession conditions
```

### Scenario File Format

```json
{
  "scenario_key": "resource_scarcity",
  "display_name": "Critical Shortage",
  "description": "Rare earth and battery mineral deposits are 80% depleted from game start. The energy transition is technologically feasible but materially constrained. R&D into recycling and material substitution becomes the primary path forward.",
  "base_world": "base_world.json",
  "starting_year": 2000,
  "starting_co2_index": 1.0,

  "player_start_options": [
    { "province_key": "valdoria_ruhr_analog" },
    { "province_key": "corrath_london_analog" },
    { "province_key": "kavandri_delhi_analog" }
  ],

  "lod_assignments": {
    "default_lod_1_nations": ["valdoria", "corrath", "merevant", "solara", "kavandri"],
    "default_lod_2_nations": "all_others"
  },

  "resource_overrides": [
    { "resource_type": "lithium_ore",     "global_scale": 0.20 },
    { "resource_type": "cobalt_ore",      "global_scale": 0.20 },
    { "resource_type": "rare_earth_ore",  "global_scale": 0.20 }
  ],

  "technology_overrides": [
    { "nation_key": "valdoria",  "starting_tech_tier": 3 },
    { "nation_key": "kavandri",  "starting_tech_tier": 1 }
  ],

  "economic_overrides": [
    { "province_key": "valdoria_ruhr_analog", "gdp_scale": 1.0 }
  ],

  "quantity_scale_factor": 1.0,

  "notes": "Intentionally makes the energy transition harder. Clean-tech R&D becomes existential rather than optional."
}
```

**LOD assignment model:** `lod_assignments` is specified at nation level in the scenario file. All provinces within a nation inherit the nation's LOD level. Provinces within a single nation do not run at different LOD levels — this would create inconsistencies in NPC population and market state. The one exception: the player's starting province always runs at LOD 0 regardless of its nation's assigned level. When the player's home nation is at LOD 1 (unlikely in base game but possible via modding), only the player's starting province is promoted to LOD 0.

### Scenario Stacking

Scenarios can reference a `base_scenario` and override only what changes:

```json
{
  "scenario_key": "resource_scarcity_accelerated_climate",
  "base_scenario": "resource_scarcity",
  "override_fields": {
    "starting_co2_index": 1.35,
    "starting_year": 2005
  }
}
```

Override resolution order: `base_world.json` → `base_scenario` → current scenario fields. Last specified wins for scalar fields; arrays are replaced entirely (not merged), except `resource_overrides` which are merged with current scenario taking precedence.

---

## Part 7 — Trade Between Provinces and Nations

### How Global Trade Works with LOD

**LOD 0 provinces** trade with each other using the full `RegionalMarket` clearing mechanism defined in TDD v2.

**LOD 1 nations** publish a `NationalTradeOffer` each simulated month: a list of goods they're willing to export (at estimated production cost + margin) and goods they want to import (at prices from their demand model). LOD 0 businesses can accept trade offers. The LOD 1 nation clears offers in aggregate, not per-province.

```cpp
struct NationalTradeOffer {
    uint32_t nation_id;
    uint32_t tick_generated;
    struct GoodOffer {
        uint32_t good_id;
        float    quantity_available;        // units per simulated month
        float    offer_price;               // per unit; includes nation's production cost + margin
        float    tariff_rate_to_apply;      // tariff the importing nation applies at their border
    };
    std::vector<GoodOffer> exports;         // what this nation will sell
    std::vector<GoodOffer> imports;         // what this nation wants to buy (bids)
};
```

**LOD 2 nations** contribute to `GlobalCommodityPriceIndex`. A drought in the LOD 2 Australian wheat belt analog reduces global wheat supply, raising prices in LOD 0 and LOD 1 markets — without any per-province simulation.

```cpp
struct GlobalCommodityPriceIndex {
    uint32_t last_updated_tick;
    std::map<uint32_t, float> lod2_price_modifier; // good_id → multiplier on base spot price
    // Updated once per simulated year during the LOD 2 annual batch job
    // All LOD 0 and LOD 1 markets incorporate this modifier in equilibrium price calculation
};
```

### Transport Costs and Distance

Trade between provinces incurs transport costs:

```
transport_cost_per_unit = base_distance_cost
    × terrain_cost_modifier
    × infrastructure_cost_modifier
    × route_mode_modifier
    × good.physical_size_factor
    × perishable_transit_cost

Where:
  base_distance_cost         = distance_km × BASE_TRANSPORT_RATE
  terrain_cost_modifier      = 1.0 (flat plain) to 2.5 (mountain crossing)
  infrastructure_cost_modifier = 1.0 + (1.0 - min_infrastructure_along_route) × INFRA_COST_COEFF
                                // lower infrastructure → higher cost; INFRA_COST_COEFF = 1.5
  route_mode_modifier        = 1.0 (road/rail); 0.30 (sea route majority); 0.60 (river route majority)
  good.physical_size_factor  = 0.0 (financial instruments) to 2.0 (bulk ore)
  perishable_transit_cost    = 1.0 + (perishable_decay_rate × transit_ticks)
                                // only non-zero for perishable goods; see Commodities doc
```

Named constants to be specified in TDD v2:
- `BASE_TRANSPORT_RATE` — $/km/unit at reference infrastructure (1.0)
- `INFRA_COST_COEFF = 1.5` — cost multiplier from infrastructure deficit
- Route mode is determined by a shortest-path calculation over the province adjacency graph, preferring sea routes where coastal provinces exist in the path

### Tariffs and Trade Policy

```cpp
struct TradeAgreement {
    uint32_t partner_nation_id;
    float    tariff_reduction;    // 0.0 = no reduction; 1.0 = full free trade
    uint32_t signed_tick;
    uint32_t expires_tick;        // 0 = permanent unless dissolved
    bool     is_active;
};

struct TariffSchedule {
    uint32_t nation_id;
    std::map<uint32_t, float> good_tariff_rates; // good_id → ad valorem rate (0.0–1.0)
    float    default_tariff_rate;                // rate for goods not in the schedule
    std::vector<TradeAgreement> trade_agreements;
};
```

Tariff rates are set by the political system and change through:
- Player lobbying (funds domestic industry campaigns that pressure governing party)
- Trade agreement negotiation (EX for player-initiated; LOD 1/2 nations negotiate autonomously)
- Era transitions triggering new regulatory regimes (e.g., Era 4 carbon border adjustments)
- Election outcomes changing governing party's trade policy stance

Criminal trade bypasses tariffs entirely. Smuggling profitability scales directly with tariff rates — a 40% legal tariff creates a 40% margin premium for illegal importers before any other criminal markup. This is intentional: high tariff environments incentivize criminal logistics networks.

---

## Part 8 — Required TDD v2 Changes

The following changes were required in Technical Design. These were applied in TDD v3 (Pass 3 Session 2). They are documented here for reference.

### 1. Rename Region → Province; add Region as grouping layer

`Region` struct in TDD v2 Section 12 is renamed `Province`. All existing fields are preserved. A new, thinner `Region` struct is added. `WorldState.regions` becomes `WorldState.provinces`. The existing `Region` vector becomes `WorldState.provinces`; a new `WorldState.regions` vector holds grouping records.

### 2. Replace Region struct content with Province as specified in Part 3

Add `GeographyProfile` and `ClimateProfile` structs. Replace `WorldGenParameters` with `WorldLoadParameters`. Populate from `world.json` at load, not from procedural generation.

### 3. Add SimulationLOD enum to Province

LOD 0 runs the full 27-step tick. LOD 1 runs a simplified tick (see item 7 below). LOD 2 runs only during the annual batch job.

### 4. Replace WorldGenParameters with WorldLoadParameters

```cpp
struct WorldLoadParameters {
    std::string world_file;          // path to world.json
    std::string scenario_file;       // path to .scenario file
    uint64_t    random_seed;         // for stochastic elements within data-seeded world
    bool        debug_all_lod0;      // if true, all provinces start at LOD 0 (debug mode only)
};
```

`WorldGenParameters` in its current form is removed. The configurable parameters it contained (nation count, region count, inequality, resource richness) are replaced by the scenario file system, which achieves the same goals with more precision and full moddability.

### 5. Add GlobalCommodityPriceIndex and NationalTradeOffer to WorldState

```cpp
struct WorldState {
    // ... existing fields ...
    GlobalCommodityPriceIndex lod2_price_index;          // updated annually by LOD 2 batch job
    std::vector<NationalTradeOffer> lod1_trade_offers;   // regenerated monthly by LOD 1 update step
    std::vector<TariffSchedule> tariff_schedules;        // one per nation
};
```

### 6. Add TariffSchedule to Nation and WorldState

As specified in Part 7. Starting tariff rates are seeded from World Bank trade policy data (year 2000 baselines) and stored in the scenario-loaded data.

### 7. Specify LOD 1 simplified tick — [BLOCKER for Bootstrapper]

The LOD 1 simplified tick needs full specification as a module set. It is flagged as a blocker for the Bootstrapper because the Bootstrapper must generate interface specs for it. Proposed scope: an 8–12 step subset of the 27-step tick covering the modules that affect trade offer generation, market pricing, and political event scheduling. The exact steps need authoring in a new TDD section.

Minimum LOD 1 tick outputs required:
- `NationalTradeOffer` regenerated monthly
- National technology tier advancement (on milestones, not every tick)
- Climate stress accumulation (receives `GlobalCO2Index` contribution, updates `ClimateProfile.climate_stress_current`)
- Political stability tracking (simplified; no full political cycle model)
- LOD 1 nations can promote to LOD 0 (expansion mechanic; initiated by scenario or player action)

### 8. Add LOD 2 annual batch job spec — [BLOCKER for Bootstrapper]

The annual batch job for LOD 2 nations requires specification. Minimum outputs:
- `GlobalCommodityPriceIndex.lod2_price_modifier` updated based on aggregate production/consumption
- Climate stress contribution from LOD 2 nation CO₂ emissions
- Era transition check contribution (LOD 2 economic conditions contribute to global era triggers)

---

## Part 9 — Moddability Levels

**Level 1 — Parameter modification** (novice)
Edit `.scenario` files: change resource scales, starting year, tech tiers, climate index. No new data required. Fully documented in modding guide.

**Level 2 — Province overrides** (intermediate)
Override specific province fields: deposit quantities, demographics, infrastructure rating, climate vulnerability. Useful for "what if" scenarios.

```json
{
  "override_key": "green_sahara",
  "province_overrides": [
    {
      "province_key": "solara_sahara_interior_analog",
      "fields": {
        "agricultural_productivity": 0.65,
        "geography.arable_land_fraction": 0.40,
        "climate.precipitation_mm": 800,
        "climate.koppen_zone": "Aw"
      },
      "deposit_additions": [
        { "type": "wheat_farmland", "quantity": 500000, "quality": 0.70 }
      ]
    }
  ]
}
```

Overrides are layered on top of `world.json`; deposit additions are always additive; scalar field overrides replace.

**Level 3 — Region and nation restructuring** (intermediate)
Rearrange province-to-region and region-to-nation memberships. Create new nations by splitting existing ones. Merge regions. Useful for scenarios modeling political fragmentation or unification.

**Level 4 — Alternative world files** (advanced)
Replace `base_world.json` with a different world entirely. Historical Earth (1950, 1975), a fictional continent, a different planet. The engine is world-agnostic — it simulates whatever `Province`, `Region`, and `Nation` records it loads.

**Level 5 — Pipeline access** (developer-level)
The GIS pipeline tool ships as a separate, open-source MIT-licensed utility. Modders can run it against their own GIS data — Mars topography, a fantasy continent exported from a world-building tool, an alternate history where continental boundaries differ — and generate a `world.json` the engine loads identically to the base game world.

**License note for modders:** The pipeline tool is MIT licensed. GIS data processed through it may carry attribution requirements (ESA WorldCover: CC-BY; OpenStreetMap infrastructure data: ODbL attribution and share-alike for derived databases). The modding guide must document these obligations clearly.

### World Editor [EX]
A visual tool for manipulating province data — painting resource deposits, adjusting climate parameters, redrawing region boundaries — is an expansion feature. V1 ships with file-based modding only. The editor is primarily a UI investment; the underlying data model fully supports visual editing.

---

## Part 10 — V1 Scope

### Province Count Decision

Open Question #1 from the first draft is resolved here.

**Decision: 6 provinces at LOD 0 for V1.**

Rationale: The V1 tick budget was designed for 4 regions at full simulation. Moving to 6 provinces is a 50% increase in LOD 0 simulation surface — within acceptable range given the LOD system reduces LOD 1 and LOD 2 overhead. At 6 provinces × ~750 significant NPCs per province = 4,500 significant NPCs at LOD 0. This is within the TDD v2 risk estimate of 10–15MB WorldState.

A Germany-analog nation with 6 provinces can represent coherent geographic archetypes: industrial heartland, financial capital, agricultural belt, port/trade hub, peripheral rural, border manufacturing zone. This gives players meaningfully different starting conditions within one nation without overcounting.

TDD v2 must re-run the tick budget estimate at 6 provinces. The existing estimate was 4 regions; 6 provinces with the same NPC density increases compute by ~50%. If this exceeds the tick budget, the first mitigation is reducing NPC density to ~600 per province (4,500 → 3,600 total), preserving province count.

### V1 LOD Assignment Defaults

For the base game default scenario:
- **Player's home nation (6 provinces): LOD 0**
- **LOD 1 (simplified simulation):** 5–8 nations with major trade relationships to the player's home nation. Default set varies by starting province choice — if the player starts in a Valdoria analog, the LOD 1 set includes its major trade partners. The scenario file specifies a default list; the player cannot change this in V1.
- **LOD 2 (statistical):** All other nations (~185+)

LOD 1 nations generate trade offers every simulated month and update their climate stress and tech tier annually. Their total per-tick overhead is small — they don't run individual NPC simulation or detailed market clearing.

### What This Adds Over the Original V1 Design

The original V1 design had an isolated 4-region nation. This architecture adds, at minimal compute cost:
- Global commodity prices respond to world conditions (not just 4 regions)
- Trade routes to other nations available from game start via LOD 1 trade offers
- Climate system has world context — CO₂ index reflects all nations
- Era transitions trigger on global conditions, not just local ones
- Expansion path to new playable nations is architecturally clean

### Bootstrapper Struct List

The GIS pipeline is a pre-game tool. The Bootstrapper generates C++ structs and loaders that consume its output, not the pipeline itself.

Required new structs and types for Bootstrapper:
1. `KoppenZone` enum
2. `SimulationLOD` enum
3. `GeographyProfile` struct
4. `ClimateProfile` struct (with `KoppenZone`)
5. `Province` struct (renamed from `Region`; fields extended)
6. `Region` struct (new grouping layer; thin)
7. `WorldLoadParameters` struct
8. `GlobalCommodityPriceIndex` struct
9. `NationalTradeOffer` struct (including `GoodOffer` inner struct)
10. `TradeAgreement` struct
11. `TariffSchedule` struct
12. World JSON loader function
13. Scenario file loader and override application function
14. LOD 1 simplified tick module interfaces (pending LOD 1 tick spec in TDD)
15. LOD 2 annual batch job interface (pending LOD 2 spec in TDD)

---

## Part 11 — Implementation Sequence

### Phase 1: GIS Pipeline (parallel to game development)

1. Download all data sources — 1 day
2. Define province boundary set (merge/split Natural Earth admin-1 features to meaningful province size) — 3 days
3. Write geopandas pipeline: deposit seeding, climate averages, population joins — 2 weeks
4. Write petroleum basin aggregation logic — 1 week
5. Generate and validate `base_world.json` — 3 days
6. Author fictional naming files (`nations.csv`, `regions.csv`, `provinces.csv`) — 3 days
7. Write base game scenario file — 1 day

**Total: ~5 weeks, fully parallel to game code**

### Phase 2: C++ Loaders

1. Implement `KoppenZone` enum, `GeographyProfile`, `ClimateProfile` — 2 days
2. Implement `Province`, `Region` structs — 2 days
3. Implement `WorldLoadParameters`, `GlobalCommodityPriceIndex`, `NationalTradeOffer`, `TariffSchedule`, `TradeAgreement` — 2 days
4. World JSON loader — 1 week
5. Scenario file loader and override application — 1 week
6. Integrate with WorldState — 2 days
7. LOD 1 module stubs (pending spec) — 1 week
8. LOD 2 batch job stub (pending spec) — 3 days

**Total: ~4 weeks**

### Phase 3: Validation

1. Load `base_world.json`; verify geographic coherence — 2 days
2. Run 100-seed simulation; verify LOD 0 commodity prices are plausible — 3 days
3. Verify LOD 2 price index modifiers produce realistic global price signals — 2 days
4. Verify transport costs produce correct relative pricing (sea routes cheap; landlocked premium) — 2 days

**Total: ~1.5 weeks**

Phase 2 can begin in parallel with Pass 3 TDD resolution. The loaders are independent of the unresolved algorithmic blockers (price equilibrium formula, NPC expected value calculation).

---

## Part 12 — Open Questions

1. **Province boundary definition methodology.** Natural Earth admin-1 features vary enormously in granularity — some countries have 50+ states/provinces, others have 5. A uniform province-size approach (targeting ~500,000–5,000,000 population per province) requires merging small admin-1 units and splitting large ones. This requires judgment calls for countries with unusual admin-1 structures. Who makes these calls, and how are they documented for modder reproducibility?

2. **LOD 1 trade offer frequency.** "Monthly" is proposed but may be too infrequent for goods with high price volatility. Consider weekly for agricultural goods, monthly for industrial goods, quarterly for capital goods. Needs a tuning decision before LOD 1 spec is written.

3. **LOD 1 nation set per starting province.** The base game default scenario lists 5–8 LOD 1 nations. How is this set determined for each possible starting province? A player starting in a Kavandri (India analog) province has different major trade partners than one starting in Valdoria (Germany analog). The scenario file needs a mapping from `player_start_province` to `lod_1_nation_set`. This may mean the base game scenario has multiple LOD 1 presets rather than one global list.

4. **Political boundary changes between 2000 and 2025.** Real borders changed (South Sudan, Kosovo, etc.). The game starts at 2000 — should the base world use 2000 political boundaries only, with boundary changes occurring as in-simulation events triggered by era/political conditions? Recommendation: yes — start with 2000 political map; era events can trigger province-to-nation reassignment as a political outcome, not a geographic change.

5. **`BASE_TRANSPORT_RATE` calibration.** The transport cost formula needs this constant tuned so that maritime trade costs are realistic relative to commodity prices. A real-world reference: shipping bulk iron ore from Western Australia to China costs roughly $8–12/tonne in 2000. The constant should be calibrated against this so simulated transport costs produce the same rough relationship. This is a tuning exercise for the validation phase.

6. **LOD 1 tick specification priority.** This is a Bootstrapper blocker (Part 8, item 7). It should be elevated to equal priority with the five remaining TDD algorithmic blockers (B13–B18) in the Pass 3 queue.

---

## Change Log

**v1.0 → v1.1**
- Added Cross-Document Conflict Alert section (GDD time setting conflict; Feature Tier List procedural generation conflict)
- Replaced `float climate_zone` encoding with `KoppenZone` enum (19 values covering full Köppen classification)
- Fixed `ResourceDeposits` type → `std::vector<ResourceDeposit>` consistent with TDD v2
- Unified `has_major_port` (bool) and `port_access` (float) → single `port_capacity` float field in `GeographyProfile`
- Moved `arable_land_fraction` to `GeographyProfile` only (removed duplication)
- Added `area_km2` to `GeographyProfile`
- Defined `infrastructure_cost_modifier` formula explicitly (was `infrastructure_inverse`, undefined)
- Defined `TradeAgreement` struct (was referenced in `TariffSchedule` but never defined)
- Clarified LOD assignment model: nation-level in scenario file; province inheritance; one exception for player starting province
- Clarified LOD 0 scope: player's home nation only (Part 1 description implied "adjacent trade partners" also at LOD 0)
- Resolved Province count Open Question: 6 provinces at LOD 0 for V1
- Elevated LOD 1 tick spec and LOD 2 batch job spec to Bootstrapper blockers (Part 8)
- Added `real_world_reference` visibility note (ships in world.json; not obfuscated)
- Added `quantity_scale_factor` to scenario file format
- Added `precipitation_seasonality`, `temperature_min_c`, `temperature_max_c` to ClimateProfile
- Replaced LOD assignment example in scenario file with explicit `default_lod_1_nations` / `default_lod_2_nations` model
- Removed "LOD 0 covers adjacent trade partners" claim from Part 1

---

*EconLife — World Map & Geography Specification v1.1*
*Option B: Real-world geography, fictional names, fully moddable scenario system*
*Architecture preserves V1 scope; expands strategic context globally from game start*
