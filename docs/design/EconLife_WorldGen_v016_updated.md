# EconLife — Procedural World Generation
*Companion document to GDD v1.7, Technical Design v29, World Map & Geography v1.2*
*Version 0.18 — Pass 5 Session 8: All TDD references updated to v29*

---

## Status and Scope

This document specifies the procedural world generation pipeline — an alternative world source to the GIS-seeded real-world pipeline described in World Map & Geography v1.1. Both pipelines produce identical output format (`world.json`) and are consumed identically by the engine.

**Tier classification:** Procedural world generation is **V1** per the Feature Tier List (updated per design decision, March 2026). This document specifies the V1 implementation. Both the 11-stage pipeline and `WorldGenParameters` are in scope for the V1 build. `H3Index` and `ProvinceLink` are integrated into TDD v29 §12 Province and Nation. The GIS-seeded pipeline (World Map & Geography v1.1) and this procedural pipeline are alternative world sources that produce identical `world.json` output — the engine consumes both identically.

**Design principle:** The generator runs **once** at new world creation, before gameplay begins. It simulates the physical processes that shaped Earth — tectonics, erosion, hydrology, atmospheric circulation — not as real-time simulation but as a staged pipeline whose outputs are geologically coherent. The goal is a world that feels like it has a billion years of history behind it without the player ever seeing the pipeline run.

**End date:** The game has no forced end state (GDD §19, Feature Tier List). The simulation starts in January 2000 and runs indefinitely — through generational play, heir succession, and Era 5 and beyond. The five-era arc in the R&D document describes the baseline historical trajectory the world follows; it is not a ceiling. Planetary and multi-body content in this document is forward architecture for expansions set in that open-ended future.

---

## Pipeline Overview

```
WorldGenPipeline {
    Stage 1:  generate_plates()           → plate boundaries, rock types, raw elevation
    Stage 2:  simulate_erosion()          → river carving, glacial sculpting, terrain shaping
    Stage 3:  calculate_hydrology()       → river networks, drainage basins, lakes, deltas
    Stage 4:  simulate_atmosphere()       → precipitation, temperature, rain shadows, ocean currents
    Stage 5:  classify_soils()            → soil types from geology + climate + time
    Stage 6:  assign_biomes()             → forest coverage, agricultural base, timber
    Stage 7:  place_special_features()    → karst, fjords, atolls, permafrost, impact craters, etc.
    Stage 8:  seed_resources()            → deposits weighted by tectonic context + soil + terrain
    Stage 9:  seed_population()           → settlement attractiveness + historical variance
    Stage 10: generate_world_commentary() → named features, province histories, pre-game events, loading text
    Stage 11: output_world_json()         → identical format to GIS pipeline output
}
```

Each stage writes province fields. Later stages read earlier-stage fields. The dependency chain is strict and one-directional — no stage reads from a later stage. This makes each stage independently unit-testable and the full pipeline reproducible from a seed.

**Target runtime:** Sub-120 seconds on any modern consumer CPU is considered good. The erosion simulation is the most computationally expensive step and can be parallelized across provinces or run at reduced iteration count for faster generation. Stage 11 (World Commentary) adds meaningful additional time but produces richer output that justifies it — its runtime is tunable via commentary depth settings in the scenario file. A loading screen of 60–90 seconds with active commentary on screen is the target player experience.

---

## Globe Discretization — Spatial Grid Architecture

### Decision: H3 Hierarchical Hexagonal Grid

The world is a sphere. Every scheme to divide it is a compromise between equal area, equal shape, adjacency uniformity, hierarchical decomposition, and computational simplicity. The chosen scheme is **Uber's H3 hexagonal hierarchical spatial index**.

**Why not a flat hex grid (Civ model):** A cylindrical projection introduces severe polar distortion — hexes near the poles represent far less real-world area than equatorial hexes. More fundamentally, EconLife provinces are economic and political units, not movement tiles. Hex grids solve problems EconLife doesn't have (uniform unit movement) while creating problems it doesn't need (wrong scale, sphere tiling exceptions).

**Why not irregular Voronoi only:** Voronoi provinces have no clean hierarchy. Region grouping of provinces is arbitrary. Parent-child relationships cannot be computed from an index — they must be stored explicitly.

**Why H3:** H3 divides the sphere into hexagonal cells at 16 resolution levels. Each cell at resolution N contains exactly 7 children at resolution N+1. The parent of any cell is computed in O(1) from the cell's 64-bit index. Neighbors are computed in O(1). This hierarchy maps directly onto EconLife's simulation LOD system — Province → Region → Nation is H3 resolution 5 → 3 → 2. No separate concept needed.

### Resolution Stack

The simulation uses H3 resolutions 2–9. Resolutions 10–15 (individual building parcels) are rendering concerns handled by the asset system, not simulation concerns.

| H3 Res | Avg cell area | Avg edge length | Global count | EconLife concept |
|---|---|---|---|---|
| 2 | 86,745 km² | ~300 km | ~490 | Nation (LOD index) |
| 3 | 12,393 km² | ~130 km | ~3,400 | Large region / state |
| 4 | 1,770 km² | ~50 km | ~24,000 | Province / county — RegionalMarket unit |
| 5 | 252 km² | ~20 km | ~170,000 | Metro area / city + hinterland |
| 6 | 36 km² | ~7 km | ~1,200,000 | City sector / district |
| 7 | 5.2 km² | ~3 km | ~8,400,000 | Neighborhood |
| 8 | 0.74 km² | ~1.2 km | ~59,000,000 | Superblock / 10-min walk |
| 9 | 0.10 km² | ~450 m | ~415,000,000 | City block — facility placement unit |

**Res 4 is the base province unit** — consistent with the existing TDD spec ("V1 ships with 6 provinces at LOD 0"). Res 9 is the fine raster used during pipeline generation and the eventual facility placement unit for close-in simulation. The pipeline runs physics on a res-9 raster, aggregates to res-4 provinces, and discards the raster. The simulation never operates below res 4 in V1.

### Dual-Scale Architecture

The pipeline uses two representations for two different purposes:

**Fine raster (res 9) — generation only:** Climate propagation, erosion simulation, hydrology tracing, and resource seeding all run on the res-9 grid during world generation. This gives the physics sufficient resolution to produce geologically coherent outputs. The raster is a pipeline tool — it is computed once and then aggregated upward.

**Province graph (res 4) — simulation:** After generation, raster values are spatially aggregated to res-4 cells. The simulation operates entirely on the res-4 province graph. No raster is retained at runtime.

```
Generation:   res-9 raster → physics pipeline → aggregate → res-4 provinces → world.json
Runtime:      res-4 province graph only; res-9 raster discarded after generation
```

This is architecturally identical to how the GIS pipeline already works: WorldClim climate data is a 1km raster, spatially joined and averaged to province level. The raster is a tool, not a simulation data structure.

### SimCell: Province Indexed by H3

The existing `Province` struct gains an `H3Index` as its primary identifier, replacing the arbitrary `uint32_t id`. All parent/child/neighbor relationships are computed from the index rather than stored explicitly.

```cpp
#include "h3/h3api.h"   // Uber H3 C library; C++ bindings available

struct SimCell {
    H3Index      h3_index;       // 64-bit; encodes position, resolution, full hierarchy
    uint8_t      resolution;     // 2–9; stored for fast access; derivable from h3_index

    // All existing Province fields follow unchanged.
    // Parent:    h3ToParent(h3_index, resolution - 1)     — O(1); no storage needed
    // Children:  h3ToChildren(h3_index, resolution + 1)   — O(1); returns 7 cells
    // Neighbors: gridDisk(h3_index, 1)                    — O(1); returns 5 or 6 cells
};
```

The `WorldState` province list becomes a flat map keyed by `H3Index`:

```cpp
std::unordered_map<H3Index, SimCell> provinces;
```

Lookup by H3 index is O(1). Neighbor enumeration requires no stored adjacency list — it is computed on demand from the H3 library.

### ProvinceLink: Adjacency With Physical Properties

Physical properties of the connection between two provinces (terrain cost, infrastructure, link type) cannot be derived from the H3 index alone. These are stored in a `ProvinceLink` struct that replaces the flat `adjacent_province_ids` list currently in the TDD.

```cpp
enum class LinkType : uint8_t {
    Land,       // shared border; standard overland transit
    Maritime,   // sea crossing; requires port on both ends or coastal access
    River,      // navigable river connection; lower cost than land; seasonal variation
};

struct ProvinceLink {
    H3Index  neighbor_h3;          // the adjacent province
    LinkType type;                 // Land | Maritime | River
    float    shared_border_km;     // longer shared border = stronger economic coupling
    float    transit_terrain_cost; // 0.0–1.0; derived from terrain between centroids
                                   // 0.0 = flat road; 1.0 = impassable without infrastructure
    float    infrastructure_bonus; // 0.0–1.0; built roads/rail reduce effective cost
};

// On SimCell, replacing adjacent_province_ids:
std::vector<ProvinceLink> links;
```

**Maritime links are first-class.** An island province with `island_isolation = true` has only `Maritime` links in its `links` vector — no `Land` links. The prior `adjacent_province_ids` model broke for island provinces because it implied all adjacency was land-border. This resolves the pre-existing [PRECISION] gap.

**Transit cost formula:**

```
effective_transit_cost(link) =
    link.transit_terrain_cost
    × terrain_type_multiplier[province.terrain_type]
    × (1.0 - link.infrastructure_bonus)
    × link_type_base_cost[link.type]   // Land=1.0; River=0.6; Maritime=0.4 base
```

**Mountain pass detection:** During Stage 2 erosion, if a province has `terrain_roughness > 0.7` and two or more neighbors on opposite sides have significantly lower elevation, the province is flagged `is_mountain_pass = true`. Its `ProvinceLink` entries to the low-elevation neighbors get `transit_terrain_cost` reduced to reflect the pass — it is costly to cross the mountain range but the pass is the least costly crossing point. All other links through the range retain full terrain cost. This creates the chokepoint mechanic without hardcoding it.

### The Pentagon Problem

H3 has exactly 12 pentagon cells per resolution level (5-neighbor cells instead of 6). At res 4 that is 12 pentagons among ~24,000 cells — 0.05%.

**Strategy: Ocean placement (Option A).** H3's icosahedron orientation can be chosen such that the 12 fixed pentagon locations fall in deep ocean cells. For a fictional world using H3's mathematics but not Earth's geography, the orientation is freely chosen at world generation time. The generation step checks each pentagon location against the landmass map and rotates the H3 orientation until all 12 fall on ocean cells or on uninhabited polar regions.

**Fallback:** Any pentagon that cannot be steered to ocean (in worlds with unusual landmass configurations) is handled transparently — 5 links instead of 6, dynamic neighbor count in all neighbor-iterating code. No special simulation logic required; the pentagon is just a cell with one fewer neighbor.

```cpp
// Pentagon check at world load:
for each H3Index cell at res 4:
    if isPentagon(cell) AND cell is land:
        log_warning("Pentagon on land at {cell}; simulation proceeds with 5 neighbors");
        // No further action needed; ProvinceLink vector will have 5 entries instead of 6
```

### H3 Pentagon Handling [CORE]

**Problem:** H3 has exactly 12 pentagon cells per resolution level. At resolution 4 (province level), pentagons are guaranteed to exist. Without explicit handling, these cells break hexagonal assumptions (6 neighbors instead of 5). The generation pipeline must handle them deterministically.

**Detection:**
```cpp
bool is_pentagon = h3_is_pentagon(cell_index);  // H3 library function; O(1)
```

**Treatment:**

Pentagon cells use 5-neighbor adjacency instead of 6:
- All area calculations use actual cell area via `h3_cell_area_km2(cell_index)`, not hexagon approximation
- Coastline generation: pentagon boundary segments = 5, not 6
- Interpolation: use area-weighted average of 5 neighbors, not 6

**Determinism guarantee:**

Pentagon positions are **fixed by H3 specification** — not seed-dependent, not procedurally determined. They occur at the same cell indices regardless of world configuration.

At resolution 4 (province level), the 12 base cells containing pentagons are:

```
Pentagon cell base indices at H3 res 4:
0x0400000000000000 through 0x0400000000000011 (hex)
(specific indices are H3 spec; not procedurally generated)
```

These are ALWAYS the same across all runs and worlds using H3 with the same resolution and orientation.

**Pipeline integration (Stage 1 or preprocessing):**

```cpp
// At world generation init (before any stage runs):
void h3_preprocessing() {
    // Load fixed pentagon cell list from H3 specification
    std::vector<H3Index> pentagon_cells = h3_get_pentagons(RESOLUTION_4);

    for (H3Index pentagon : pentagon_cells) {
        // Check if this pentagon falls on land or ocean (after tectonic stage)
        SimCell& cell = get_cell_by_h3_index(pentagon);

        if (cell.is_ocean) {
            // Ideal case: pentagon is in ocean; no special handling needed
            cell.is_pentagon_ocean = true;
        } else {
            // Pentagon is on land: log warning but proceed normally
            cell.is_pentagon_land = true;
            log_warning("Pentagon cell on land at H3Index {0}; will use 5 neighbors", pentagon);
        }

        // Compute 5 or 6 neighbors (function handles pentagon case)
        cell.neighbor_count = h3_neighbor_count(pentagon);  // Returns 5 or 6
        cell.neighbors = h3_grid_disk(pentagon, 1);        // Always works; returns N=5 or 6
    }
}

// During adjacency computation (Stage 1):
void compute_province_links(const SimCell& cell) {
    // Works for both hexagons (6 neighbors) and pentagons (5 neighbors)
    std::vector<H3Index> neighbors = h3_grid_disk(cell.h3_index, 1);

    for (H3Index neighbor_index : neighbors) {
        // Create ProvinceLink for each neighbor
        // No special logic needed; ProvinceLink vector naturally has 5 or 6 entries
        create_province_link(cell, neighbor_index);
    }
}
```

**Field additions to SimCell:**

```cpp
struct SimCell {
    // ... existing fields ...

    // Pentagon metadata (added)
    bool   is_pentagon;        // True if this cell is one of H3's 12 fixed pentagons
    bool   is_pentagon_ocean;  // True if pentagon falls on ocean (ideal)
    bool   is_pentagon_land;   // True if pentagon falls on land (fallback case)
    uint8_t neighbor_count;    // Will be 5 (pentagon) or 6 (hexagon); stored for debug
};
```

**Area calculation adjustment:**

When calculating province area, `geometry_profile.area_km2` must use `h3_cell_area_km2()` (actual cell area) instead of assuming hexagonal area formula. For hexagons this produces the same result; for pentagons it produces the correct 5-sided area:

```cpp
// Instead of:
//   area_km2 = HEXAGON_APPROX_AREA * (EARTH_RADIUS / resolution_level)
//
// Use:
geometry_profile.area_km2 = h3_cell_area_km2(h3_index);  // Always correct
```

**Coastline generation:**

When tracing coastlines between provinces, iteration over cell boundaries is naturally correct:

```cpp
void trace_coastline(const SimCell& cell) {
    // Works for both hexagons (6 edges) and pentagons (5 edges)
    std::vector<LatLngEdge> edges = h3_cell_edges(cell.h3_index);

    for (const LatLngEdge& edge : edges) {
        // Each edge represents one boundary; pentagon has 5 edges, hexagon has 6
        if (is_water_boundary(edge)) {
            add_coastline_segment(cell, edge);
        }
    }
}
```

**Neighbor interpolation:**

When interpolating a value (e.g., elevation, climate) from neighboring cells, use area-weighted averaging:

```cpp
float interpolate_from_neighbors(const SimCell& cell,
                                  const std::function<float(const SimCell&)>& get_value) {
    std::vector<H3Index> neighbors = h3_grid_disk(cell.h3_index, 1);
    float total_weight = 0.0f;
    float weighted_sum = 0.0f;

    for (H3Index neighbor_h3 : neighbors) {
        const SimCell& neighbor = get_cell_by_h3_index(neighbor_h3);
        float weight = neighbor.geometry_profile.area_km2;  // Area-weighted, works for pentagons
        weighted_sum += get_value(neighbor) * weight;
        total_weight += weight;
    }

    return weighted_sum / total_weight;  // Correctly handles 5 or 6 neighbors
}
```

**Summary:**

Pentagon handling is **transparent to the simulation**. The H3 library reports the correct number of neighbors (5 or 6). All calculations that iterate over neighbors work correctly without special logic. Area calculations are correct when using `h3_cell_area_km2()`. No procedural special-casing is needed — pentagons are just cells with one fewer neighbor, and all code that iterates correctly handles variable neighbor count.

---

### What H3 Does Not Change

The existing `KoppenZone`, `GeographyProfile`, `ClimateProfile`, `ResourceDeposit`, and `RegionalMarket` structs are unchanged. H3 is the indexing and hierarchy system — it replaces `uint32_t id` and `adjacent_province_ids` but touches nothing else in the simulation data model. The GIS pipeline can adopt the same H3 indexing by spatially joining Natural Earth province centroids to H3 res-4 cells at pipeline time.

---

## Stage 1 — Deep Time: Tectonics

Everything downstream depends on this stage. Rather than simulating millions of years of plate motion literally, the pipeline simulates the **outputs** of that motion at the moment the world crystallizes into its starting state.

### Plate Generation

Place 5–10 tectonic plates as irregular Voronoi regions across the map. Each plate carries:

```cpp
struct TectonicPlate {
    uint32_t  id;
    vec2      velocity;        // direction and magnitude of drift
    float     density;         // oceanic (high, thin) vs. continental (low, thick)
    float     age;             // older crust is cooler, denser, more deeply eroded
    PlateType type;            // Oceanic | Continental | Mixed
};
```

Oceanic plates are denser. Where an oceanic plate meets a continental plate, the oceanic plate **subducts** (dives under). Where two continental plates meet, they **crumple upward**. Where plates pull apart, crust **rifts and spreads**.

### Boundary Classification

Every province is assigned a dominant tectonic context based on its position relative to plate boundaries. Boundary type is the most consequential classification in the pipeline — terrain, climate, resources, and hazards all derive from it.

```cpp
enum class TectonicContext : uint8_t {
    Subduction,            // oceanic under continental; Andes/Cascades/Japan analog
    ContinentalCollision,  // continent meets continent; Himalayas/Alps analog
    RiftZone,              // plates pulling apart; East Africa/Basin-and-Range analog
    TransformFault,        // plates sliding past; San Andreas/Dead Sea analog
    HotSpot,               // mantle plume through interior; Hawaii/Yellowstone analog
    PassiveMargin,         // old rifted edge far from active boundary; US East Coast analog
    CratonInterior,        // ancient stable interior; Canadian Shield/Siberian Platform analog
    SedimentaryBasin,      // subsided interior filled with sediment; oil and coal country
};
```

**Boundary type → game signature:**

| TectonicContext | Real analog | Terrain output | Resource signature | Hazard |
|---|---|---|---|---|
| Subduction | Andes, Cascades, Japan | Coastal mountain range, volcanic peaks, steep escarpment | Porphyry copper, gold, molybdenum, sulfur | Earthquakes, volcanic eruptions |
| ContinentalCollision | Himalayas, Alps, Appalachians | Massive interior range, rain shadow desert on lee side | Limestone, marble, gem deposits; no metal fluids | Landslides, ongoing uplift |
| RiftZone | East Africa, Rhine Graben | Elongated valley with escarpment walls, lakes at floor | Lithium, soda ash, rare earths, geothermal | Earthquakes, subsidence |
| TransformFault | San Andreas, Dead Sea | Linear valley, no significant mountains | Minimal; disrupts existing deposits | Frequent earthquakes |
| HotSpot | Hawaii, Iceland, Yellowstone | Isolated volcanic peaks or igneous plateau | Basalt, geothermal, sulfur | Eruption; large-scale = civilization event |
| PassiveMargin | US East Coast, West Africa | Gently shelving coastal plain, wide shelf | Offshore oil, gas, phosphate, fishing | None; tectonically stable |
| CratonInterior | Canadian Shield, Siberian Platform | Ancient worn-down hills, glacially scoured lake country | Iron ore, gold, uranium, diamonds, nickel | None; extremely stable |
| SedimentaryBasin | Permian Basin, North Sea | Flat to gently rolling; often inland sea remnants | Coal, crude oil, natural gas, potash, salt | Subsidence from extraction |

### Stage 1 Outputs Per Province

At the end of Stage 1, every province has:
- `tectonic_context` — enum value above
- `plate_age` — how long the crust has existed; old crust is more eroded, cooler, geologically quieter
- `tectonic_stress` — how active the boundary currently is; drives ongoing hazard probability
- `rock_type` — Igneous | Sedimentary | Metamorphic | Mixed (derived from context)
- `raw_elevation` — tectonic component only, before erosion; mountain ranges are sharp spikes at this stage
- `geology_type` — derived classification for use in deposit grade estimation (resolves pre-existing [BLOCKER] gap)

**`mantle_heat_flux` derivation from planetary age:**

`PlanetaryParameters.mantle_heat_flux` is not set in the scenario file as a raw number — it is derived from `planet_age_gyr` at Stage 1 init. Mantle heat has two components: **primordial heat** (leftover from accretion and differentiation) and **radiogenic heat** (from ongoing decay of U-238, Th-232, and K-40 in the mantle). Both decline with age, but on different curves.

```python
def derive_mantle_heat_flux(planet_age_gyr, planet_mass_earth=1.0):
    # Primordial heat: exponential decay with ~3 Gyr half-life
    # (cooling rate also scales with surface/volume ratio → mass)
    primordial = 0.038 × exp(-planet_age_gyr / 3.0) × (planet_mass_earth ^ 0.3)

    # Radiogenic heat: U-238, Th-232, K-40 contributions
    # Present-day Earth radiogenic flux ≈ 0.032 W/m²
    # Each isotope decays on its own half-life
    u238_flux  = 0.016 × 0.5 ^ (planet_age_gyr / 4.47)
    th232_flux = 0.010 × 0.5 ^ (planet_age_gyr / 14.05)
    k40_flux   = 0.006 × 0.5 ^ (planet_age_gyr / 1.25)
    radiogenic = (u238_flux + th232_flux + k40_flux) × (planet_mass_earth ^ 0.5)

    total = primordial + radiogenic
    return max(total, 0.003)  // floor: residual heat even on dead old planets
                               // Earth analog (4.6 Gyr): ≈ 0.065 W/m²  ✓
                               // Young planet (1.0 Gyr): ≈ 0.110 W/m²
                               // Old planet (8.0 Gyr):   ≈ 0.028 W/m²
                               // Ancient planet (12 Gyr): ≈ 0.018 W/m²
```

**Gameplay consequences of low mantle heat flux:**
- Lower `tectonic_stress` globally → fewer earthquake and eruption hazard events
- Lower hydrothermal circulation → reduced probability weights for hydrothermally-concentrated metals (copper, gold, zinc) in Stage 8
- Lower geothermal energy availability → `Geothermal` resource probability weights reduced proportionally
- More stable crust → higher proportion of `CratonInterior` provinces (ancient stable shields dominate)
- Tectonically quiet old worlds feel geologically settled — safe for infrastructure but mineralogically depleted in the resources that require active geology to concentrate

```cpp
enum class RockType : uint8_t {
    Igneous,       // volcanic zones; hard; weathers into fertile soil slowly
    Sedimentary,   // basins and margins; soft; hosts petroleum, coal, potash
    Metamorphic,   // collision zones; hard; hosts gem deposits, some metals
    Mixed,         // transition provinces; blend of adjacent types
};

enum class GeologyType : uint8_t {
    VolcanicArc,       // subduction-associated; porphyry copper systems
    GraniteShield,     // craton core; gold, uranium, iron ore
    GreenstoneBelt,    // ancient craton margins; gold, nickel, chromite
    SedimentarySequence, // layered basins; coal, oil, gas, evaporites
    CarbonateSequence, // limestone/dolomite; karst potential, marble
    MetamorphicCore,   // deep collision zones; gems, garnet, kyanite
    BasalticPlateau,   // flood basalt; geothermal; poor in metals
    AlluvialFill,      // river/lake sediment; sand, gravel, placer gold
};
```

---

## Stage 2 — Uplift vs. Erosion: Terrain Formation

Raw tectonic elevation is a spike map. Erosion rounds everything into the shapes we recognize. This stage runs a simplified erosion simulation across multiple processes.

### Parallelization Strategy

Stage 2 is the dominant pipeline cost — 60–80% of total generation time on Earth and Mars analogs. The erosion simulation is iterative and partially data-dependent, but most operations are embarrassingly parallel across provinces. The following strategy achieves the sub-120s target on 8-core hardware without GPU.

**What parallelizes freely (province-independent):**
- Uplift rate computation per province — reads only `tectonic_context`, `tectonic_stress`, `rock_type`; no cross-province dependency
- Chemical weathering classification — reads only local province fields
- Glacial erosion classification — reads only local `elevation_avg_m` and `latitude`
- Aeolian erosion type classification — reads only local fields
- Derived field computation (`terrain_type`, `arid_type`, etc.) at end of stage

**What has directional data dependency (must be staged):**
- Fluvial erosion flow simulation — each province needs its upstream neighbors' accumulated flow before computing its own. Process in *topological order* (high elevation first, draining downhill). Within each elevation band, provinces at the same height are independent and can be parallelized.

```python
# Fluvial parallelization: topological sort then parallel within each rank

def compute_flow_parallel(provinces):
    # 1. Sort provinces by elevation descending (headwaters first)
    sorted_by_elevation = topological_sort(provinces, key=elevation_avg_m, order=desc)
    
    # 2. Assign ranks: provinces with no upstream dependents = rank 0
    #    Each province's rank = 1 + max(rank of its upstream neighbors)
    ranks = assign_flow_ranks(sorted_by_elevation)
    
    # 3. Process each rank in sequence; within each rank, fully parallel
    for rank in range(max_rank + 1):
        rank_provinces = [p for p in provinces if ranks[p] == rank]
        parallel_for(rank_provinces, compute_fluvial_erosion)
        # rank N+1 cannot start until rank N completes
```

This is the *wavefront pattern* — the dependency frontier advances downhill one rank at a time. In practice, ranks are broad (many provinces at similar elevations) so parallelism is high even in the sequential portion.

**Stage 2 parallel work breakdown (Earth-analog):**

| Operation | Parallelizable | % of Stage 2 time | Strategy |
|---|---|---|---|
| Uplift computation | Fully | ~5% | `parallel_for` over all provinces |
| Fluvial erosion (flow accumulation) | Wavefront | ~55% | Topological rank; parallel within rank |
| Fluvial erosion (channel cutting) | Fully per-channel | ~15% | `parallel_for` over identified rivers |
| Glacial erosion | Fully | ~10% | `parallel_for` over high-latitude provinces |
| Aeolian erosion | Fully | ~8% | `parallel_for` over desert provinces |
| Chemical weathering | Fully | ~4% | `parallel_for` over all provinces |
| Derived field classification | Fully | ~3% | `parallel_for` over all provinces |

**Stage 3 (Hydrology):** Same wavefront pattern as fluvial erosion — drainage direction is already computed in Stage 2. Stage 3 reuses the topological rank ordering from Stage 2; no re-sort needed.

**Stage 4 (Atmosphere):** Rain shadow algorithm processes in prevailing-wind direction order (see Stage 4). Within each wind-direction sweep, provinces in the same column (perpendicular to wind) are independent and can be parallelized. This gives a 4–8× speedup on typical continent widths.

**Stages 5–9:** All province-independent. Fully parallel. Combined cost is 3–6s regardless of parallelization — not a bottleneck.

**Stage 10 (Commentary):** Province history generation is fully parallel — each province's history reads only its own fields. Text assembly is independent per province. The main sequential cost is the final encyclopedia JSON serialization.

**GPU path:** Stages 2 and 4 are the only stages where GPU acceleration is meaningful. The wavefront pattern maps to GPU compute shaders with synchronization barriers between ranks. GPU path targets 2–3× speedup over optimized CPU for these stages. Stages 1, 3, 5–10 are not GPU-accelerated — their cost is too low to justify the transfer overhead.

**Floating-point determinism across platforms:** The wavefront ordering is deterministic from the world seed. However, x86 and ARM CPUs produce different floating-point results for the same operations in edge cases (FMA instruction handling, denormal treatment). This means a world generated on one hardware platform may not reproduce identically on another.

**Resolution:** Use fixed-point arithmetic for the elevation field only — the one field that accumulates error across thousands of erosion iterations. All other fields (temperature, precipitation, attractiveness) accept platform-specific minor variance; they are not iteratively accumulated and their variance is below perceptible thresholds. Fixed-point elevation: store as `int32_t` in millimetres (range ±2,147,483 m, sufficient for any planetary body). Convert to float only for display.

This eliminates cross-platform divergence in the erosion simulation while keeping all other computations in standard floating-point.

### Uplift Rate

Mountains only exist where uplift outpaces erosion. Active boundaries (Subduction, ContinentalCollision) sustain high uplift — ranges stay sharp and tall. Ancient ranges (old ContinentalCollision with low `tectonic_stress`) have near-zero uplift — erosion has won, they are rounded and low (Appalachians, Urals analog).

```
net_elevation_change_per_iteration =
    uplift_rate[tectonic_context] × tectonic_stress
    - erosion_rate(elevation, precipitation, rock_hardness[rock_type])
```

### Fluvial Erosion (Rivers Carving Valleys)

Dominant shaper of most landscapes. Water flows downhill and cuts. Run multi-pass flow simulation:

1. Seed precipitation at high elevations (atmosphere not yet calculated; use latitude + elevation approximation)
2. Flow to steepest downhill neighbor each step
3. Accumulated flow above threshold = river channel
4. Rivers cut their channels: `elevation -= flow_power × rock_softness × iterations`
5. Rivers carry sediment to low points; deposition creates **alluvial fans** at mountain bases and **deltas** at coasts

**Outputs:** V-shaped valleys in mountains, meandering rivers in flatlands, deep canyon systems in arid uplifted plateaus (high and dry → rivers cut deep before rainfall can erode slopes), alluvial plains at the base of every mountain range.

**Canyon detection:** Province with `elevation_avg_m > 1500` and `terrain_roughness > 0.7` and `KoppenZone` in arid family → flag `has_canyon_system`. These become transit chokepoints with few crossing points.

### Glacial Erosion (Ice Sculpting)

Ice sculpts fundamentally differently from water. Simulate: any province above the **glaciation line** (elevation threshold varying with latitude) receives glacial modification.

| Glacial feature | Formation condition | Terrain output | Game implication |
|---|---|---|---|
| U-shaped valleys | Glacier carved valley | Wide flat bottom, steep walls | Better transit than V-valleys; scenic |
| Fjords | Glacial valley flooded at coast | Very deep water to shore | Exceptional port_capacity; poor inland access |
| Cirques | Glacier head bowl | Mountain lake at range crest | Freshwater; visual landmark |
| Arêtes | Between adjacent cirques | Knife-edge ridge | Extreme terrain_roughness; near-impassable |
| Drumlins / moraines | Glacial deposition | Rolling hills of mixed sediment | Thin poor soils; many lakes |
| Continental ice scour | Polar latitude; ice sheet | Flat, lake-dotted, thin soils | Canadian Shield / Scandinavia analog |

At polar latitudes, glaciation line drops to sea level — continental ice sheets scour everything flat. Result: thousands of lakes (fishing, freshwater), poor soil, exposed ancient minerals.

### Aeolian Erosion (Wind)

Dominates where vegetation is absent — deserts and polar regions.

| Feature | Formation | Game implication |
|---|---|---|
| Erg (sand sea) | High sediment supply + consistent wind | Zero agriculture; extreme transit difficulty |
| Reg (gravel plain) | Deflation removes fine material | Easier than erg; still no agriculture |
| Hamada (rocky plateau) | Bare rock, ancient | Lowest population density; possible groundwater |
| Loess deposits | Wind-blown silt downwind of glaciers/deserts | agricultural_productivity bonus; Chinese Loess Plateau / US Midwest / Pampas analog |
| Yardangs | Wind-carved rock ridges | terrain_roughness elevated along prevailing wind axis |

Loess provinces: apply `agricultural_productivity` bonus during Stage 6. Erg provinces: `arable_land_fraction = 0.0`; `transit_terrain_multiplier` elevated.

### Chemical Weathering

In warm, wet climates, rock dissolves.

| Feature | Condition | Output |
|---|---|---|
| Karst | rock_type == Sedimentary (Carbonate) AND precipitation_mm > 600 | Sinkholes, caves, disappearing rivers; set `has_karst = true` |
| Laterite soils | Tropical (Af/Am/Aw) AND plate_age > HIGH | Iron/aluminum-rich red soils; bauxite deposits; low agricultural_productivity despite appearance |
| Tropical forest soils | Af/Am + high forest_coverage | Nutrients locked in biomass; deforestation → rapid agricultural_productivity collapse |

**Karst gameplay note:** Cave networks are a load-bearing world feature for smuggling routes and hidden facilities. `has_karst` flag must be preserved through to Province struct.

### Stage 2 Derived Fields

```cpp
// Added to GeographyProfile at end of Stage 2:
float     terrain_roughness;        // 0.0 flat → 1.0 mountainous; derived from elevation gradient
float     elevation_avg_m;          // post-erosion elevation
TerrainType terrain_type;           // derived enum; see below
bool      has_canyon_system;        // transit chokepoint flag
bool      has_karst;                // cave network flag; smuggling/facility implication
bool      is_mountain_pass;         // true if high-roughness cell with low-elevation neighbors
bool      former_river_valley;      // true if drainage analysis detected a palaeochannel axis
                                    // (elongated low perpendicular to coast or following gradient)
                                    // set by Stage 3 drainage basin pass; used by Stage 7 Ria detection
                                    // on opposite sides; reduces transit_terrain_cost on pass links
AridType  arid_type;                // erg/reg/hamada; only populated for desert provinces

enum class TerrainType : uint8_t {
    Flatland,
    RollingHills,
    Mountains,
    HotDesert,
    ColdDesert,
    CoastalPlain,
    Archipelago,
    RiverDelta,
    Plateau,
    ArcticTundra,
    RainforestBasin,
    GlacialLakeCountry,   // scoured craton; many lakes; thin soil
    VolcanicHighland,
    Badlands,             // heavily eroded soft sedimentary rock; complex ravines; near-zero agriculture
    Estuary,              // tidal mixing zone; river meets sea; unique ecology; sheltered water
    RiaCoast,             // drowned river valleys; natural harbours without glaciation
};

enum class AridType : uint8_t {
    Erg,       // sand sea; very difficult transit
    Reg,       // gravel plain; difficult transit
    Hamada,    // rocky plateau; moderate transit
    SaltFlat,  // evaporite basin; flat but hazardous
};
```

`TerrainType` is derived at pipeline time from `{terrain_roughness, elevation_avg_m, KoppenZone, coastal_length_km, arable_land_fraction}` — it is the human-readable label the UI and Bootstrapper need. It is not sourced separately.

---

## Stage 3 — Hydrology: Where Water Goes

With terrain established, run a full hydrological simulation.

### Drainage Basin Calculation

Every province drains to somewhere. Algorithm:
1. For each province, find lowest-elevation neighbor
2. Follow chain until reaching ocean or local minimum (closed basin)
3. Sum all upstream provinces → **catchment area**
4. River size = f(catchment_area, precipitation_mm)

**Outputs:**
- `river_access` — 0.0–1.0; navigable river density; high on major river trunks, low at headwaters
- Drainage basin membership — used in Stage 4 evapotranspiration and in climate event propagation

### Special Hydrological Features

**Endorheic basins:** No outlet to ocean. Water accumulates → lakes if enough precipitation; salt flats/playas if arid. Caspian / Aral / Great Salt Lake / Dead Sea analogs. Flag `is_endorheic = true` on basin provinces. Economic significance: lithium brines, potash, salt extraction, fishing if lake survives.

**Alluvial fans:** Where a steep mountain river meets a flat plain and dumps sediment. Fan-shaped deposit at mountain base. Detection: province at foot of `terrain_roughness > 0.6` neighbor where elevation drops > 500m over one province. Apply `agricultural_productivity` bonus and `groundwater_reserve` bonus (porous sediment traps water).

**Floodplain meanders:** Rivers crossing flat terrain meander. Wide flat floodplains flood seasonally. High `agricultural_productivity` (natural fertilization), high `flood_vulnerability`, low `infrastructure_rating` (road construction cost on wetlands). Mesopotamia / Mekong delta / Mississippi bottomlands analogs.

**Oxbow lakes:** Meander loops that cut off. Minor feature; contributes to fisheries in river valley provinces.

**River deltas:** Where major rivers (catchment area above threshold) reach the coast. Flag `is_delta = true`. Apply: `agricultural_productivity` cap override to 0.85+; `flood_vulnerability` elevated; `port_capacity` moderate (shallow water requires dredging).

---

### Snowpack Hydrology

**The mechanism:** Mountain provinces above the seasonal snowline accumulate precipitation as snowpack over winter rather than delivering it immediately to rivers. Snowmelt in spring and early summer produces a delayed, concentrated pulse of freshwater that feeds rivers far downstream — including rivers flowing through arid zones that receive almost no local precipitation.

This is load-bearing for arid province habitability. The Colorado, Indus, Amu Darya, Syr Darya, and Yellow Rivers all flow through desert or semi-desert lowlands sustained entirely by snowmelt from distant mountains. Without modelling snowpack separately from precipitation, downstream arid provinces would be assigned zero `river_access` — which is geographically false and breaks settlement attractiveness for some of history's most important civilisations.

**Snowline derivation:** The seasonal snowline elevation is determined at Stage 3 from latitude and `temperature_avg_c` (the Stage 4 atmospheric pass has not yet run; use the pre-atmosphere latitude-elevation approximation from Stage 2's fluvial erosion seed):

```python
def seasonal_snowline_m(latitude_deg, planet):
    # Snowline drops ~150m per degree of latitude from equator
    # At equator: ~5,000m (tropical glaciers); at 60°: ~500m; at 70°+: sea level
    base_snowline = max(0.0, 5000.0 - abs(latitude_deg) × 75.0)
    
    # Adjust for planetary solar constant relative to Earth
    solar_ratio = planet.solar_constant_wm2 / 1361.0
    base_snowline *= sqrt(solar_ratio)   # warmer star raises snowline
    
    return base_snowline
```

**Snowpack computation per province:**

```python
def compute_snowpack(province, planet):
    snowline = seasonal_snowline_m(province.latitude, planet)
    
    if province.elevation_avg_m < snowline:
        province.snowpack_contribution = 0.0
        return
    
    # Fraction of province area above snowline (approximated from elevation distribution)
    snow_fraction = clamp(
        (province.elevation_avg_m - snowline) / 1000.0,
        0.0, 1.0
    )
    
    # Winter precipitation that accumulates as snow rather than running off immediately
    # Spring melt releases it as a concentrated pulse
    province.snowpack_contribution = (
        snow_fraction
        × province.precipitation_mm   # pre-atmosphere approximation; refined in Stage 4
        × SNOWPACK_RETENTION_FACTOR   // config: ~0.70 (70% of high-altitude precip held as snow)
    )
    
    # Province's own snowpack feeds its local rivers in spring
    province.river_access += snowpack_to_river_contribution(province.snowpack_contribution)
```

**Downstream propagation:** After all provinces have their `snowpack_contribution` computed, propagate meltwater downstream through the drainage basin graph using the same topological order as fluvial erosion:

```python
def propagate_snowmelt(provinces, drainage_graph):
    # Process in topological order (headwaters first)
    for province in topological_order(provinces):
        if province.snowpack_contribution == 0.0:
            continue
        
        # Distribute meltwater to downstream provinces proportionally to drainage path
        downstream = drainage_graph.downstream_of(province)
        melt_volume = province.snowpack_contribution × province.area_km2
        
        for downstream_province in downstream:
            distance_decay = exp(-downstream_province.distance_from_source_km / MELT_DECAY_KM)
            downstream_province.river_access += (
                melt_volume × distance_decay / downstream_province.area_km2
                × SNOWMELT_TO_RIVER_ACCESS_SCALE
            )
            downstream_province.snowmelt_fed = True  // flag: river here is snowmelt-sustained
```

**`snowmelt_fed` flag:** A downstream province with `snowmelt_fed = True` receives `river_access` unjustified by its local precipitation. The flag is used in:
- Stage 9: settlement attractiveness formula uses `river_access` directly; the snowmelt origin is transparent
- Stage 10.2: province histories note "sustained by meltwater from the [named range]" as a historical fact
- Simulation: drought events in snowmelt-fed provinces are modulated by snowpack levels in their upstream mountain provinces, not local rainfall

**Seasonal river timing:** Snowmelt-fed rivers peak in late spring / early summer regardless of local rainfall season. This matters for agriculture timing and flood vulnerability. Store as:

```cpp
enum class RiverFlowRegime : uint8_t {
    RainfedPerennial,    // flow tracks local rainfall; peaks in wet season
    SnowmeltPerennial,   // sustained by snowmelt; peaks late spring; may drop in autumn
    SnowmeltEphemeral,   // only flows during melt season; dry rest of year
    RainfedEphemeral,    // only flows during local wet season; wadi/arroyo analog
    Glacierfed,          // sustained by glacier melt; stable through summer; declining long-term
};
```

`RiverFlowRegime` is set on the drainage basin's trunk river and inherited by downstream provinces. Glacierfed rivers are snowmelt-fed rivers whose source glacier is large enough to maintain flow year-round — these are stable now but decline under climate stress as glacier mass decreases.

---

### Springs, Seeps, and Artesian Access

**The mechanism:** Where the water table intersects terrain — on hillsides where impermeable rock forces groundwater to the surface, or where confined aquifers under pressure reach a natural vent — springs and seeps appear. These are the reason some settlements exist in landscapes where no surface river is visible.

The Saharan oases (Siwa, Dakhla, Kharga) are not river-fed — they sit above artesian basins where ancient groundwater is under sufficient pressure to reach the surface. Desert civilisations in Arabia, the American Southwest, and the Atacama all depended on spring-fed water that the surface precipitation record cannot explain.

**Physics:** An artesian spring forms where a confined aquifer (permeable rock layer between impermeable layers) is recharged at a high-elevation outcrop and pressurised by the water column. At topographic lows where the confining layer is breached, water vents to the surface without pumping.

**Detection algorithm:**

```python
def detect_springs(province, all_provinces):
    # Condition 1: groundwater_reserve is elevated (aquifer present)
    if province.groundwater_reserve < 0.35:
        return  # insufficient aquifer
    
    # Condition 2: Province is topographically lower than its groundwater recharge zone
    # Recharge zone = any upgradient neighbor with elevation > province + 200m
    #                 AND precipitation sufficient to charge the aquifer
    recharge_neighbors = [
        n for n in province.neighbors
        if n.elevation_avg_m > province.elevation_avg_m + 200
        and n.precipitation_mm > 400
        and n.rock_type in {Sedimentary, Mixed}   // permeable recharge rock
    ]
    if not recharge_neighbors:
        return
    
    # Condition 3: Province itself has low-permeability surface layer
    # (confining layer present — impermeable cap trapping pressurised water below)
    if province.rock_type == Igneous and province.geology_type == GraniteShield:
        return  # no confining layer; water disperses rather than pressurising
    
    # Spring detected
    province.has_artesian_spring = True
    province.spring_flow_index = (
        province.groundwater_reserve
        × mean(n.precipitation_mm for n in recharge_neighbors) / 1000.0
        × ARTESIAN_FLOW_SCALE    // config value
    )
```

**Gameplay effects of `has_artesian_spring = True`:**

```python
# In Stage 9 settlement attractiveness — applied after main formula
if province.has_artesian_spring:
    # Spring provides water access independent of surface river
    # Effect is smaller than a major navigable river but enables settlement where river_access = 0
    attractiveness += spring_flow_index × 0.25
    
    # Desert provinces with springs are oasis settlements
    if KoppenZone in {BWh, BWk, BSh, BSk}:
        attractiveness += spring_flow_index × 0.15   // oasis bonus; disproportionate in desert
        province.is_oasis = True
```

**Oasis provinces** (`is_oasis = True`) receive special treatment in Stage 10:
- Named features: the spring itself is named via the language system as a landmark
- Province history: settlement explicitly tied to water access in `current_character` text
- NPC context: water rights over oasis springs generate `DiplomaticContextEntry` — historically the most contested resource in desert regions

**Groundwater depletion (simulation-time mechanic):** `spring_flow_index` declines if upstream recharge zones are deforested (reduced infiltration) or if climate stress reduces precipitation in the recharge zone. Ancient aquifers (Ogallala, Nubian Sandstone, Great Artesian Basin analogs) recharge on geological timescales — once depleted they do not recover within any game-relevant period. `groundwater_reserve` tracks depletion during play; if it falls below a threshold, `has_artesian_spring` is cleared.

---

### Port Capacity Baseline

`port_capacity` is used in Stage 9 settlement attractiveness (weight 0.20 — the second largest single term) and in three Stage 10 archetype conditions. Stage 7 special terrain features (fjords, estuaries, ria coasts, atolls, barrier islands, deltas) override it with specific values. This baseline pass runs first, in Stage 3, so those overrides replace rather than stack.

```python
def seed_port_capacity_baseline(province):
    """
    Sets port_capacity for all provinces. Stage 7 special terrain passes
    subsequently override this value for qualifying provinces.
    Non-coastal provinces: 0.0. Coastal provinces: 0.0–1.0 from formula.
    """
    if province.coastal_length_km == 0:
        province.port_capacity = 0.0
        return

    # Coastal length: more coastline = more potential anchorage sites.
    # Normalised to 1.0 at 150 km (a medium-sized coastal province).
    coast_factor = clamp(province.coastal_length_km / 150.0, 0.0, 1.0)

    # Terrain penalty: cliffs and steep ground prevent harbour construction.
    # High terrain_roughness → exposed ledge coasts rather than sheltered beaches.
    terrain_factor = 1.0 - province.terrain_roughness × 0.50

    # Elevation penalty: low-lying coasts are buildable; high plateau meeting sea
    # is sheer cliff. Capped at 0.5 reduction to avoid eliminating all cliff coast value.
    elevation_factor = 1.0 - clamp(province.elevation_avg_m / 2000.0, 0.0, 0.50)

    # River mouth bonus: where a navigable river meets the coast, a natural deep-water
    # approach forms. River deltas are shallow (overridden in Stage 7) but river mouths
    # on hard rock coasts become excellent natural harbours.
    river_mouth_bonus = province.river_access × 0.25

    province.port_capacity = clamp(
        coast_factor × terrain_factor × elevation_factor + river_mouth_bonus,
        0.0, 1.0
    )
```

**Stage 7 override values** (replace the baseline; not additive):

| Special terrain | port_capacity range | Notes |
|---|---|---|
| Fjord | 0.85–0.95 | Very deep; sheer-walled; exceptional natural harbour |
| Ria Coast | 0.70–0.90 | Multiple sheltered inlets; `port_count_potential` 2–8 |
| Estuary | 0.55–0.75 | Stable deep channel; tidal but reliable |
| Barrier Island (lagoon side) | 0.55–0.70 | Sheltered lagoon; not ocean-facing |
| River Delta | 0.35–0.55 | Shallow; shifting channels; requires constant dredging |
| Atoll | 0.40–0.60 | Lagoon harbour; limited by pass width |
| Island isolation (no special type) | ≥ 0.50 | Forced minimum; island must be accessible |

---

### Stage 3 Fields

```cpp
// Added to GeographyProfile at end of Stage 3:
bool             is_endorheic;             // no ocean drainage outlet
bool             is_delta;                 // major river delta
float            groundwater_reserve;      // 0.0–1.0; aquifer potential; alluvial fans and floodplains highest
float            snowpack_contribution;    // mm water equivalent held as seasonal snowpack; 0 below snowline
bool             snowmelt_fed;             // true if river_access includes significant snowmelt from upstream
RiverFlowRegime  river_flow_regime;        // seasonal timing of river flow; see enum above
bool             has_artesian_spring;      // pressurised groundwater reaches surface
float            spring_flow_index;        // 0.0–1.0; spring yield; drives oasis settlement bonus
bool             is_oasis;                 // desert province sustained by spring; named landmark
float            tidal_exposure;           // 0.0–1.0; tidal mixing intensity; derived from
                                           // coastal_length_km × ocean_body_size × coast_exposure_factor
                                           // used by Stage 7 Estuary detection threshold TIDAL_MIXING_THRESHOLD
float            port_capacity;            // 0.0–1.0; seeded by Stage 3 baseline formula for all provinces;
                                           // overridden in Stage 7 for special terrain (fjords, estuaries, rias, etc.)
float            flood_vulnerability;      // 0.0–1.0; initial values set here from terrain context:
                                           //   delta: elevated; floodplain: elevated; karst: low;
                                           //   endorheic basin: moderate; coastal plain: moderate
                                           // Stage 4 monsoon pass adds MONSOON_FLOOD_BONUS on top
```

---

## Stage 4 — Atmosphere: Climate Generation

### Hadley-Ferrel-Polar Cell Model

Earth's atmospheric circulation is driven by uneven solar heating. Implement as latitude bands with a base precipitation and temperature curve, then modify locally:

| Latitude band | Cell | Prevailing conditions |
|---|---|---|
| 0–10° | Equatorial ITCZ | Rising air; very high precipitation; Af/Am climate |
| 10–30° | Hadley descent | Subsiding dry air; subtropical deserts; BWh/BSh |
| 30–60° | Ferrel / Westerlies | Variable; west coasts wet; continental interiors dry |
| 60–90° | Polar | Descending cold air; ET/EF; polar deserts |

```
base_temperature_c = 30.0 - (abs(latitude) × 0.7)
altitude_correction = elevation_avg_m × -0.0065     // lapse rate: 6.5°C per 1000m
effective_temperature = base_temperature_c + altitude_correction

continentality = 1.0 - (1.0 / (1.0 + distance_to_coast_km × 0.005))
base_precipitation_mm = lerp(1200, 300, continentality)
```

### The Rain Shadow — Critical Algorithm

The most consequential atmospheric mechanism for creating terrain diversity. Moisture-laden air rises over mountains, cools, and deposits rain on the windward side; the leeward side receives dry descending air.

**The critical dependency: prevailing wind direction is latitude-dependent.** The rain shadow algorithm must process provinces in the correct upwind-to-downwind order, which reverses between latitude bands.

```
Prevailing wind by latitude band:

  0–10°  (ITCZ):         Variable; convective dominance; no persistent prevailing direction
  10–30° (Trade winds):  East → West  (Northeast trades in NH; Southeast trades in SH)
  30–60° (Westerlies):   West → East
  60–90° (Polar):        East → West  (Polar easterlies)
```

**Why this matters:** At 15°N latitude (Sahel, India, Caribbean), moisture arrives from the east on the trade winds. A mountain range at 15°N casts its rain shadow to the *west* — opposite to the same range at 45°N. The Western Ghats of India block the southwest monsoon (itself a trade-wind reversal) and create the Deccan rain shadow. The Ethiopian Highlands block east-to-west trade wind moisture, creating the Sahel gradient. Getting this wrong produces physically impossible climates — wet interiors and dry coasts at tropical latitudes.

**Special case — monsoon reversal:** Monsoon regions (10–25° latitude, adjacent to warm ocean) experience a seasonal wind reversal. In monsoon season, onshore flow overwhelms the trade wind direction. The rain shadow algorithm runs with trade wind direction for annual baseline, then the monsoon modifier in Stage 4 applies a separate wet-season orographic component on top. These stack — a monsoon coast blocked by a coastal range gets the annual trade wind rain shadow *and* a monsoon enhancement on the windward side.

```python
def prevailing_wind_direction(latitude_deg):
    """Returns (dx, dy) unit vector in the direction moisture travels."""
    abs_lat = abs(latitude_deg)
    hemisphere = 1 if latitude_deg >= 0 else -1
    
    if abs_lat < 8:
        return None  # ITCZ: convective; no persistent direction; skip rain shadow pass
    elif abs_lat < 30:
        # Trade winds: blow from east to west (easterly)
        return (-1, 0)   # moisture moves west; mountains shadow to their west
    elif abs_lat < 60:
        # Westerlies: blow from west to east
        return (1, 0)    # moisture moves east; mountains shadow to their east (standard)
    else:
        # Polar easterlies
        return (-1, 0)

def compute_rain_shadow(provinces, province_grid):
    # Group provinces by latitude band to determine processing order
    for lat_band_provinces in group_by_latitude_band(provinces):
        wind_dir = prevailing_wind_direction(lat_band_provinces[0].latitude)
        if wind_dir is None:
            continue   # ITCZ provinces: skip; precipitation set by convective formula only
        
        # Order provinces upwind → downwind along wind direction
        ordered = sort_by_wind_order(lat_band_provinces, wind_dir)
        
        moisture = 0.0
        for province in ordered:
            # Initialize moisture from ocean sources at windward coast
            if province.is_ocean_adjacent and is_upwind_coast(province, wind_dir):
                moisture = 1.0  # full oceanic moisture at windward shore
            
            orographic_lift = province.terrain_roughness × MOUNTAIN_LIFT_COEFFICIENT
            precipitation_deposited = moisture × orographic_lift × PRECIPITATION_EFFICIENCY
            
            province.precipitation_mm += base_precipitation_mm(province) × moisture
            moisture -= precipitation_deposited
            moisture = clamp(moisture, 0.0, 1.0)
            moisture += evapotranspiration_return(province)  # vegetation recycles moisture
```

**Resulting analogs (all emerge from physics without special-casing):**

| Analog | Latitude | Wind direction | Mechanism |
|---|---|---|---|
| Atacama Desert | 20–30°S | Trade winds (E→W) | Andes cast shadow westward; cold Humboldt current also suppresses |
| Gobi Desert | 40–50°N | Westerlies (W→E) | Himalayas/Tibetan Plateau cast shadow eastward |
| Thar Desert (India) | 25°N | Trade winds + monsoon | Aravalli Hills partially block; monsoon failure amplifies |
| Patagonian Desert | 40–50°S | Westerlies (W→E) | Southern Andes cast shadow eastward; same physics as Gobi |
| Sahel gradient | 10–18°N | Trade winds (E→W) | Ethiopian Highlands intercept east-to-west moisture |
| Great Basin | 38–42°N | Westerlies (W→E) | Sierra Nevada and Cascades cast shadow eastward |

**ITCZ provinces (0–8° latitude):** Precipitation is set by the convective formula only — strong solar heating drives powerful updrafts that pull surface air inward from all directions and release it as rain. No persistent wind direction means no orographic moisture transport in the conventional sense; instead, any mountain in the ITCZ triggers local convective enhancement on all sides. These provinces typically receive the world's highest precipitation regardless of terrain.

### Ocean Currents

Two current types, placed based on ocean/continent geometry:

**Warm currents** (Gulf Stream, Kuroshio analogs): carry tropical heat poleward along eastern ocean margins. Coastal provinces adjacent to warm currents: `temperature_avg_c += warm_current_delta`; precipitation elevated; winters mild relative to latitude. Northwestern Europe is habitable because of the Gulf Stream — without it, London's latitude matches Labrador.

**Cold upwelling currents** (Humboldt, Benguela, California analogs): upwelling cold water along western ocean margins at subtropical latitudes. Create coastal deserts; suppress rainfall; produce extraordinary fisheries.

```cpp
// Cold current provinces:
temperature_avg_c -= cold_current_delta
precipitation_mm  *= cold_current_precip_suppression   // 0.2–0.4× baseline
fish_biomass      += cold_current_fishery_bonus        // upwelling = nutrient-rich = fish
```

The Atacama and Namib deserts are both cold current deserts — arid not because of latitude alone but because cold ocean air holds little moisture and the upwelling suppresses convection.

### Monsoons

Detection rule: province in 10–25° latitude band AND adjacent to ocean AND prevailing summer wind crosses warm ocean surface before reaching land → apply monsoon modifier:

```cpp
precipitation_seasonality = 0.85 + random_variance(0.1)  // almost all rain in 3 months
flood_vulnerability       += MONSOON_FLOOD_BONUS
agricultural_productivity_variance += MONSOON_VARIANCE_PENALTY  // yield unpredictable
```

Gameplay implications: infrastructure not built for flooding takes seasonal damage; agriculture is high-yield when timed correctly but catastrophic if monsoon fails (drought event). India / Southeast Asia / West Africa Sahel analogs.

---

### Inter-Annual Variance: ENSO Analog

The monsoon modifier and rain shadow algorithm produce the correct *average* precipitation distribution. But year-to-year agricultural variance — the difference between a good year and a famine year — is driven primarily by a separate mechanism: the El Niño–Southern Oscillation and its planetary equivalents.

**Physics:** ENSO is a coupled ocean-atmosphere oscillation in the tropical Pacific. In El Niño phase, anomalously warm sea surface temperatures suppress the Walker circulation, shifting rainfall patterns globally: drought in Australia and southern Africa, flooding in coastal South America and East Africa, weaker Indian monsoon, reduced Atlantic hurricane activity. La Niña reverses these. The cycle runs 2–7 years and is the dominant source of inter-annual agricultural yield variance on Earth.

Any planet with a large ocean basin in the tropics will develop some equivalent oscillation. The strength scales with ocean basin size and tropical solar flux.

**World-gen parameter:** `ENSO_strength` is set at Phase 0 from planetary geometry — ocean coverage fraction and tropical surface area. It is a scalar multiplier on year-to-year precipitation variance during the simulation. It does not change the mean precipitation (that is set by the rain shadow and atmospheric cell model); it controls how much the actual precipitation in any given simulated year deviates from that mean.

```python
def derive_enso_strength(planet, ocean_coverage_fraction, tropical_ocean_area_km2):
    # ENSO requires: large tropical ocean basin; warm enough for SST anomalies
    # Minimum ocean coverage ~40% for significant ENSO; Earth ~71%
    if ocean_coverage_fraction < 0.35:
        return 0.10   // weak; small oceans damp the oscillation
    
    # Scale with tropical ocean area (larger basin = stronger oscillation potential)
    area_factor = clamp(tropical_ocean_area_km2 / EARTH_TROPICAL_OCEAN_KM2, 0.0, 1.5)
    
    # Scale with solar constant (warmer star → stronger SST anomalies possible)
    solar_factor = sqrt(planet.solar_constant_wm2 / 1361.0)
    
    enso_strength = 0.70 × area_factor × solar_factor
    return clamp(enso_strength, 0.05, 1.50)   // Earth ≈ 0.70
```

**Province-level ENSO susceptibility:** Not all provinces are equally affected. Susceptibility is highest in the trade wind belt (10–25° latitude) and diminishes at higher latitudes. Provinces adjacent to large ocean basins are more susceptible than interior continental provinces.

```python
def enso_susceptibility(province):
    abs_lat = abs(province.latitude)

    if abs_lat < 10:
        base = 0.90   // equatorial; directly in the oscillation zone
    elif abs_lat < 25:
        base = 0.80   // trade wind belt; core ENSO impact region
    elif abs_lat < 40:
        base = 0.50   // teleconnection zone; indirect impact
    elif abs_lat < 60:
        base = 0.25   // weak teleconnections only
    else:
        base = 0.05   // polar; essentially unaffected

    # Modulate by continentality: deep interiors are buffered from ocean signal
    # (the previous version placed this multiplication after all return statements,
    # making it unreachable dead code — P-01 fix: compute base first, then apply)
    return base × (1.0 - province.continentality × 0.40)
```

**Stored fields and simulation use:**

```cpp
// Added to Province at Stage 4:
// --- Core atmospheric outputs ---
float      temperature_avg_c;           // mean annual surface temperature in °C; from latitude/altitude
                                         // baseline; modified by warm/cold ocean current passes
float      continentality;              // 0.0 (fully oceanic) → 1.0 (deep continental interior);
                                         // derived from distance_to_coast_km via decay function;
                                         // drives precipitation baseline and ENSO susceptibility damping
float      precipitation_mm;            // mean annual precipitation in mm; set by Hadley-cell baseline,
                                         // then modified in-place by: rain shadow pass, monsoon modifier,
                                         // cold current suppression; Stage 3 snowpack derivation used a
                                         // pre-atmosphere approximation — this is the authoritative value
float      precipitation_seasonality;   // 0.0 = evenly distributed year-round (e.g. Cfb oceanic)
                                         // → 1.0 = almost all rain in ≤3 months (monsoon; extreme Aw)
                                         // set by monsoon modifier; default 0.15 for non-monsoon provinces

// --- Derivative vulnerability fields ---
float      flood_vulnerability;         // 0.0–1.0; combined flood exposure; initial values set in
                                         // Stage 3 (delta: elevated; floodplain: elevated; karst: low);
                                         // Stage 4 monsoon pass adds MONSOON_FLOOD_BONUS where applicable;
                                         // Stage 9.2 reads this to penalise infrastructure_rating
float      agricultural_productivity_variance; // 0.0–1.0; inter-annual yield instability from ENSO and
                                         // monsoon; a province at 0.80 variance risks famine in bad years
                                         // even if mean productivity is high; monsoon provinces: 0.35–0.65;
                                         // non-monsoon ENSO-susceptible: 0.10–0.30
float      geographic_vulnerability;    // 0.0–1.0; climate stress exposure; derived at Stage 4 from
                                         // KoppenZone, coastal_length_km, elevation_avg_m, tectonic_context;
                                         // not recalculated at runtime; see derivation table below

// --- Climate classification ---
KoppenZone koppen_zone;                 // climate zone assigned after all atmospheric modifiers applied;
                                         // referenced throughout as province.KoppenZone in code
KoppenZone paleoclimate_zone;           // copy of koppen_zone assigned at world-gen; never modified;
                                         // Stage 10.2 compares province.KoppenZone vs paleoclimate_zone
                                         // to detect ClimateShift events

// --- Ocean current outputs ---
bool       cold_current_adjacent;       // true if province is coastal and adjacent to a cold upwelling
                                         // current; drives fish_biomass bonus and precipitation suppression;
                                         // used by Stage 6 fisheries and Stage 10 archetype classifier
float      fish_biomass;                // 0.0–1.0; initial carrying capacity for fisheries;
                                         // set here for cold-current provinces (upwelling bonus);
                                         // further modified upward in Stage 7 for estuaries, fjords, atolls;
                                         // 0.0 for all non-coastal inland provinces

// --- ENSO ---
float      enso_susceptibility;         // 0.0–1.0; how much ENSO shifts this province's precipitation;
                                         // used by simulation's annual climate tick:
                                         // precip_actual = precip_mean × enso_factor

// Set at world-gen on PlanetaryParameters (Phase 0.6 / Stage 4 init):
float  enso_strength;         // 0.0–1.5; planet-level ENSO intensity; Earth ≈ 0.70
float  enso_cycle_years;      // mean cycle length; Earth 2–7 yr; draw from uniform(2.0, 7.0)
```

**During simulation:** Each year, the climate system draws an ENSO phase value from a bounded random walk seeded by the world seed and year. The `enso_factor` for each province is:

```python
enso_factor = 1.0 + enso_phase × enso_strength × province.enso_susceptibility × 0.40
// enso_phase: −1.0 (strong La Niña) to +1.0 (strong El Niño)
// Maximum swing on a susceptible province with Earth-strength ENSO: ±28% of mean precipitation
// This matches observed ENSO precipitation anomalies in core-affected regions
```

**Agricultural consequence:** A province whose mean precipitation puts it just above the viable crop threshold (250mm for wheat; 400mm for most food crops) becomes drought-vulnerable in moderate El Niño years. A province well above threshold is merely stressed. This is the mechanism by which ENSO creates famine risk: not by making wet regions dry, but by pushing marginal regions over the edge. The Sahel's famines and Australia's droughts are ENSO events hitting provinces already at the precipitation threshold.

### KoppenZone Assignment

After all atmospheric modifiers applied, assign `KoppenZone` from `{effective_temperature, precipitation_mm, precipitation_seasonality}`:

```cpp
enum class KoppenZone : uint8_t {
    Af = 0,   // Tropical rainforest:     temp > 18°C all months; precip > 60mm driest month
    Am = 1,   // Tropical monsoon:        temp > 18°C; seasonal but heavy wet season
    Aw = 2,   // Tropical savanna:        temp > 18°C; dry season < 60mm
    BWh = 3,  // Hot desert:              precip < 250mm; temp_avg > 18°C
    BWk = 4,  // Cold desert:             precip < 250mm; temp_avg < 18°C
    BSh = 5,  // Hot steppe:              precip 250–500mm; temp_avg > 18°C
    BSk = 6,  // Cold steppe:             precip 250–500mm; temp_avg < 18°C
    Cfa = 7,  // Humid subtropical:       no dry season; hot summer
    Cfb = 8,  // Temperate oceanic:       no dry season; warm summer
    Cfc = 9,  // Subpolar oceanic:        no dry season; cool summer
    Csa = 10, // Mediterranean hot:       dry hot summer; mild wet winter
    Csb = 11, // Mediterranean warm:      dry warm summer; mild wet winter
    Cwa = 12, // Subtropical monsoon:     dry winter; hot wet summer
    Dfa = 13, // Humid continental hot:   cold winter; hot summer; no dry season
    Dfb = 14, // Humid continental warm:  cold winter; warm summer; no dry season
    Dfc = 15, // Subarctic:               very cold winter; short cool summer
    Dfd = 16, // Extreme subarctic:       extremely cold winter
    ET = 17,  // Tundra:                  warmest month 0–10°C
    EF = 18,  // Ice cap:                 warmest month < 0°C
};
```

### Geographic Vulnerability (Resolves [PRECISION] Gap)

The climate stress formula requires a numeric `geographic_vulnerability` per province. Derive it at Stage 4 from `{KoppenZone, coastal_length_km, elevation_avg_m, tectonic_context}`:

| Condition | Vulnerability range | Rationale |
|---|---|---|
| KoppenZone in {EF, ET} OR elevation > 3000m | 0.85–1.0 (Very High) | Arctic/alpine; permafrost; ice melt |
| KoppenZone in {Af, Am} AND coastal_length_km > 50 | 0.80–0.95 (Very High) | Tropical coastal; sea level; extreme storms |
| KoppenZone in {BWh, BSh} | 0.65–0.80 (High) | Arid; drought; desertification |
| KoppenZone in {Cfa, Cwa, Aw} | 0.45–0.65 (Medium) | Temperate agricultural; seasonal flooding |
| KoppenZone in {Dfa, Dfb} | 0.30–0.45 (Medium-Low) | Continental industrial; heat stress |
| KoppenZone in {Dfc, Dfd} AND NOT coastal | 0.25–0.40 (Low-Medium) | Northern boreal; warming initially positive |
| KoppenZone in {Cfb, Cfc} | 0.20–0.35 (Low) | Temperate oceanic; moderate exposure |

`geographic_vulnerability` is stored as a float on Province (or RegionClimateState) at world load time. It is not recalculated at runtime.

---

## Stage 5 — Soils: What the Ground Is Made Of

Soils synthesize geology + climate + vegetation + time. They determine agricultural productivity more than almost any other factor.

```cpp
enum class SoilType : uint8_t {
    Mollisol,    // temperate grassland; continental; the black soils — Ukraine/Kansas/Pampas analog
    Oxisol,      // tropical; old surface; nutrients in biomass not soil; bauxite forms here
    Aridisol,    // desert; minimal weathering; can be productive with irrigation
    Vertisol,    // seasonally wet/dry shrink-crack clay; cotton soils of India/Sudan analog
    Spodosol,    // cool boreal; acidic; pine forest; poor for crops; good for timber
    Histosol,    // waterlogged; peat; massive carbon store; draining releases CO2
    Alluvial,    // river floodplain/delta; replenished by flooding; best land per area
    Andisol,     // volcanic ash weathering; extremely fertile; Java/Central America analog
    Cryosol,     // permafrost-affected; thin active layer; construction extremely difficult
    Entisol,     // young soil; little development; sand dunes, recent volcanic flows
};
```

**Soil type derivation rule:**

```
SoilType = classify(
    rock_type,
    KoppenZone,
    plate_age,           // older surface = more weathered
    terrain_type,
    precipitation_mm,
    is_delta,
    tectonic_context == HotSpot OR Subduction  // volcanic parent material
)
```

**Key derivations:**

| Condition | SoilType | Agricultural modifier |
|---|---|---|
| Continental, Dfb/Dfa, former grassland | Mollisol | ×1.6 — world's best grain land |
| Tropical (Af/Am), old surface | Oxisol | ×0.4 — deforestation collapses rapidly |
| BWh/BWk | Aridisol | ×0.1 base; ×0.5 with irrigation |
| Seasonal wet/dry (Aw/BSh), clay-rich | Vertisol | ×0.8 base; ×1.2 with water management |
| Dfc/Dfd, pine/spruce | Spodosol | ×0.2 crops; ×1.2 timber |
| Waterlogged, high_water_table | Histosol | ×0.6; peat resource; CO2 release if drained |
| is_delta OR alluvial_fan | Alluvial | ×1.5 — Nile/Ganges/Mekong analog |
| tectonic_context HotSpot/Subduction, recent volcanism | Andisol | ×1.4 — Java/Costa Rica analog |
| ET/EF, permafrost | Cryosol | ×0.0 agriculture; ×3.0 infrastructure cost |

**Volcanic soil note:** A volcano that erupts during play destroys immediate infrastructure but shifts affected provinces toward Andisol over time, improving long-run agricultural productivity. Short-term catastrophe, long-term benefit — a risk/reward dynamic for provinces near volcanic zones.

---

### Irrigation and Water Scarcity

The soil table marks `Aridisol` as `×0.1 base; ×0.5 with irrigation` and `Vertisol` as `×0.8 base; ×1.2 with water management`. Without a formula for what irrigation costs and what enables it, these multipliers are inert — the Bootstrapper cannot generate correct extraction logic.

**Physics basis:** Irrigation requires a water source and a delivery mechanism. The source is `river_access` (diversion canals from a flowing river), `groundwater_reserve` (pump irrigation from aquifers), or `spring_flow_index` (gravity-fed from artesian springs). The delivery mechanism scales with the area being irrigated — larger farms require more infrastructure. And the water has to go somewhere: irrigation in enclosed basins raises the water table and eventually salinises soils (Aral Sea basin, Mesopotamian salt deposits, Murray-Darling). Drainage is a separate cost.

**Province-level irrigation fields** (set at world gen; modified during play):

```cpp
// Added to Province:
float  irrigation_potential;     // 0.0–1.0; maximum achievable agricultural productivity
                                  // with full irrigation investment; derived at Stage 5
float  irrigation_cost_index;    // 0.5–5.0; cost multiplier per unit of irrigated area relative
                                  // to baseline (temperate province, river access, flat terrain = 1.0)
                                  // 0.5 = gravity-fed delta (cheaper than baseline); 5.0 = deep desert
                                  // deep aquifer on rough terrain (very expensive)
float  salinisation_risk;        // 0.0–1.0; probability of soil salinisation per decade of
                                  // intensive irrigation without drainage investment
float  water_table_depth_m;      // depth to groundwater in metres; drives pump energy cost;
                                  // derived in Stage 5 from groundwater_reserve, KoppenZone, soil_type
float  water_availability;       // 0.0–1.0; weighted composite of river_access, groundwater_reserve,
                                  // spring_flow_index; stored as Province field (not a local variable)
                                  // so apply_irrigation() can read it at simulation time without
                                  // re-deriving from potentially-depleted groundwater_reserve
```

**Derivation at Stage 5:**

```python
def compute_irrigation_fields(province):

    # --- Irrigation potential ---
    # Maximum productivity achievable if water were unlimited
    # = base soil productivity × climate temperature suitability
    # Arid soils with warm temperatures can be extremely productive with water (Nile, Indus, California)

    temp_suitability = clamp(
        lerp(0.0, 1.0, (province.effective_temperature - 5.0) / 25.0),
        0.0, 1.0
    )  # below 5°C = no growth; above 30°C = heat stress begins
    
    if province.soil_type == Aridisol:
        province.irrigation_potential = 0.75 × temp_suitability
        # Arid soils are often deep, unleached, nutrient-rich — they just lack water
        # With water, Californian Central Valley / Nile delta / Indus plains productivity levels
    elif province.soil_type == Vertisol:
        province.irrigation_potential = 0.85 × temp_suitability
    elif province.soil_type == Alluvial:
        province.irrigation_potential = 0.90  # already high; irrigation extends dry season
    elif province.soil_type in {Oxisol, Spodosol}:
        province.irrigation_potential = 0.30  # poor soils don't respond well to irrigation
    else:
        province.irrigation_potential = province.agricultural_productivity × 1.15

    # --- Water availability score ---
    # How much water is accessible from all sources
    # Stored as a Province field so apply_irrigation() can read it at simulation time
    province.water_availability = (
        province.river_access          × 0.50   // surface diversion; cheapest; limited by river flow
      + province.groundwater_reserve   × 0.35   // pump irrigation; energy cost; depletion risk
      + province.spring_flow_index     × 0.15   // gravity-fed; limited volume; very cheap
    )
    water_availability = province.water_availability   // local alias for remainder of this function

    # Where no water is available, irrigation_potential is unreachable
    if water_availability < 0.10:
        province.irrigation_potential = 0.0   // no source; no irrigation possible

    # --- Irrigation cost index ---
    # Baseline 1.0 = temperate province with river access on flat terrain
    # Higher = more expensive to deliver the same irrigated area

    terrain_cost = 1.0 + province.terrain_roughness × 1.5
    # Rough terrain requires more canal infrastructure; centre-pivot irrigation impossible on slopes

    water_lift_cost = 1.0 + (province.water_table_depth_m / 100.0) × 0.8
    # Deep aquifer requires more pump energy; 100m depth = 0.8× cost premium
    # Surface river diversion has no lift cost

    distance_cost = 1.0  // local water sources assumed reachable within province
    # Cross-province water transfer (dam + long-distance canal) would multiply this but
    # is a simulation-time infrastructure investment, not a world-gen parameter

    province.irrigation_cost_index = clamp(
        terrain_cost × water_lift_cost
        × (2.0 - water_availability),  // scarce water = higher marginal cost per unit
        0.5, 5.0
    )
    # Range: 0.5 (delta with river diversion; cheaper than baseline — gravity flow)
    #        5.0 (deep desert with deep aquifer on rough terrain — very expensive)

    # --- Salinisation risk ---
    # Irrigation in endorheic basins or arid zones with poor drainage concentrates salt
    # as water evaporates; classic problem in Mesopotamia, Pakistan, Central Asia

    base_salinisation = 0.0
    if province.KoppenZone in {BWh, BWk, BSh, BSk}:
        base_salinisation += 0.40   // arid climate: high evaporation concentrates salt
    if province.is_endorheic:
        base_salinisation += 0.30   // closed basin: salt has nowhere to go
    if province.terrain_roughness < 0.15:
        base_salinisation += 0.15   // flat terrain = poor natural drainage
    if province.groundwater_reserve > 0.70:
        base_salinisation += 0.10   // shallow water table rises under irrigation

    province.salinisation_risk = clamp(base_salinisation, 0.0, 1.0)
```

**`water_table_depth_m` derivation:**

```python
def compute_water_table_depth(province) -> float:
    """
    Returns depth to groundwater in metres (0.5–200.0).
    High groundwater_reserve → shallow table; low reserve → deep table.
    Arid climate, no recharge: table driven deeper.
    Alluvial / delta / permafrost: forced shallow.
    """
    # Baseline: inversely proportional to groundwater reserve
    # reserve = 1.0 → near-surface (~1m); reserve = 0.0 → 200m
    base_depth_m = (1.0 - province.groundwater_reserve) × 200.0

    # Arid climates have minimal recharge; table retreats further
    if province.KoppenZone in {BWh, BWk}:
        base_depth_m *= 1.8   // hot and cold desert: deep aquifers common
    elif province.KoppenZone in {BSh, BSk}:
        base_depth_m *= 1.3   // steppe: moderately deep

    # Floodplains and deltas: waterlogged; table at or near surface
    if province.soil_type == SoilType.Alluvial or province.is_delta:
        base_depth_m = min(base_depth_m, 5.0)

    # Permafrost: effective water table is active layer depth (~0.5–2.0m)
    if province.permafrost_type != PermafrostType.None:
        base_depth_m = min(base_depth_m, 2.0)

    province.water_table_depth_m = clamp(base_depth_m, 0.5, 200.0)
```

This feeds directly into `irrigation_cost_index` via `water_lift_cost` (line 1031): a 100m-deep water table adds 0.8× cost premium on top of terrain and water-scarcity factors.

**Simulation-time irrigation mechanics** (world gen seeds the potential; these trigger during play):

```python
# When a province has irrigation infrastructure built:
def apply_irrigation(province, irrigated_fraction):
    # Actual yield uplift
    current_productivity = lerp(
        province.agricultural_productivity,    // dry farming baseline
        province.irrigation_potential,         // maximum irrigated potential
        irrigated_fraction × water_availability
    )
    
    # Salinisation accumulation (per decade of intensive irrigation without drainage)
    if not province.has_drainage_infrastructure:
        province.soil_salt_accumulation += (
            province.salinisation_risk
            × irrigated_fraction
            × SALINISATION_RATE_PER_TICK
        )
    
    # When salt accumulation crosses threshold, productivity degrades
    # Mesopotamia, Aral Sea basin, Pakistan: historical precedent
    if province.soil_salt_accumulation > SALINISATION_DAMAGE_THRESHOLD:
        productivity_penalty = (province.soil_salt_accumulation - SALINISATION_DAMAGE_THRESHOLD) × 0.8
        current_productivity = max(0.05, current_productivity - productivity_penalty)
    
    return current_productivity
```

**Groundwater depletion** ties back to `groundwater_reserve` from Stage 3: pump irrigation draws from the aquifer. If extraction exceeds recharge rate, `groundwater_reserve` declines. When it falls below `AQUIFER_CRITICAL_THRESHOLD`, `water_availability` drops and `irrigation_cost_index` rises (pumping from greater depth). Ancient aquifers (Ogallala analog) recharge on geological timescales — once depleted they do not recover within game time. This is the mechanics of the American High Plains water crisis: extremely productive land, finite water, no recovery path.

**The full decision chain:** Arid province with river_access → high irrigation_potential → low irrigation_cost_index (river diversion is cheap) → high salinisation_risk if endorheic. Player who irrigates without building drainage watches productivity peak then slowly collapse. Player who builds drainage prevents collapse but at additional capital cost. This mirrors the exact trade-off faced by every ancient Mesopotamian state.

---

## Stage 6 — Biomes: What Grows There

With climate and soils established, biomes emerge deterministically using Whittaker classification (temperature × precipitation):

```
Biome = classify(temperature_avg_c, precipitation_mm, KoppenZone)
```

**Biome → field outputs:**

| Biome | KoppenZone(s) | forest_coverage base | agricultural_productivity base | timber_resource | wildfire_vulnerability |
|---|---|---|---|---|---|
| Tropical Rainforest | Af | 0.90 | 0.30 (oxisol; biomass-locked) | High | 0.10 |
| Tropical Savanna | Aw, Am | 0.30 | 0.55 | Low | 0.60 |
| Hot Desert | BWh | 0.02 | 0.02 | None | 0.05 |
| Cold Desert | BWk | 0.05 | 0.05 | None | 0.10 |
| Mediterranean Scrub | Csa, Csb | 0.35 | 0.65 | Low | 0.75 |
| Temperate Rainforest | Cfb (coastal) | 0.85 | 0.55 | Very High | 0.15 |
| Temperate Deciduous | Cfb, Cfa | 0.60 | 0.70 | High | 0.25 |
| Temperate Grassland | Dfa/Dfb (low precip) | 0.05 | 0.90 (mollisol) | None | 0.40 |
| Boreal Forest (Taiga) | Dfc, Dfd | 0.80 | 0.10 | Very High | 0.45 |
| Tundra | ET | 0.05 | 0.00 | None | 0.05 |
| Ice Cap | EF | 0.00 | 0.00 | None | 0.00 |

**Viable crop type by KoppenZone** (resolves pre-existing [BLOCKER] gap; applies to both this pipeline and GIS pipeline):

| KoppenZone | Viable crop keys |
|---|---|
| Af, Am | Rubber, CoffeeArabica, Cacao, SugarCane, Rice, Banana |
| Aw | Cotton, Tobacco, SugarCane, Soybeans, Sorghum |
| BWh, BSh | None (base); Wheat/Date viable with irrigation and river_access > 0.4 |
| BWk, BSk | Wheat (marginal), Barley, Sagebrush |
| Cfa | Wheat, Corn, Soybeans, Cotton, Tobacco, Rice |
| Cfb | Wheat, Barley, Potatoes, Hops, Apples |
| Csa, Csb | WineGrapes, Olives, Citrus, Wheat, Almonds |
| Cwa | Rice, Wheat, Tea, Cotton |
| Dfa, Dfb | Wheat, Corn, Soybeans, Sunflower, Beets |
| Dfc, Dfd | Barley, Rye, Oats, Potatoes |
| ET, EF | None |

This table should be stored in `crop_viability.csv` in the data directory, not in engine code, so modders can extend it without recompilation.

### `agricultural_productivity` Computation Chain

`province.agricultural_productivity` is the single float read by factory output calculations, worker models, and settlement attractiveness. It is assembled from three ordered passes and then clamped. **The multiplication rule is authoritative: soil multiplier scales the biome base; modifiers add to the scaled result.** This means soil type can dramatically suppress or amplify the biome potential — Oxisol tropical rainforest is fertile-looking but agriculturally poor; Mollisol temperate grassland is the reverse.

```python
def compute_agricultural_productivity(province) -> float:
    """
    Three-pass composition. Called at end of Stage 6 after soil type is set.
    Stores result in province.agricultural_productivity.
    """

    # --- Pass 1: Biome base ---
    # Read from biome table above (column: agricultural_productivity base)
    biome_base = BIOME_AG_BASE[province.biome]   // config table; not inline

    # --- Pass 2: Soil multiplier ---
    # From Stage 5 soil derivation table (column: Agricultural modifier)
    soil_multiplier = SOIL_AG_MULTIPLIER[province.soil_type]   // config table

    # Soil multiplier is applied multiplicatively to biome base.
    # Examples:
    #   Mollisol (×1.6) on Temperate Grassland (0.90 base) → 1.44 → clamped to 1.0
    #   Oxisol (×0.4) on Tropical Rainforest (0.30 base) → 0.12
    #   Andisol (×1.4) on Tropical Savanna (0.55 base) → 0.77
    #   Cryosol (×0.0) on Tundra (0.00 base) → 0.00
    scaled = biome_base × soil_multiplier

    # --- Pass 3: Terrain and hydrology modifiers (additive after scaling) ---
    modifier = 0.0

    # Alluvial fan bonus (set in Stage 3 special features)
    if province.is_alluvial_fan:
        modifier += 0.10

    # Loess bonus (set in Stage 2 aeolian erosion; wind-blown silt; extremely fertile)
    if province.arid_type == AridType.Loess:   // Loess is stored as an AridType subtype
        modifier += 0.12

    # River access fertilisation (flood-renewed floodplains)
    # Only applies where flooding is the mechanism, not for snowmelt-fed arid rivers
    if province.river_access > 0.60 and not province.snowmelt_fed:
        modifier += province.river_access × 0.08

    # Altitude ceiling: farming becomes marginal above 1500m and impossible above 4500m
    # (these are hard modifiers on the scaled value, not the additive modifier)
    altitude_factor = 1.0
    alt = province.elevation_avg_m
    if alt > 4500:
        altitude_factor = 0.0
    elif alt > 3500:
        altitude_factor = 0.10
    elif alt > 2500:
        altitude_factor = 0.40
    elif alt > 1500:
        altitude_factor = 0.75

    result = (scaled + modifier) × altitude_factor
    province.agricultural_productivity = clamp(result, 0.0, 1.0)
```

**Config tables** (`biome_ag_base` and `soil_ag_multiplier`) contain the numeric values from the biome table above and the Stage 5 soil table respectively. They are not inline constants — they live in a data file so scenario mods can adjust them without recompilation.

**Field declaration:**

```cpp
// Set at end of Stage 6:
float  agricultural_productivity;  // 0.0–1.0; final assembled value from biome × soil × modifiers
                                    // this is the authoritative read-field for all downstream systems;
                                    // do not read biome_base or soil_multiplier separately at runtime
```

---

### Fisheries Model

`fish_biomass` is set on coastal and lake provinces by Stage 4 (cold current bonus) and Stage 7 (atolls, fjords). Without an economic model attached to it, the field has no implementable meaning. This section specifies the complete fisheries model so the Bootstrapper can generate correct extraction logic.

**Physics basis:** Fish populations follow Schaefer surplus production dynamics — the stock grows logistically toward a carrying capacity set by `fish_biomass`, and surplus production above current stock is the maximum sustainable yield. Overfishing below the reproduction rate depletes the stock; zero fishing allows recovery toward carrying capacity.

**Field taxonomy:**

```cpp
enum class FishingAccessType : uint8_t {
    None,           // landlocked; no fishing
    Inshore,        // coastal shelf ≤200m depth; small boats; high yield per km²; easily overexploited
    Offshore,       // open ocean; industrial trawlers required; lower yield per km² but vast area
    Pelagic,        // open ocean surface schools (tuna, sardines, anchovies); highly migratory
    Freshwater,     // rivers and lakes; limited yield; important for inland provinces
    Upwelling,      // cold current upwelling zones; highest yield on Earth; Humboldt/Benguela analog
};

struct FisheriesProfile {
    FishingAccessType  access_type;
    float  carrying_capacity;      // max sustainable fish stock; 0.0–1.0 normalized
    float  current_stock;          // starts at carrying_capacity × 0.85 (slightly below carrying cap)
    float  max_sustainable_yield;  // MSY = 0.5 × intrinsic_growth_rate × carrying_capacity
    float  intrinsic_growth_rate;  // r; species-dependent; derived from access_type
    float  seasonal_closure;       // fraction of year fishing is impossible (ice, spawning protection)
    bool   is_migratory;           // stock moves between provinces; shared-access problem
};
```

**Derivation of fisheries fields at world generation:**

```python
def seed_fisheries(province):
    profile = FisheriesProfile()
    
    # Access type
    if province.coastal_length_km == 0 and not province.has_navigable_river:
        profile.access_type = FishingAccessType.None
        return profile
    
    if province.coastal_length_km == 0:
        profile.access_type = FishingAccessType.Freshwater
        profile.carrying_capacity = province.river_access × 0.20  // rivers are low-yield
    elif province.cold_current_adjacent:
        profile.access_type = FishingAccessType.Upwelling
        profile.carrying_capacity = 0.70 + random_variance(0.15)  // highest yields
    elif province.is_atoll or province.port_capacity > 0.7:
        profile.access_type = FishingAccessType.Inshore
        profile.carrying_capacity = 0.45 + random_variance(0.20)
    elif province.coastal_length_km > 200:
        profile.access_type = FishingAccessType.Offshore
        profile.carrying_capacity = 0.30 + random_variance(0.15)
    
    # Intrinsic growth rate by access type (per simulated year)
    growth_rates = {
        Upwelling:   0.60,   // anchovies, sardines; fast reproduction
        Inshore:     0.40,   // mixed demersal; moderate
        Offshore:    0.25,   // cod, haddock; slow; prone to collapse
        Pelagic:     0.55,   // tuna; moderate-fast
        Freshwater:  0.35,
    }
    profile.intrinsic_growth_rate = growth_rates[profile.access_type]
    
    # Maximum Sustainable Yield: Schaefer model
    # MSY occurs at stock = carrying_capacity / 2
    profile.max_sustainable_yield = (
        0.5 × profile.intrinsic_growth_rate × profile.carrying_capacity
    )
    
    # Seasonal closure: ice coverage and latitude
    if KoppenZone in {ET, EF} or province.elevation_avg_m > 3000:
        profile.seasonal_closure = 0.50   // 6 months ice/inaccessible
    elif KoppenZone in {Dfc, Dfd}:
        profile.seasonal_closure = 0.25
    else:
        profile.seasonal_closure = 0.05   // minimal; spawning season protection only
    
    profile.current_stock = profile.carrying_capacity × 0.85
    profile.is_migratory = (profile.access_type in {Pelagic, Offshore})
    
    return profile
```

**Simulation-time depletion:** Each simulated year, extraction reduces `current_stock`. The Schaefer growth function replenishes it:

```python
def annual_fisheries_tick(province):
    fp = province.fisheries_profile
    
    # Harvest by all fishing operations in province (player + NPC)
    harvest = sum(op.annual_catch for op in province.fishing_operations)
    
    # Schaefer surplus production
    growth = fp.intrinsic_growth_rate × fp.current_stock × (
        1.0 - fp.current_stock / fp.carrying_capacity
    )
    
    fp.current_stock = clamp(fp.current_stock + growth - harvest, 0.0, fp.carrying_capacity)
    
    # Collapse threshold: if stock falls below 10% of carrying capacity,
    # growth becomes too slow to sustain any commercial harvest
    if fp.current_stock < fp.carrying_capacity × 0.10:
        province.fisheries_collapsed = True
        # Recovery takes decades at zero harvest; matches Grand Banks cod collapse (1992–present)
```

**The collapse mechanic is intentional design.** Offshore and inshore fisheries are economically significant and easy to overexploit. A player or NPC who subsidises fishing fleets without quota management will deplete the stock. Recovery is very slow. This is not a gotcha — the collapse trajectory is visible in `current_stock` long before it happens.

These are the details that separate a plausible world from a memorable one. Each is physically grounded and flagged as boolean or enum fields on Province.

### Atolls and Coral Reefs
**Condition:** Former HotSpot volcanic island that has since subsided below sea level; coral grew upward as the island sank. Province has near-zero land area, surrounded by ocean.
**Fields:** `is_atoll = true`; `port_capacity` moderate (lagoon harbor); `fish_biomass` elevated; `agricultural_productivity = 0.0`; `infrastructure_rating` very low.

### Fjords
**Condition:** Glaciated province at high latitude where glacial valley intersects coast.
**Fields:** `port_capacity` very high (deep natural harbor); adjacent inland provinces have very low `infrastructure_rating` (cliff walls); `coastal_length_km` very high.

```cpp
// Added to Province at Stage 7 (Fjord):
bool   has_fjord;   // true if province meets glaciated valley coast condition;
                    // used by Stage 7 Ria detection to exclude glaciated coastlines
```

### Permafrost
**Condition:** `KoppenZone` in {ET, EF, Dfc, Dfd} AND `elevation_avg_m > threshold(latitude)`.

```cpp
enum class PermafrostType : uint8_t {
    None,
    Discontinuous,   // some areas frozen; construction difficult; ground survey required
    Continuous,      // everything frozen; infrastructure cost ×3.0; permafrost thaw events possible
};
```
Permafrost thaw under climate stress: releases methane (CO2 index contribution), destabilizes built infrastructure (damage events), exposes previously inaccessible mineral deposits (resource unlock). Continuous permafrost → new Arctic oil/gas becomes accessible in later eras as climate stress accumulates.

### Inland Seas and Salt Flats
**Condition:** `is_endorheic = true` AND `KoppenZone` in arid family.
**Fields:** `arid_type = SaltFlat`; lithium brine deposit seeded in Stage 8; potash deposit possible; fishing resource if water body persists. Lithium Triangle (Bolivia/Chile/Argentina analog) emerges automatically from endorheic basins in arid highland zones.

### Karst Landscapes
**Condition:** `has_karst = true` (set in Stage 2).
**Fields:** `groundwater_reserve` elevated but unpredictable; `flood_vulnerability` low on surface (water drains underground rapidly); sinkhole hazard flag for infrastructure; cave network present → facility hide cost reduced (EX: smuggling route).

### Barrier Islands and Lagoons
**Condition:** Low-elevation coastal province with ocean on one side and shallow lagoon separating it from mainland.
**Fields:** `elevation_avg_m` < 5m; `flood_vulnerability` very high (hurricane/storm surge); `infrastructure_rating` very low; `port_capacity` elevated for lagoon side (sheltered water). US East Coast outer banks / Venice analog.

### Volcanic Highland / Fertile Slopes
**Condition:** `tectonic_context` in {Subduction, HotSpot} AND `soil_type == Andisol`.
**Fields:** `agricultural_productivity` elevated; `wildfire_vulnerability` low (moist climate common on volcano flanks); hazard events: eruption possible during play (rare; catastrophic-then-fertile arc).

### Badlands
**Condition:** `rock_type == Sedimentary` (soft: mudstone, shale, siltstone) AND `KoppenZone` in arid/semi-arid family (BWh, BWk, BSh, BSk) AND erosion rate high (derived from `terrain_roughness` gradient during Stage 2 chemical + fluvial erosion passes).

**Physics:** Soft sedimentary rock exposed to periodic intense rainfall — the pattern of arid climates — erodes into spectacular ravine networks: hoodoos, gullies, badland topography. The same rock that would be ploughed flat under a temperate climate becomes impassable sculpture under the alternating desiccation and flash flood regime of dryland climates. Classic examples: Badlands National Park (South Dakota), Bardenas Reales (Spain), Cappadocia (Turkey), Bisti Wilderness (New Mexico).

**Detection:**
```python
def classify_badlands(province):
    return (
        province.rock_type == RockType.Sedimentary
        and province.geology_type in {SedimentarySequence, CarbonateSequence}
        // soft and fine sedimentary rock (mudstone, shale, siltstone, limestone);
        // SedimentarySequence and CarbonateSequence are the GeologyType enum values
        // that correspond to the erodible rock types that produce badlands topography
        and province.KoppenZone in {BWh, BWk, BSh, BSk}
        and province.terrain_roughness > 0.55   // moderate roughness from erosion, not orogeny
        and province.elevation_avg_m < 2000      // not mountain; erosion-carved upland
    )
```

**Fields on Province:**
```cpp
// Added to Province at Stage 7 (Badlands):
// terrain_type = TerrainType::Badlands
// arable_land_fraction = 0.0 (nothing grows; topsoil is absent)
// terrain_roughness elevated: 0.55–0.75 (complex but not mountainous)
// has_canyon_system = true (deep gully networks; transit chokepoints)
bool   archaeological_interest;    // true for all Badlands provinces; exposed strata reveal
                                   // fossils and ancient deposits; tourism and research value
float  facility_concealment_bonus; // 0.0–1.0 additive bonus to facility concealment checks;
                                   // Badlands base = 0.30 (rough terrain + no settlement = low surveillance)
                                   // used by security/enforcement systems in TDD
```

**Game implications:**
- Zero agricultural value; almost no settlement attractiveness from land itself
- `transit_terrain_multiplier` very high (road construction through badlands requires constant bridging of gullies)
- Fossil deposits (palaeontological resource): badlands expose geological strata; notable sites generate `NamedFeature` of type `FossilBed` with tourism and research value
- Defensive value: historically used as hideout terrain by irregular forces; `facility_concealment_bonus` elevated (rough terrain + no settlement = low surveillance)
- Archetype: always `marginal_periphery` or `resource_frontier` (if fossil/mineral deposits notable)

---

### Estuary
**Condition:** Major river (catchment area > LARGE_RIVER_THRESHOLD) meets coast AND `is_delta = false` AND tidal range > LOW_TIDE_THRESHOLD (derived from ocean body size and coast exposure).

**Physics:** An estuary is a tidal mixing zone — the transitional water body where fresh river water and saline ocean water mix. Unlike a delta (where the river dumps sediment and builds land outward), an estuary occupies a drowned river mouth or funnel-shaped coastal inlet. The mixing zone creates a uniquely productive ecology: nutrient-rich, temperature-buffered, sheltered water. The Thames Estuary, Chesapeake Bay, San Francisco Bay, Gironde, and Rio de la Plata are estuaries, not deltas.

**Detection:**
```python
def classify_estuary(province):
    has_major_river = any(
        r.catchment_area_km2 > LARGE_RIVER_THRESHOLD
        for r in province.drainage_basin_connections
    )
    return (
        has_major_river
        and province.coastal_length_km > 30
        and not province.is_delta
        and province.tidal_exposure > TIDAL_MIXING_THRESHOLD
    )
```

**Fields:**
```cpp
// terrain_type = TerrainType::Estuary
// fish_biomass: elevated (tidal mixing + nutrient influx; estuary fisheries among most productive)
// fisheries_profile.access_type = FishingAccessType::Inshore (sheltered; accessible with small boats)
// port_capacity: moderate-to-high (sheltered tidal water; accessible even at low tide for flat-bottomed craft)
// flood_vulnerability: elevated but seasonal (storm surge + river flood can combine)
// agricultural_productivity: low-moderate (waterlogged; salt intrusion at margins)
// has_artesian_spring: false (water table at or near surface; no spring pressure needed)
```

**Distinguishing from delta:** A delta builds land outward; has very shallow water; channels are unstable and shift; `port_capacity` is moderate (requires constant dredging). An estuary has stable deep water; no significant sediment deposition; `port_capacity` is higher (deeper, reliable channels). This distinction matters for port economics: London, New York, and Buenos Aires are estuary ports; the Nile Delta has consistently poor port conditions for deep-draft vessels.

---

### Ria Coasts
**Condition:** Former river valley mouth flooded by sea level rise OR tectonic subsidence, without glaciation. Province is coastal; former valley axis visible in bathymetry (detected as elongated deep-water intrusion perpendicular to coast); no `has_fjord` flag (fjords are glaciated; rias are not).

**Physics:** Where a river valley intersects a rising sea level, the valley floods from the mouth inward. The result is a complex coastline of deep sheltered inlets — natural harbours that require no engineering, no dredging, and offer protection from ocean swell by the surrounding hills. Ria coasts are among the most harbour-rich coastlines in the world. The Atlantic coasts of Galicia (Rías Baixas), Cork Harbour, Sydney Harbour, the Chesapeake Bay system (at smaller scale), and the Dalmatian Coast are all ria or quasi-ria formations.

Rias form wherever: (a) a river drains perpendicular to the coast, (b) the rock is hard enough to preserve the valley walls as the sea rises (soft rock deltas instead), and (c) there was no glacier — glaciated valleys flood as fjords, not rias.

**Detection:**
```python
def classify_ria_coast(province):
    return (
        province.coastal_length_km > 80           // complex, indented coastline
        and not province.has_fjord                 // not glacially carved
        and not province.is_delta                  // not sediment-filled
        and province.rock_type in {Igneous, Metamorphic, HardSedimentary}  // valley walls preserved
        and province.elevation_avg_m > 50          // hills surround inlets
        and province.former_river_valley           // set in Stage 2 drainage analysis
        and province.latitude_abs < 55             // poleward of 55° → likely fjord not ria
    )
```

**Fields:**
```cpp
// terrain_type = TerrainType::RiaCoast
// port_capacity: high (deep, sheltered; multiple natural harbour sites within province)
// port_count_potential: int — number of distinct inlet harbours; 2–8 per ria province
// coastal_length_km: very high (indented coastline inflates effective coastal length)
// fish_biomass: elevated (sheltered water + mixing)
// infrastructure_rating boost: ria coasts attract early settlement; port_capacity drives infra
```

**Port capacity note:** A single ria province may contain multiple distinct natural harbour sites — each inlet is a potential anchorage. `port_count_potential` records how many. This field is informational; actual port infrastructure is built by NPCs or the player during play. But a ria coast starts with an unusually high ceiling on port development, which is why these coasts produced so many historic maritime civilisations (Phoenician coast, Atlantic Iberia, Dalmatia, Atlantic Ireland).

**Distinguishing from fjord:**

| Feature | Formation | Depth profile | Latitude | Coastline character |
|---|---|---|---|---|
| Fjord | Glacial carving | Very deep, sheer walls | 55°+ | Long, straight, steep-sided |
| Ria | River valley flooding | Moderate depth, rounded hills | 20–55° | Wide, branching, village-friendly |
| Delta | Sediment deposition | Very shallow, shifting | Any | Flat, unstable, no natural harbours |
| Estuary | Tidal mixing zone | Moderate depth, open | Any | Funnel or channel shaped |

### Island Isolation
**Condition:** Province has zero land-border neighbors (all neighbors are ocean).
**Fields:** `island_isolation = true`; `adjacent_province_ids` contains only maritime links; `infrastructure_rating` starts low (no road connections to neighbors); `port_capacity` forced ≥ 0.5; labor market operates with immigration penalty.

```cpp
// Added to GeographyProfile:
bool          island_isolation;   // true if zero land-border neighbors; maritime links only
PermafrostType permafrost_type;   // None | Discontinuous | Continuous
bool          is_atoll;
```

### Impact Craters

Every planetary body in the simulation receives an impact cratering pass. The difference between bodies is not whether craters exist — it is how well they are preserved.

**Earth-analog behavior:** Earth has ~190 confirmed impact structures. Most are invisible to the naked eye because plate tectonics subducts old crust, erosion rounds crater rims, vegetation covers the geology, and sedimentary infill buries the bowl. What survives is geologically informative but rarely dominant in the landscape. The Chicxulub structure (66 Ma) is barely a gravitational anomaly. Vredefort (2 Ga) is a circular rock pattern in South Africa. Only young, large, or geologically stable-region craters are visible features.

**Preservation mechanics:**

```
crater_preservation_score =
    (planet.planet_age_gyr × 0.0 if body_type == Terrestrial)   // starts fresh each calc
    + tectonic_erasure_rate × plate_age   // old crust is recycled; craters on it are gone
    + erosion_erasure_rate(precipitation_mm, terrain_roughness)
    - sedimentation_burial(soil_type, river_access)
    + (1.0 if tectonic_context == CratonInterior)   // stable shield → preserved longer

// Erasure rates by body type:
erosion_erasure_rate:
    Earth-analog:     HIGH  (water + tectonics; half-life ~50 Ma for most craters)
    Mars-analog:      LOW   (dry; no tectonics; craters persist billions of years)
    Moon-analog:      NONE  (airless; no erosion; record extends to formation)
    Super-Earth:      VERY HIGH (stronger erosion at 2g; craters erase faster)
```

**What survives on Earth-analog bodies:**

| Crater age | Diameter | Likely condition |
|---|---|---|
| < 50 Ma | Any | Visible rim; possible lake; mineral alteration visible |
| 50–300 Ma | > 20 km | Circular valley; anomalous drainage pattern; geological marker |
| 300 Ma – 1 Ga | > 50 km | Circular rock formation; gravity anomaly; no surface expression |
| > 1 Ga | > 100 km | Craton only; faint circular structure in ancient shield rock |

**What survives on airless / geologically dead bodies:**

Every crater from the last 4 billion years survives. The surface is a palimpsest — small craters inside large craters inside ancient basins. Terrain roughness is entirely crater-dominated. Province terrain is classified by crater density rather than erosional landforms.

**Gameplay outputs per province:**

```cpp
struct ImpactCraterRecord {
    float  diameter_km;
    float  age_ma;               // millions of years
    float  preservation_score;   // 0.0 erased → 1.0 pristine
    bool   is_visible_surface_feature;  // above 0.4 preservation and > 1 km diameter
    bool   has_crater_lake;      // preserved rim + humid climate → lake forms
    float  mineral_alteration;   // 0.0–1.0; hydrothermal alteration from impact → resource signal
                                 // impact sites concentrate platinum group metals, shocked quartz
                                 // Sudbury (Ontario), Bushveld (South Africa) analogs
};

// On Province GeographyProfile:
std::vector<ImpactCraterRecord> impact_craters;  // most provinces: empty; rare: 1–3 entries
bool  has_impact_basin;          // large ancient crater (> 100 km); terrain organized around rim
bool  has_crater_lake;           // crater preserved + humid → lake; tourism/freshwater resource
float impact_mineral_signal;     // 0.0–1.0; boosts probability of PGM, nickel, shocked-quartz deposits
```

**Resource seeding interaction (Stage 8):** Provinces with `mineral_alteration > 0.5` get elevated probability of platinum group metals (PGMs), shocked quartz (industrial), and nickel sulfide deposits in Stage 8. Impact hydrothermal systems concentrate metals that would otherwise be diffuse — this is the mechanism behind Sudbury (the world's largest nickel deposit inside an impact crater) and Bushveld (platinum group metals, also impact-related). The game does not label these as "impact resources" — the player discovers the correlation.

**Loading screen fact (Stage 10):** Visible craters and crater lakes are named in Stage 10 and appear in loading commentary: *"The Karath Basin, 140 km across, is the remnant of an asteroid strike 380 million years ago. The circular lake at its heart has no outflow — it drains entirely underground through limestone fractures."*

---

## Stage 8 — Resource Deposit Seeding

With tectonic context, rock type, soil, and planetary age all established, resources are seeded with geological coherence. Seeding has three passes:

1. **Tectonic probability** — which resources can exist here at all, based on `TectonicContext`
2. **Radiogenic age modifiers** — planet age depletes unstable isotopes and accumulates their decay products
3. **Quantity and quality distribution** — lognormal draw scaled by all modifiers

### 8.1 — Resource Taxonomy

Resources fall into three categories by their relationship to planetary age.

```cpp
enum class AgeRelationship : uint8_t {
    Decays,       // quantity decreases with planet age; source isotopes are consumed
    Accumulates,  // quantity increases with planet age; produced by decay of parent isotopes
    Invariant,    // unaffected by radioactive decay; quantity set by tectonic context only
};

struct ResourceType {
    std::string      id;
    AgeRelationship  age_relationship;
    float            half_life_gyr;    // only meaningful for Decays; 0 for others
    std::string      decay_parent;     // for Accumulates: which Decays resource produces this
};
```

**Decays resources** — depleted by decay:

| Resource | Half-life (Gyr) | Primary decay product | Notes |
|---|---|---|---|
| Uranium | 4.47 (U-238); 0.704 (U-235) | Lead-206, Lead-207 | U-235 almost fully gone by 3 Gyr; U-238 dominant on Earth-analog |
| Thorium | 14.05 (Th-232) | Lead-208 | Depletes slowly; still substantial on very old planets |
| Potassium-40 | 1.25 | Argon-40 (89%), Calcium-40 (11%) | Affects fertiliser feedstock on ancient worlds |

**Accumulates resources** — built up by decay:

| Resource | Produced from | Rate | Notes |
|---|---|---|---|
| Lead | U-238 → Pb-206; U-235 → Pb-207; Th-232 → Pb-208 | Mirrors uranium/thorium depletion | Radiogenic lead; same resource as geological lead; indistinguishable to player |
| Radiogenic Helium | Every alpha decay step in U/Th chains | Accumulates in porous rock; migrates to gas traps | Appears as elevated He fraction in NaturalGas deposits; affects gas quality field |

**Invariant resources** — age has no effect:

IronOre, Copper, Bauxite, Lithium, Gold, RareEarths, Diamonds, Nickel, Coal, CrudeOil, NaturalGas (quantity), Potash, Phosphate, Sulfur, Geothermal, SolarPotential, WindPotential, PlatinumGroupMetals, Sand, Aggregate, Peat.

---

### Quality Modifier Fields on Deposits

Some resources are not separate deposits but co-located quality attributes on a host deposit. These are stored as fields on the `ResourceDeposit` struct rather than as independent entries in `province.resource_deposits[]`.

**Cobalt (`cobalt_fraction` on Nickel and Copper deposits):**

Cobalt is geochemically bound with nickel in ultramafic magmatic deposits (pentlandite ore) and with copper in sediment-hosted copper deposits (DRC-type). It is never a standalone primary resource — it is always a by-product or co-product of nickel or copper mining. Its economic significance scales from negligible (Era 1–2) to critical (Era 3+ battery cathode manufacturing).

```python
def seed_cobalt_fraction(deposit):
    if deposit.resource == "Nickel":
        if deposit.tectonic_context in {CratonInterior, HotSpot}:
            # Magmatic nickel-sulfide deposits (Sudbury, Norilsk analogs)
            # Cobalt/nickel ratio ~0.03–0.08 in pentlandite ores
            deposit.cobalt_fraction = beta(alpha=2, beta=10) × 0.10
        else:
            deposit.cobalt_fraction = 0.01   // minor; laterite nickel has little cobalt
    
    elif deposit.resource == "Copper":
        if deposit.tectonic_context in {CratonInterior, SedimentaryBasin}:
            # Sediment-hosted copper (Central African Copperbelt analog; DRC/Zambia)
            # Some of the world's richest cobalt deposits are here
            deposit.cobalt_fraction = beta(alpha=3, beta=8) × 0.25
        elif deposit.tectonic_context == Subduction:
            # Porphyry copper; cobalt trace only
            deposit.cobalt_fraction = beta(alpha=1, beta=15) × 0.03
        else:
            deposit.cobalt_fraction = 0.005
    
    # cobalt_fraction is the mass ratio of extractable cobalt to host metal
    # Era lock: cobalt is accessible from Era 1 alongside host metal
    # Economic significance multiplier activates at Era 3 (battery manufacturing tech)
    deposit.cobalt_era_economic_multiplier = {1: 0.05, 2: 0.15, 3: 1.00, 4: 1.20, 5: 0.90}
```

**Peat** (on Histosol provinces):

Peat is not a mineral deposit — it is an accumulated organic sediment in waterlogged provinces. It is seeded wherever `soil_type == Histosol` with quantity derived from `groundwater_reserve` and `plate_age` (older waterlogging = deeper peat). It is a slow-renewable fuel: extraction is effectively permanent on game timescales (peat accumulates ~1mm/year; significant deposits are thousands of years old). As a fuel resource it bridges Era 1 in provinces without coal access, at the cost of destroying the soil if the peat layer is removed.

```cpp
// Added to ResourceDeposit when resource == "Peat":
float  peat_depth_m;           // 0.5–10m; deeper = more fuel; also more CO2 if drained
bool   is_carbon_sink;         // true while waterlogged; false if drained for extraction
float  co2_release_on_drain;   // tonne CO2 / km² if peat is drained and exposed to air
```

---

### 8.2 — Radiogenic Age Modifier Formulas

Age modifiers are computed from `PlanetaryParameters.planet_age_gyr` before any deposit is seeded. The same formulas drive both resource quantities and the Stage 1 mantle heat flux derivation — consistent physics throughout.

**Uranium remaining fraction:**

```
// U-238 and U-235 are blended into a single Uranium resource.
// At formation, U-235 / U-238 ratio was ~0.31. Today on Earth (4.6 Gyr) it is ~0.007.
// Use blended effective half-life weighted by initial abundance:

u238_remaining(age) = 0.5 ^ (age / 4.47)
u235_remaining(age) = 0.5 ^ (age / 0.704)

uranium_age_modifier(age) =
    (0.993 × u238_remaining(age) + 0.007 × u235_remaining(age))
    / (0.993 × u238_remaining(0) + 0.007 × u235_remaining(0))

// Simplifies to: blended fraction remaining relative to formation abundance
// Earth analog (4.6 Gyr): ≈ 0.48  →  roughly half original uranium remains
// Old planet  (8.0 Gyr):  ≈ 0.24  →  one quarter remains
// Ancient     (12 Gyr):   ≈ 0.10  →  one tenth remains; trace deposits only
```

**Thorium remaining fraction:**

```
thorium_age_modifier(age) = 0.5 ^ (age / 14.05)

// Earth analog (4.6 Gyr): ≈ 0.80  →  most thorium intact; slower decay
// Old planet  (8.0 Gyr):  ≈ 0.64
// Ancient     (12 Gyr):   ≈ 0.55  →  still over half present; barely depleted
```

**Lead accumulation fraction:**

```
// Lead accumulates as uranium and thorium decay.
// Radiogenic lead fraction = 1 − remaining parent isotopes (simplified; ignores intermediate daughters)
// True lead deposit quantity = geological_lead_base + radiogenic_accumulation

radiogenic_pb206(age) = 1.0 − u238_remaining(age)   // from U-238
radiogenic_pb207(age) = 1.0 − u235_remaining(age)   // from U-235; nearly complete by 3 Gyr
radiogenic_pb208(age) = 1.0 − thorium_age_modifier(age)  // from Th-232

lead_age_modifier(age) =
    geological_lead_base                                    // primordial; invariant; tectonic-set
    + RADIOGENIC_LEAD_SCALE × (
        0.993 × radiogenic_pb206(age)
      + 0.007 × radiogenic_pb207(age)
      + 0.30  × radiogenic_pb208(age)   // thorium contribution; weighted by relative abundance
    )

// Earth analog (4.6 Gyr): moderate accumulation; lead deposits common
// Old planet  (8.0 Gyr):  high accumulation; lead abundant; uranium scarce
// Ancient     (12 Gyr):   very high; lead deposits nearly everywhere uranium was present
// Young planet (1.0 Gyr):  low; only geological (non-radiogenic) lead; uranium abundant
```

**Radiogenic helium (NaturalGas quality modifier):**

```
// Each alpha decay step in the U and Th chains emits one He-4 nucleus.
// U-238 chain: 8 alpha decays total. U-235 chain: 7. Th-232 chain: 6.
// Helium accumulates in porous rock; migrates upward; concentrates in gas traps.

he4_accumulation(age) =
    8.0 × (1 − u238_remaining(age))   // U-238 chain alphas
  + 7.0 × (1 − u235_remaining(age))   // U-235 chain alphas (mostly complete by 3 Gyr)
  + 6.0 × (1 − thorium_age_modifier(age))  // Th-232 chain alphas

// Stored as NaturalGas.helium_fraction: 0.0–1.0
// Earth analog: natural gas fields typically 0.01–0.05% He; some fields 7%+
// He fraction affects gas market value (helium is a separate commodity; coolant, lifting gas)
// High he4_accumulation in province → elevated helium_fraction in any NaturalGas deposit
```

**Potassium-40 depletion:**

```
k40_age_modifier(age) = 0.5 ^ (age / 1.25)

// K-40 is ~0.012% of natural potassium; the rest is stable K-39 and K-41.
// Potash (KCl) total quantity is invariant — the stable isotopes are unaffected.
// K-40 depletion only matters if the player has tech to use K-40 as a fuel/tracer.
// Era 4+ mechanic; not modelled in Era 1–3. Field stored for forward compatibility.
// province.potash_k40_fraction = 0.012% × k40_age_modifier(age)
```

---

### 8.3 — Resource Probability Table

The tectonic probability table sets the baseline. Age modifiers are applied multiplicatively after the probability draw, scaling the deposited quantity. Lead and Geothermal receive explicit age-driven entries.

*Weight values: 0 = impossible; 1 = low; 2 = medium; 3 = high; 4 = very high. All weights are pre-age-modifier baseline.*

| Resource | Subduction | ContCollision | RiftZone | TransformFault | HotSpot | PassiveMargin | CratonInterior | SedimBasin | Age relationship |
|---|---|---|---|---|---|---|---|---|---|
| IronOre | 1 | 1 | 1 | 0 | 0 | 0 | 4 | 2 | Invariant |
| Copper | 4 | 1 | 1 | 0 | 2 | 0 | 1 | 0 | Invariant; `cobalt_fraction` field on deposit |
| Bauxite | 0 | 0 | 0 | 0 | 1 | 2 | 2 | 0 | Invariant |
| Lithium | 1 | 0 | 4 | 0 | 2 | 0 | 0 | 1 | Invariant |
| Gold | 3 | 2 | 1 | 0 | 1 | 0 | 4 | 0 | Invariant |
| Uranium | 0 | 0 | 2 | 0 | 0 | 0 | 3 | 1 | Decays (×`uranium_age_modifier`) |
| Thorium | 0 | 0 | 1 | 0 | 0 | 0 | 3 | 0 | Decays (×`thorium_age_modifier`) |
| Lead | 1 | 1 | 1 | 0 | 0 | 1 | 2 | 1 | Accumulates (×`lead_age_modifier`) |
| RareEarths | 0 | 0 | 3 | 0 | 1 | 0 | 2 | 0 | Invariant |
| Diamonds | 0 | 0 | 1 | 0 | 0 | 0 | 4 | 0 | Invariant |
| Nickel | 1 | 0 | 1 | 0 | 2 | 0 | 3 | 0 | Invariant; `cobalt_fraction` field on deposit |
| PlatinumGroupMetals | 0 | 0 | 0 | 0 | 1 | 0 | 2 | 0 | Invariant; boosted by impact `mineral_alteration` |
| Coal | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 4 | Invariant |
| CrudeOil | 0 | 0 | 1 | 0 | 0 | 3 | 0 | 4 | Invariant |
| NaturalGas | 0 | 0 | 1 | 0 | 1 | 2 | 0 | 4 | Invariant (quantity); He fraction accumulates |
| Potash | 0 | 0 | 1 | 0 | 0 | 1 | 0 | 3 | Invariant (quantity); K-40 fraction decays |
| Phosphate | 0 | 0 | 0 | 0 | 0 | 3 | 1 | 2 | Invariant |
| Sulfur | 3 | 0 | 2 | 0 | 4 | 0 | 0 | 0 | Invariant |
| Geothermal | 3 | 0 | 3 | 1 | 4 | 0 | 0 | 0 | Decays (×`geothermal_age_modifier`) |
| Sand | 0 | 0 | 1 | 1 | 0 | 4 | 1 | 2 | Invariant; see derivation note below |
| Aggregate | 1 | 2 | 1 | 1 | 1 | 1 | 3 | 2 | Invariant; see derivation note below |
| Peat | 0 | 0 | 0 | 0 | 0 | 1 | 1 | 2 | Invariant; seeded only where `soil_type == Histosol` |
| SolarPotential | per KoppenZone | — | — | — | — | — | — | — | Invariant |
| WindPotential | per terrain/coast | — | — | — | — | — | — | — | Invariant |

**Sand and Aggregate derivation:**

Sand and aggregate (crushed stone, gravel) are the highest-volume extracted materials on Earth — by mass, more is quarried annually than all metals combined. Every construction project requires them. Their absence from the resource table means construction economics lack a local supply constraint that is historically and economically real.

Sand and aggregate are not seeded probabilistically from tectonic context alone — they are derived deterministically from geology, which is already known at Stage 8:

```python
def seed_sand_aggregate(province):
    # Sand: marine and river-deposited; abundant on coasts and floodplains
    sand_quantity = 0.0
    if province.coastal_length_km > 50:
        sand_quantity += province.coastal_length_km × COASTAL_SAND_SCALE
    if province.soil_type in {Alluvial, Entisol}:
        sand_quantity += province.river_access × RIVER_SAND_SCALE
    if province.arid_type == AridType.Erg:
        sand_quantity += 0.90   // abundant; but desert sand is too fine-grained for concrete
        province.sand_usable_fraction = 0.05  // erg sand is rounded; poor for construction
    else:
        province.sand_usable_fraction = 0.85  // river and marine sand: angular; suitable

    # Aggregate: quarry rock from hard geology
    aggregate_quantity = 0.0
    if province.rock_type in {Igneous, Metamorphic}:
        aggregate_quantity += 0.80   // granite, basalt, gneiss: excellent aggregate
    elif province.rock_type == Sedimentary:
        if province.geology_type in {Limestone, Dolomite}:
            aggregate_quantity += 0.70   // limestone: common road base; also cement feedstock
        else:
            aggregate_quantity += 0.40   // softer sedimentary; lower quality
    
    province.resource_deposits.append(ResourceDeposit("Sand", sand_quantity, era_available=1))
    province.resource_deposits.append(ResourceDeposit("Aggregate", aggregate_quantity, era_available=1))
```

**Construction economics:** A province with low local Sand and Aggregate must import them, adding transport cost to every construction project. This matters most for large infrastructure projects (roads, dams, ports) and in island or desert provinces. The Maldives imports nearly all construction aggregate. Dubai imports sand for concrete despite being surrounded by desert, because erg sand is unsuitable. These constraints are now represented.

**SolarPotential and WindPotential seeding:**

These are not probabilistic deposits — they are deterministic capacity indices derived from the province's physical properties. They appear in `resource_deposits[]` for consistency with the extraction mechanics framework but are flagged `is_renewable = true` and do not deplete.

```python
def seed_solar_potential(province) -> ResourceDeposit | None:
    """
    Solar potential is primarily a function of latitude (solar angle) and
    cloud cover (precipitation / KoppenZone). Desert regions at 20–30° latitude
    are the global optimum: high insolation + low cloud cover.
    Returns None for polar and permanently-overcast provinces (no viable solar).
    """
    abs_lat = abs(province.latitude)

    # Base insolation index from latitude (cosine of angle of incidence)
    # Equator = 1.0; poles approach 0 due to oblique angle and day length
    if abs_lat < 15:
        lat_factor = 0.90   # equatorial: high sun angle but heavy cloud in Af/Am
    elif abs_lat < 35:
        lat_factor = 1.00   # subtropical desert belt: global optimum; clear sky + high sun
    elif abs_lat < 55:
        lat_factor = 0.70   # temperate: lower sun angle; seasonal
    elif abs_lat < 70:
        lat_factor = 0.40   # subarctic: low angle; long winter darkness
    else:
        lat_factor = 0.10   # polar: midnight sun in summer but zero in winter; low average

    # Cloud cover penalty from KoppenZone
    cloud_penalty = {
        KoppenZone.Af:  0.35,   # perpetually overcast; dense cloud
        KoppenZone.Am:  0.40,
        KoppenZone.Aw:  0.15,
        KoppenZone.BWh: 0.05,   # hot desert: clear sky almost always
        KoppenZone.BWk: 0.08,
        KoppenZone.BSh: 0.10,
        KoppenZone.BSk: 0.15,
        KoppenZone.Cfa: 0.25,
        KoppenZone.Cfb: 0.35,   # oceanic: persistently cloudy
        KoppenZone.Cfc: 0.45,
        KoppenZone.Csa: 0.10,   # Mediterranean: dry summer is best solar season
        KoppenZone.Csb: 0.12,
        KoppenZone.Cwa: 0.20,
        KoppenZone.Dfa: 0.20,
        KoppenZone.Dfb: 0.25,
        KoppenZone.Dfc: 0.35,
        KoppenZone.Dfd: 0.40,
        KoppenZone.ET:  0.30,   # tundra: clear but low angle; see lat_factor
        KoppenZone.EF:  0.25,   # ice cap: clear but polar angle; lat_factor dominates
    }.get(province.KoppenZone, 0.25)

    # Scale by planetary solar constant relative to Earth
    solar_scale = province.planet.solar_constant_wm2 / 1361.0

    potential = clamp((lat_factor - cloud_penalty) × solar_scale, 0.0, 1.0)

    if potential < 0.05:
        return None   # no viable solar resource (polar or perpetually overcast)

    return ResourceDeposit(
        resource      = "SolarPotential",
        quantity      = potential,   # 0.05–1.0; higher = more capacity factor
        quality       = potential,   # quality == quantity for renewables (no ore grade concept)
        depth         = 0.0,         # surface resource
        accessibility = 1.0,         # no extraction difficulty; land use only
        era_available = 2,           # utility-scale solar is Era 2 (post-1950s equivalent)
        is_renewable  = True,
    )


def seed_wind_potential(province) -> ResourceDeposit | None:
    """
    Wind potential is a function of terrain exposure and latitude-driven
    atmospheric circulation. Coastal and plateau sites in the westerly and
    trade wind belts are highest. Interior continental areas in blocking
    high-pressure zones are lowest.
    Returns None for wind-sheltered or very low wind provinces.
    """
    abs_lat = abs(province.latitude)

    # Base wind index from latitude band (atmospheric cell velocity)
    if abs_lat < 10:
        lat_wind = 0.25   # ITCZ: calm convergence zone; variable winds; low
    elif abs_lat < 30:
        lat_wind = 0.55   # trade winds: consistent direction; moderate speed
    elif abs_lat < 60:
        lat_wind = 0.80   # westerlies: strongest persistent surface winds on Earth
    else:
        lat_wind = 0.60   # polar easterlies: strong but extreme; operational challenges

    # Coastal bonus: sea breezes and unobstructed fetch
    coastal_bonus = 0.20 if province.coastal_length_km > 30 else 0.0

    # Terrain effects
    if province.terrain_type == TerrainType.Plateau:
        terrain_factor = 1.25   # elevated; above boundary layer; excellent exposure
    elif province.terrain_type in {TerrainType.Flatland, TerrainType.CoastalPlain}:
        terrain_factor = 1.00   # open; no obstruction
    elif province.terrain_type == TerrainType.RollingHills:
        terrain_factor = 0.90   # ridge-tops viable; valleys sheltered
    elif province.terrain_type == TerrainType.Mountains:
        terrain_factor = 0.70   # summits excellent; most area sheltered by terrain; logistically hard
    elif province.terrain_type == TerrainType.RainforestBasin:
        terrain_factor = 0.30   # canopy drag; calm interior
    else:
        terrain_factor = 0.80

    # Continentality penalty: deep interiors are sheltered from marine wind systems
    continent_penalty = province.continentality × 0.20

    potential = clamp(
        (lat_wind + coastal_bonus) × terrain_factor - continent_penalty,
        0.0, 1.0
    )

    if potential < 0.10:
        return None   # below viable threshold for utility-scale wind

    return ResourceDeposit(
        resource      = "WindPotential",
        quantity      = potential,
        quality       = potential,
        depth         = 0.0,
        accessibility = 1.0,
        era_available = 2,           # modern wind turbines are Era 2
        is_renewable  = True,
    )
```

Both functions are called in the main Stage 8 seeding loop after all finite resources, appending to `province.resource_deposits[]` only when the return is not `None`. Renewable resources are excluded from `MINIMUM_VIABLE_DEPOSIT` checks and the age-modifier pass.

**Geothermal age modifier:**

```
// Geothermal availability tracks mantle heat flux directly.
// mantle_heat_flux derived in Stage 1 from planet_age_gyr.
// Normalise against Earth reference value:

geothermal_age_modifier(age) =
    mantle_heat_flux(age) / mantle_heat_flux(4.6)  // 4.6 Gyr = Earth reference

// Young planet (1.0 Gyr): modifier ≈ 1.7  →  geothermal abundant; more active geology
// Earth analog (4.6 Gyr): modifier = 1.0  →  baseline
// Old planet  (8.0 Gyr):  modifier ≈ 0.45 →  geothermal scarce
// Ancient     (12 Gyr):   modifier ≈ 0.28 →  limited to residual hotspot activity
```

---

### 8.4 — Seeding Algorithm with Age Modifiers

**`DEPOSIT_MEAN` — lognormal mean quantity by resource and tectonic context:**

All values are in abstract `quantity units` where 1.0 = a medium-sized commercially viable deposit (analogous to a mid-tier real-world field that would sustain one extraction company for 20–30 years at moderate production rates). The lognormal sigma is 0.8 throughout — high variance intentional; deposit sizes span several orders of magnitude in reality.

*Tectonic context abbreviations: Sub = Subduction; CC = ContCollision; Rift = RiftZone; TF = TransformFault; HS = HotSpot; PM = PassiveMargin; Crat = CratonInterior; SB = SedimBasin*

| Resource | Sub | CC | Rift | TF | HS | PM | Crat | SB | Notes |
|---|---|---|---|---|---|---|---|---|---|
| IronOre | 0.8 | 0.7 | 0.6 | — | — | — | 2.5 | 1.2 | BIF deposits enormous; cratons dominant |
| Copper | 2.0 | 0.8 | 0.7 | — | 1.2 | — | 0.6 | — | Porphyry Cu at subduction: largest known deposits |
| Bauxite | — | — | — | — | 0.6 | 1.0 | 1.0 | — | Lateritic; craton surface weathering |
| Lithium | 0.7 | — | 2.0 | — | 1.0 | — | — | 0.5 | Rift brines (Atacama) and pegmatites |
| Gold | 1.5 | 1.0 | 0.6 | — | 0.8 | — | 2.0 | — | Archean cratons: largest goldfields |
| Uranium | — | — | 1.2 | — | — | — | 1.8 | 0.7 | Sandstone roll-front (SB) and craton vein |
| Thorium | — | — | 0.8 | — | — | — | 1.5 | — | Monazite placers; craton weathering |
| Lead | 0.5 | 0.5 | 0.6 | — | — | 0.4 | 1.0 | 0.8 | Sedex and MVT deposits in SB |
| RareEarths | — | — | 1.5 | — | 0.8 | — | 1.2 | — | Carbonatite and laterite sources |
| Diamonds | — | — | 0.7 | — | — | — | 2.0 | — | Kimberlite pipes; deep craton roots only |
| Nickel | 0.6 | — | 0.7 | — | 1.5 | — | 2.0 | — | Komatiite-hosted (craton); laterite (HS) |
| PlatinumGroupMetals | — | — | — | — | 0.7 | — | 1.8 | — | Bushveld-type intrusions; craton only |
| Coal | — | — | — | — | — | 0.8 | — | 2.5 | Swamp basin deposition; thickness drives quantity |
| CrudeOil | — | — | 0.6 | — | — | 2.0 | — | 2.5 | Passive margin and basin: majority of reserves |
| NaturalGas | — | — | 0.8 | — | 0.7 | 1.5 | — | 2.5 | Often co-located with oil; also biogenic in SB |
| Potash | — | — | 0.9 | — | — | 0.7 | — | 2.0 | Evaporite basins; ancient restricted seas |
| Phosphate | — | — | — | — | — | 2.0 | 0.6 | 1.0 | Marine upwelling zones; phosphorite |
| Sulfur | 1.5 | — | 1.2 | — | 2.0 | — | — | — | Volcanic fumaroles and cap rock over salt domes |
| Geothermal | 1.8 | — | 2.0 | 0.6 | 3.0 | — | — | — | Hotspot dominant; multiplied by age modifier |
| Sand | — | — | 0.5 | 0.5 | — | 2.0 | 0.5 | 1.0 | See deterministic derivation in 8.3 |
| Aggregate | 0.6 | 1.0 | 0.5 | 0.5 | 0.6 | 0.5 | 1.5 | 1.0 | Quarry rock from hard geology |
| Peat | — | — | — | — | — | 0.6 | 0.5 | 1.0 | Seeded only on Histosol; deterministic by depth |

`—` entries correspond to probability weight 0 in the 8.3 table; `DEPOSIT_MEAN` is not read for these combinations (the seeding loop skips them).

**`MINIMUM_VIABLE_DEPOSIT` — below this quantity, age depletion has made the deposit sub-economic and it is discarded:**

| Resource | Minimum viable quantity | Rationale |
|---|---|---|
| IronOre | 0.10 | Large-volume commodity; only very small deposits are truly uneconomic |
| Copper | 0.08 | Porphyry deposits are huge; skarn deposits can be small but still viable |
| Bauxite | 0.15 | Requires large volume for aluminium refinery economics |
| Lithium | 0.05 | High value/tonne; even small brine or pegmatite deposits are economic |
| Gold | 0.03 | Extreme value/tonne; tiny deposits mined throughout history |
| Uranium | 0.05 | Age depletion primary filter; U-235 especially depleted on old worlds |
| Thorium | 0.05 | Slow decay; threshold rarely triggered except on very ancient planets |
| Lead | 0.10 | Low value; requires size to justify extraction |
| RareEarths | 0.04 | High value but processing complexity; small deposits often uneconomic in Era 1–2 |
| Diamonds | 0.02 | Extreme value; even trace kimberlite economically significant |
| Nickel | 0.08 | Battery demand (Era 3+) makes smaller deposits economic over time |
| PlatinumGroupMetals | 0.02 | Extreme value; very small deposits viable |
| Coal | 0.15 | Bulk commodity; thin seams uneconomic without rail access |
| CrudeOil | 0.10 | Small fields are real (many small North Sea fields) but below threshold here |
| NaturalGas | 0.10 | Associated or independent; small fields often flared rather than captured |
| Potash | 0.15 | Large-volume fertiliser feedstock; requires scale |
| Phosphate | 0.12 | Bulk; requires large deposit for standalone extraction |
| Sulfur | 0.08 | Industrial feedstock; cap-rock sulfur deposits vary widely in size |
| Geothermal | 0.10 | Requires sufficient heat flux for power generation; age-depleted worlds lose viability |
| Sand | 0.05 | Almost always viable; only erg sand fails usability test |
| Aggregate | 0.05 | Quarry rock; virtually always viable where geology permits |
| Peat | 0.05 | Thin peat (<0.5m depth) is not economic for fuel; only soil interest |

`DEPOSIT_MEAN` and `MINIMUM_VIABLE_DEPOSIT` live in `config/resource_deposits.json`. `QUANTITY_SCALE_FACTOR` (global scalar converting lognormal output to internal units) is set in `config/world_generation.json`.

```python
age = planet.planet_age_gyr

# Pre-compute all age modifiers once
modifiers = {
    "Uranium":    uranium_age_modifier(age),
    "Thorium":    thorium_age_modifier(age),
    "Lead":       lead_age_modifier(age),
    "Geothermal": geothermal_age_modifier(age),
    # all Invariant resources: modifier = 1.0
}

for province in all_provinces:
    for resource in ALL_RESOURCES:
        base_prob = probability_table[resource][province.tectonic_context]
        if base_prob == 0:
            continue

        # Tectonic probability draw
        if random_draw(seed=hash(province.id, resource)) > base_prob / 4.0:
            continue

        # Quantity: lognormal base × age modifier
        base_quantity = lognormal(
            mean  = DEPOSIT_MEAN[resource][province.tectonic_context],
            sigma = 0.8
        ) × QUANTITY_SCALE_FACTOR

        quantity = base_quantity × modifiers.get(resource, 1.0)

        if quantity < MINIMUM_VIABLE_DEPOSIT[resource]:
            continue   # age has depleted this deposit below extraction threshold

        quality = beta(alpha=2, beta=2)
        depth   = infer_depth(resource, province.tectonic_context)
        accessibility = compute_accessibility(
            province.infrastructure_rating,
            province.geography.terrain_roughness,
            province.geography.island_isolation
        )

        # Append to province deposits
        province.resource_deposits.append(ResourceDeposit(
            resource      = resource,
            quantity      = quantity,
            quality       = quality,
            depth         = depth,
            accessibility = accessibility,
            era_available = ERA_LOCKS.get(resource, 1),
        ))

    # Natural gas helium fraction: set on any NaturalGas deposit in this province
    for deposit in province.resource_deposits:
        if deposit.resource == "NaturalGas":
            deposit.helium_fraction = compute_he_fraction(
                he4_accumulation(age),
                province.tectonic_context,
                province.rock_type,
            )
            # helium_fraction is a quality multiplier on gas value
            # high He (> 0.07): helium is separately extractable; bonus revenue stream
```

**Helium fraction derivation:**

```python
def he4_accumulation(age_gyr: float) -> float:
    """
    Returns a 0.0–1.0 index of how much radiogenic He-4 has accumulated
    in the crust over the planet's lifetime. He-4 is produced by every
    alpha decay step in U-238, U-235, and Th-232 chains (8 alphas per U-238
    chain; 7 per U-235; 6 per Th-232). Rate is highest when U/Th stocks
    are full (young planet) and declines as parent isotopes are consumed.
    Normalised to 1.0 at Earth analog (4.6 Gyr).
    """
    # Integrated He-4 production is proportional to the *depleted* fraction of U/Th
    # (He produced = parent consumed). Use blended depletion integral.
    u_depleted  = 1.0 - uranium_age_modifier(age_gyr)   # fraction of U consumed → He produced
    th_depleted = 1.0 - thorium_age_modifier(age_gyr)   # fraction of Th consumed

    # Weighted sum: U chains produce ~5× more alpha particles per Gyr than Th at Earth age
    # because U-235 burns fast early. Weight 0.75 U, 0.25 Th.
    raw_accumulation = u_depleted × 0.75 + th_depleted × 0.25

    # Normalise to Earth (4.6 Gyr) value
    earth_u_dep  = 1.0 - uranium_age_modifier(4.6)
    earth_th_dep = 1.0 - thorium_age_modifier(4.6)
    earth_ref    = earth_u_dep × 0.75 + earth_th_dep × 0.25

    return clamp(raw_accumulation / earth_ref, 0.0, 2.0)
    # > 1.0 possible on very old planets; capped at 2.0 (structural trapping limits)


def compute_he_fraction(
    he4_index: float,           # from he4_accumulation(age)
    tectonic_context: TectonicContext,
    rock_type: RockType,
) -> float:
    """
    Returns the helium mole fraction in a NaturalGas deposit (0.0–0.30).
    He-4 accumulates in gas traps that have been structurally intact for
    geological time. Craton interiors and sedimentary basins on old stable
    platforms are the best traps. Young or tectonically active settings
    allow He to escape.

    Earth reference values:
      US Hugoton field (Kansas/Oklahoma): ~1.5–2.0% He  — CratonInterior, old
      Algerian Hassi R'Mel: ~0.05–0.2% He               — SedimBasin, moderate age
      Most gas fields: < 0.1% (below economic threshold)
    """
    # Base fraction from He-4 index
    base = he4_index × 0.04   # Earth CratonInterior ≈ 0.04 at index 1.0

    # Tectonic trap quality: old stable platforms retain He; active margins lose it
    trap_factor = {
        TectonicContext.CratonInterior:  1.0,   // best trap; ancient stable rock; Hugoton analog
        TectonicContext.SedimBasin:      0.70,  // good trap; common gas field setting
        TectonicContext.PassiveMargin:   0.40,  // moderate; some leakage through faults
        TectonicContext.RiftZone:        0.15,  // poor trap; active faulting causes leakage
        TectonicContext.HotSpot:         0.10,  // poor; volcanism drives off gas continuously
        TectonicContext.Subduction:      0.05,  // very poor; crustal recycling destroys traps
        TectonicContext.ContCollision:   0.20,  // moderate; collision folds create traps but also fractures
        TectonicContext.TransformFault:  0.08,  // poor; lateral motion breaks seals
    }.get(tectonic_context, 0.10)

    # Rock type modifier: crystalline basement holds He poorly vs porous sedimentary host rock
    rock_factor = {
        RockType.Sedimentary: 1.0,    // porous; holds gas and He; best host
        RockType.Mixed:       0.65,
        RockType.Metamorphic: 0.40,   // low porosity; He migrates to fractures
        RockType.Igneous:     0.20,   // very low porosity; unusual to find gas here
    }.get(rock_type, 0.5)

    fraction = base × trap_factor × rock_factor
    return clamp(fraction, 0.0, 0.30)
    # > 0.07: economically extractable as standalone helium product
    # > 0.20: exceptionally rich; rare; major helium supply significance
```

**Stage 8 derived Province fields** (set immediately after the seeding loop; used by Stage 9 settlement attractiveness):

```python
# After all deposits seeded:
for province in all_provinces:
    # geothermal_available: true if a viable Geothermal deposit was seeded
    province.geothermal_available = any(
        d.resource == "Geothermal" for d in province.resource_deposits
    )

    # volcanic_soil_bonus: convenience float read by Stage 9 settlement formula
    # Andisol provinces sit on volcanic parent material; extremely fertile;
    # attractive for settlement regardless of other attributes
    province.volcanic_soil_bonus = 1.0 if province.soil_type == SoilType.Andisol else 0.0
```

```cpp
// Added to Province at end of Stage 8:
bool   geothermal_available;    // true if Geothermal resource deposit seeded this province
float  volcanic_soil_bonus;     // 0.0 or 1.0; 1.0 if soil_type == Andisol; used in Stage 9
                                 // settlement attractiveness formula (weight 0.05)
```

---

### 8.5 — Age Effects on Planet Character

The combined effect of age modifiers produces distinct resource profiles for planets of different ages. These are not edge cases — they are design-relevant differences any player on a non-Earth-analog world will encounter.

| Planet age | Uranium | Thorium | Lead | Geothermal | He in gas | Character |
|---|---|---|---|---|---|---|
| 1 Gyr (young) | ~90% | ~95% | Low | ~170% | Trace | Hot, tectonically violent; uranium-rich; geothermal abundant; lead scarce |
| 3 Gyr | ~60% | ~86% | Moderate | ~110% | Low | Active geology; good nuclear feedstock; beginning lead accumulation |
| 4.6 Gyr (Earth) | ~48% | ~80% | Moderate | 100% | Moderate | Baseline; familiar resource distribution |
| 8 Gyr (old) | ~24% | ~64% | High | ~45% | High | Quieter geology; uranium scarce and valuable; lead common; geothermal limited |
| 12 Gyr (ancient) | ~10% | ~55% | Very high | ~28% | Very high | Tectonically near-dead; no viable nuclear from uranium; thorium-cycle reactors the only fission option; lead everywhere; geothermal only at rare ancient hotspots |

**Design implication for old worlds:** A player on a 10+ Gyr planet faces a fundamentally different energy and materials economy. Uranium is a precious strategic resource rather than an industrial input. Lead is abundant and cheap. Geothermal energy is limited to specific geological accidents. The tech tree that assumes uranium availability as a bridge to fission power may not apply — thorium-cycle reactors or pure renewables may be the rational path.

This is not a balance constraint applied from outside. It emerges from the physics the planet's age produces.

---

### 8.6 — Quantity and Quality Distribution

Real mineral deposits follow a **lognormal distribution** — many small deposits, few large ones, rare giants. A uniform distribution would feel fake.

```
depth_classification:
    Surface: laterite (bauxite), placer (gold), peat
    Mid:     coal seams, most metal ores, uranium roll-fronts
    Deep:    oil, gas, deep gold, kimberlite (diamonds)
```

Deposit records written to `province.resource_deposits[]`.

---

### 8.7 — Unconventional Resource Locks

Resources that require technology to access are seeded at Stage 8 but flagged with `era_available`. They exist in the world from generation; they are invisible to the player until the unlock condition is met.

| Resource | era_available | Unlock condition |
|---|---|---|
| Shale oil / tight gas | 2 | Unconventional extraction tech |
| Oil sands | 2 | Heavy oil processing tech |
| Arctic offshore oil | 3 | Arctic drilling tech + sea ice recession |
| Deep sea minerals | 3 | Deep sea mining tech (EX) |
| Lithium brine (salt flat) | 1 | Standard extraction; Era 1 accessible |
| Thorium (as fuel) | 3 | Thorium reactor tech unlock; seeded as mineral from Era 1 |
| Helium extraction from gas | 3 | Helium separation plant; `helium_fraction` field already set |
| Potassium-40 isotope use | 4 | Isotope separation tech (EX); K-40 fraction field set at gen time |

---

### 8.8 — `geology_type` → Deposit Grade Estimation

For deposits where grade is unknown, `regional_average_grade()` uses `geology_type`:

```
regional_average_grade(resource, geology_type) =
    AVERAGE_GRADE_TABLE[resource][geology_type] × normal_variance(sigma=0.15)
```

`geology_type` defined in Stage 1. Resolves the pre-existing [BLOCKER].

---

## Stage 9 — Population and Infrastructure Seeding

Population density is historically driven by agricultural productivity, water access, and coastal access. Infrastructure follows population. Three corrections to the naive formula are essential for physical accuracy: floodplains must attract rather than repel settlement, altitude imposes a hard biological ceiling, and tropical disease burden explains why high-productivity tropical provinces are underpopulated relative to their agricultural output.

### 9.1 — Settlement Attractiveness Formula

```python
def settlement_attractiveness(province):

    # --- Base attractiveness ---
    base =  (
        agricultural_productivity    × 0.35
      + port_capacity                × 0.20
      + river_access                 × 0.20
      + (1.0 - terrain_roughness)   × 0.10
      + (soil_type == Alluvial)     × 0.08   # floodplain soil is a positive signal
      + geothermal_available         × 0.02
      + volcanic_soil_bonus          × 0.05
    )

    # --- Floodplain correction ---
    # Flood_vulnerability is NOT an attractiveness penalty.
    # Historically the world's densest populations live on floodplains: Nile, Ganges,
    # Mekong, Yellow River, Mesopotamia, Bangladesh. Annual flooding deposits nutrients;
    # river access provides irrigation and transport.
    # Flood risk is a cost to INFRASTRUCTURE, not to population.
    # Apply it here as an infrastructure cost modifier only (see 9.2 below).
    # Do NOT subtract flood_vulnerability from attractiveness.

    # --- Altitude ceiling ---
    # Above ~3,500m, most humans cannot sustain productive work or reproduction
    # without multi-generational genetic acclimatisation (Andean/Tibetan populations
    # are the exception, developed over ~10,000 years). The penalty is not gradual
    # terrain roughness — it is a hard physiological ceiling.
    if province.elevation_avg_m > 4500:
        base *= 0.03    # near-uninhabitable; only extreme specialists
    elif province.elevation_avg_m > 3500:
        base *= 0.15    # severe; Tibetan Plateau / Altiplano density levels
    elif province.elevation_avg_m > 2500:
        base *= 0.45    # significant; Colorado / Ethiopian Highlands levels
    elif province.elevation_avg_m > 1500:
        base *= 0.80    # mild; production efficiency reduced; still settled normally

    # --- Disease burden ---
    # Tropical regions historically had far lower settled densities than agricultural
    # productivity alone predicts. Vector-borne disease (malaria, yellow fever, sleeping
    # sickness, dengue) imposed a mortality and morbidity load that suppressed population
    # growth and deterred immigration. European colonisation of tropical Africa was
    # devastated by disease until quinine and later interventions.
    # Disease burden is derived from climate zone and reduced by infrastructure investment
    # (drainage, sanitation) during gameplay — it is not a fixed modifier.
    disease_burden = compute_disease_burden(province)
    base *= (1.0 - disease_burden × 0.50)
    # Maximum penalty is 0.50× at full disease burden (disease halves attractiveness,
    # not eliminates it — settled populations did exist, just at suppressed levels)

    # --- Environmental penalties (genuine uninhabitability, not mere difficulty) ---
    if KoppenZone in {BWh, BWk}:          base *= 0.12  # true desert; near-zero base population
    if KoppenZone in {EF}:                base *= 0.02  # ice cap; essentially zero
    if KoppenZone in {ET}:                base *= 0.15  # tundra; sparse but real
    if terrain_roughness > 0.85:          base *= 0.25  # near-vertical terrain
    if island_isolation:                   base *= 0.60  # isolation penalty
    if permafrost_type == Continuous:     base *= 0.05  # construction and agriculture both fail

    return clamp(base, 0.0, 1.0)
```

### 9.2 — Disease Burden Derivation

```python
def compute_disease_burden(province) -> float:
    """
    Returns 0.0 (no burden) to 1.0 (maximum historical burden).
    Derived from climate zone, standing water, and elevation.
    Reducible during gameplay by sanitation, drainage, and medical infrastructure.
    """
    burden = 0.0

    # Malaria and yellow fever: warm + standing water + low elevation
    if KoppenZone in {Af, Am}:
        burden += 0.55   # hyperendemic in tropical rainforest; historically devastating
    elif KoppenZone in {Aw, BSh}:
        burden += 0.35   # tropical savanna/steppe; seasonal transmission
    elif KoppenZone in {Cfa, Cwa}:
        burden += 0.15   # humid subtropical; lower but present

    # Standing water amplifies vector habitat
    if province.flood_vulnerability > 0.6:
        burden += 0.15   # floodplains breed mosquitoes; note this does NOT affect
                         # attractiveness directly — the floodplain food/transport
                         # benefit outweighs disease at pre-modern population densities

    # Elevation reduces disease (malaria vector altitude limit ~2,000m)
    if province.elevation_avg_m > 2000:
        burden *= 0.10   # above mosquito ceiling; disease burden near zero
    elif province.elevation_avg_m > 1500:
        burden *= 0.35
    elif province.elevation_avg_m > 1000:
        burden *= 0.65

    return clamp(burden, 0.0, 1.0)
```

**Gameplay implication:** `disease_burden` is a Province field set at world generation. During gameplay, investment in sanitation infrastructure, swamp drainage, and eventually medical research reduces `disease_burden` multiplicatively. A player who drains a malarial delta and builds medical facilities unlocks the province's suppressed population potential — the underlying `agricultural_productivity` was always high; the disease burden was hiding it. This is exactly the historical pattern of tropical development.

### 9.3 — Infrastructure Derivation

```python
infrastructure_rating =
    clamp(
        settlement_attractiveness × 0.70
        - flood_vulnerability × 0.15   # flood risk raises construction cost; lowers infra for given investment
        + random_variance(sigma=0.12),
        0.0, 1.0
    )
```

Flood vulnerability reduces infrastructure (roads, bridges, foundations are costlier and more fragile on floodplains) without reducing population attractiveness. This correctly separates the two effects: the Ganges plain is densely populated and poorly roaded; the Rhine plain is densely populated and well-roaded. Same agricultural attractiveness; different flood vulnerability; different infrastructure outcome.

### 9.4 — Historical Variance Pass

After the formula runs, apply a bounded random offset to each province to simulate historical accidents. Some provinces exceed formula predictions (a mountain pass that attracted transit tolls, a mineral discovery that drew a rush, a river confluence that became a trading city). Some fall short (historical depopulation events, disease epidemics, conquest and displacement). Variance bounded to ±0.15 of the formula result.

The historical variance is the gap that Stage 10.2 `ProvinceHistory` explains — the generation logic runs backwards from this variance to construct a plausible historical narrative for why the province is where it is.


### 9.5 — Nation Formation

Nation formation converts the province graph into a political map. It must run before §9.7 (capital seeding) and Stage 10.1 (language family assignment), both of which require `province.nation_id` to be set. It also fills SC-02: §9.5 was the previously missing section between the historical variance pass (§9.4) and nomadic population (§9.6).

**Design constraints:**

- Nations must be contiguous provinces (no exclaves in V1). Island provinces join the nearest coastal nation via maritime link.
- Nation count scales with the habitable province count of the world — Earth analog targets ~150–200 nations, matching real-world density at game start (early 2000s).
- Mountain ranges, major rivers, and coastlines are natural borders; the algorithm weights against crossing them.
- Every habitable province belongs to exactly one nation. Uninhabitable provinces (EF ice cap; deep desert at attractiveness < 0.02) are unclaimed territory — no nation administers them.
- The algorithm must work from physics fields alone; no hand-placement is possible for non-Earth worlds.

**The `Nation` struct:**

```cpp
struct Nation {
    uint64_t    id;                    // world-seed-derived unique identifier
    std::string fictional_name;        // generated in Stage 10.1 via language family
    H3Index     capital_province_id;   // set in §9.7
    std::string language_family_id;    // set in §9.5.3; references languages/*.json
    std::string secondary_language_id; // optional; bilingual border nations; "" if none
    float       gdp_index;             // 0.0–1.0; aggregated from province fields at sim start
    float       governance_quality;    // 0.0–1.0; seeded from mean infrastructure_rating ± variance
    NationSize  size_class;            // Microstate | Small | Medium | Large | Continental
    bool        is_colonial_power;     // true if nation has ≥1 ColonialDevelopment event in its history
    std::vector<H3Index> province_ids; // all member provinces at game start
};

enum class NationSize : uint8_t {
    Microstate,    // 1–3 provinces; city-states, small island nations
    Small,         // 4–12 provinces
    Medium,        // 13–40 provinces
    Large,         // 41–120 provinces
    Continental,   // > 120 provinces; superstates; rare
};
```

**§9.5.1 — Nation Seed Placement**

Nations grow from seed provinces. Seed placement determines where political power concentrates — high attractiveness provinces become the cores of nations.

```python
def place_nation_seeds(provinces, world_seed) -> list[Province]:
    """
    Select seed provinces for nation formation.
    Target count: sqrt(habitable_province_count) × 1.8, clamped to [20, 400].
    Seeds must be spatially separated — no two seeds closer than 3 H3 grid-disks.
    High settlement_attractiveness provinces are preferentially selected.
    """
    habitable = [p for p in provinces
                 if p.settlement_attractiveness > 0.10
                 and p.terrain_type not in {TerrainType.ArcticTundra,
                                            TerrainType.GlacialLakeCountry}
                 and p.KoppenZone != KoppenZone.EF]

    target_count = clamp(int(sqrt(len(habitable)) * 1.8), 20, 400)
    rng = seeded_rng(world_seed, "nation_seeds")

    # Attractiveness² bias: avoids seeds on thin coastal margins with poor depth
    weights = [p.settlement_attractiveness ** 2 for p in habitable]
    seeds = []

    for province in weighted_sample_without_replacement(habitable, weights, rng):
        # Minimum separation: no seed within 3 grid-disks of an existing seed
        if all(h3_distance(province.h3_index, s.h3_index) > 3 for s in seeds):
            seeds.append(province)
        if len(seeds) >= target_count:
            break

    return seeds
```

**§9.5.2 — Voronoi Growth with Terrain Resistance**

From each seed, nation territory expands outward using a priority-queue Voronoi flood-fill weighted by terrain resistance. Natural barriers increase resistance; open flatland is absorbed cheaply. The result is geographically coherent nations bounded by mountains, rivers, and coasts — the same mechanism that produced real political geography.

```python
def grow_nations(seeds, provinces, province_graph) -> dict[H3Index, int]:
    """
    Weighted Voronoi partition. Returns H3Index → nation_index mapping.
    Dijkstra-style: provinces nearest their seed in terrain-weighted distance
    are assigned first. Each province is assigned to exactly one nation.
    """
    assignment = {}   # H3Index → nation_index
    pq = []           # min-heap: (cost, province_h3, nation_index)

    for i, seed in enumerate(seeds):
        heappush(pq, (0.0, seed.h3_index, i))
        assignment[seed.h3_index] = i

    while pq:
        cost, current_h3, nation_idx = heappop(pq)

        for link in province_graph[current_h3].links:
            neighbor_h3 = link.neighbor_h3
            if neighbor_h3 in assignment:
                continue

            neighbor = province_graph[neighbor_h3]

            # Permanent ice and near-zero-attractiveness tundra left unclaimed
            if neighbor.KoppenZone == KoppenZone.EF:
                continue
            if (neighbor.settlement_attractiveness < 0.02
                    and neighbor.terrain_type == TerrainType.ArcticTundra):
                continue

            resistance = compute_border_resistance(link, neighbor)
            heappush(pq, (cost + resistance, neighbor_h3, nation_idx))
            assignment[neighbor_h3] = nation_idx

    return assignment


def compute_border_resistance(link, neighbor_province) -> float:
    """
    Cost of absorbing this province from the expanding nation.
    Higher resistance = stronger natural border = nations tend to stop here.
    Base cost = 1.0. Multipliers stack; result clamped to [1.0, 20.0].
    """
    resistance = 1.0

    # Mountain ranges: strongest natural borders
    if neighbor_province.terrain_roughness > 0.70:
        resistance *= 4.0   # major range; few nations straddle these
    elif neighbor_province.terrain_roughness > 0.45:
        resistance *= 2.0   # upland; passable but expensive to administer

    # Major rivers: strongly favoured as borders
    if link.type == LinkType.River and neighbor_province.river_access > 0.60:
        resistance *= 2.5

    # Maritime crossing: creates exclave; nations rarely absorb across open water
    if link.type == LinkType.Maritime:
        resistance *= 6.0

    # Desert interior: difficult to project power into; nations stop at edges
    if neighbor_province.KoppenZone in {KoppenZone.BWh, KoppenZone.BWk}:
        resistance *= 1.8

    # Dense rainforest: historically difficult to administer
    if neighbor_province.terrain_type == TerrainType.RainforestBasin:
        resistance *= 2.2

    return clamp(resistance, 1.0, 20.0)
```

**Island assignment** — provinces whose only links are `Maritime` are unreachable by land flood-fill. Assigned post-pass to the nearest coastal nation:

```python
def assign_island_provinces(assignment, provinces, province_graph):
    unassigned_islands = [
        p for p in provinces
        if p.h3_index not in assignment
        and all(lnk.type == LinkType.Maritime for lnk in p.links)
    ]

    for island in unassigned_islands:
        # Assign to nation that owns the maritime-adjacent province with the
        # longest shared border km (best candidate port of call)
        candidates = [
            (assignment[lnk.neighbor_h3], lnk.shared_border_km)
            for lnk in island.links
            if lnk.neighbor_h3 in assignment
        ]
        if candidates:
            assignment[island.h3_index] = max(candidates, key=lambda x: x[1])[0]
        # else: isolated; remains unclaimed territory (rare edge case)
```

**§9.5.3 — Language Family Assignment**

Each nation receives a primary language family, with an optional secondary for bilingual border zones. Language families produce geographic coherence: neighboring nations tend to share or closely relate their families, replicating the regional language zones of Earth history.

```python
def assign_language_families(nations, provinces, available_families, world_seed):
    """
    Two-pass assignment.
    Pass 1: assign each nation's seed province a language family by geographic affinity.
    Pass 2: propagate — neighboring nations have a 60% chance to inherit the dominant
            family of their largest neighbor, producing regional language zones with
            natural fractures where terrain shifts sharply.
    """
    rng = seeded_rng(world_seed, "language_families")
    nation_graph = build_nation_adjacency(nations, provinces)

    # Pass 1: geography-weighted initial assignment
    seed_assignments = {}
    for nation in nations:
        seed_province = provinces[nation.capital_province_id]
        weights = [
            language_geographic_affinity(family, seed_province)
            for family in available_families
        ]
        seed_assignments[nation.id] = weighted_choice(available_families, weights, rng)

    # Pass 2: neighbor propagation (largest nations first; they set regional tone)
    final_assignments = dict(seed_assignments)
    for nation in sorted(nations, key=lambda n: -len(n.province_ids)):
        neighbors = nation_graph[nation.id]
        if not neighbors:
            continue
        largest_neighbor = max(neighbors, key=lambda n: len(n.province_ids))
        if rng.random() < 0.60:
            final_assignments[nation.id] = final_assignments[largest_neighbor.id]

    # Apply assignments; flag secondary for border nations
    for nation in nations:
        nation.language_family_id = final_assignments[nation.id].id
        different_lang_neighbors = [
            n for n in nation_graph[nation.id]
            if final_assignments[n.id].id != nation.language_family_id
        ]
        if different_lang_neighbors:
            nation.secondary_language_id = Counter(
                final_assignments[n.id].id for n in different_lang_neighbors
            ).most_common(1)[0][0]


def language_geographic_affinity(family, province) -> float:
    """
    Returns a weight reflecting how well a language family fits a province's
    geography. Used to prevent cold-climate languages dominating tropical
    zones and vice versa on Earth-analog worlds.
    On non-Earth worlds all families get equal weight (1.0) — geography
    doesn't map to real cultural zones.
    """
    abs_lat = abs(province.latitude)

    affinities = {
        "germanic": (
            1.5 if 45 < abs_lat < 65 and province.precipitation_mm > 400 else
            0.8 if 30 < abs_lat < 75 else 0.3
        ),
        "nordic": (
            1.5 if abs_lat > 55 and province.KoppenZone in {
                KoppenZone.Dfc, KoppenZone.Dfd, KoppenZone.ET, KoppenZone.Cfb}
            else 0.7 if abs_lat > 45 else 0.2
        ),
        "english": (
            1.2 if 30 < abs_lat < 60 and province.coastal_length_km > 20 else
            1.0 if 20 < abs_lat < 65 else 0.5
        ),
    }
    return affinities.get(family.id, 1.0)   # unknown families: neutral weight
```

**§9.5.4 — Pre-Game Border Change Seeding**

`border_change_count` encodes how many times a province changed national administration in the 150-year pre-game window. It is not random — it is derived from structural instability factors. It drives Stage 10.2 border history event generation and the `colonial_remnant` archetype classifier.

```python
def seed_border_changes(provinces, assignment, province_graph, world_seed):
    rng = seeded_rng(world_seed, "border_changes")

    for province in provinces:
        if province.h3_index not in assignment:
            province.border_change_count = 0
            continue

        instability = 0.0

        # Border location: frontier provinces are contested more than cores
        is_border = any(
            province_graph[lnk.neighbor_h3].nation_id != province.nation_id
            for lnk in province.links
            if lnk.type == LinkType.Land
            and lnk.neighbor_h3 in assignment
        )
        if is_border:
            instability += 0.30

        # Strategic value: resources and high attractiveness attract conquest
        if province.resource_deposits:
            best = max(province.resource_deposits, key=lambda d: d.quantity)
            if best.quantity > NOTABLE_THRESHOLD:
                instability += 0.25
        if province.settlement_attractiveness > 0.70:
            instability += 0.15

        # Colonial signature: infra_gap > 0.20 suggests external development
        # by a prior administering power — likely a border change in the pre-game window
        if province.infra_gap > 0.20:
            instability += 0.20

        # Chokepoints and plateaus are contested by neighbors
        if province.is_mountain_pass or province.terrain_type == TerrainType.Plateau:
            instability += 0.15

        # Poisson draw: instability score → expected count → actual count
        # instability = 1.05 maximum → expected ~2.6 border changes over 150 years
        expected = instability * 2.5
        count = poisson_draw(expected, rng)
        province.border_change_count = clamp(count, 0, 6)

        # Colonial development flag: read by colonial_remnant archetype in §10.0
        province.has_colonial_development_event = (
            province.infra_gap > 0.20
            and province.border_change_count > 0
            and province.infrastructure_rating > 0.50
        )
```

**§9.5.5 — Fields Declared at Stage 9.5**

```cpp
// Added to Province at Stage 9.5:
uint64_t  nation_id;                      // which nation this province belongs to.
                                           // FORWARD-COMPAT NOTE: WorldGen uses uint64_t (H3Index width)
                                           // for future EX multi-resolution compatibility. TDD v29 Province
                                           // currently declares nation_id as uint32_t. This is a known
                                           // width discrepancy (audit finding I-10). The GIS pipeline and
                                           // V1 simulation will not exercise the upper 32 bits — V1 world
                                           // has ≤ 65,536 nations (well within uint32_t). When EX WorldGen
                                           // integration ships, TDD Province.nation_id must be widened to
                                           // uint64_t. No data loss for V1 saves; migration is additive.
int32_t   border_change_count;            // times province changed nation in pre-game window;
                                           // 0 = stable; max 6; drives Stage 10.2 events
float     infra_gap;                      // infrastructure_rating − predicted_from_attractiveness();
                                           // positive = better than geography predicts (colonial legacy);
                                           // negative = suppressed by geography or instability
                                           // positive = better than geography predicts (colonial legacy);
                                           // negative = suppressed by geography or instability
bool      has_colonial_development_event; // true → Stage 10.2 generates ColonialDevelopment event;
                                           // read by colonial_remnant archetype classifier
```

`predicted_from_attractiveness(province)` is defined as `province.settlement_attractiveness × 0.70`. See §9.8 for derivation note.

**Execution order within Stage 9:**

```
9.1 — Settlement attractiveness    (no dependencies outside Stage 9)
9.2 — Infrastructure rating        (reads settlement_attractiveness)
9.3 — Population density           (reads infrastructure_rating)
9.4 — Historical variance          (reads population_density)
9.5 — Nation formation             ← reads 9.1–9.4; produces nation_id, border_change_count,
      9.5.1 Seed placement                infra_gap, has_colonial_development_event
      9.5.2 Voronoi growth
      9.5.3 Language family assignment
      9.5.4 Border change seeding
9.6 — Nomadic population           (does not depend on nation_id)
9.7 — Nation capital seeding       (requires nation_id; reads settlement_attractiveness)
9.8 — Nation formation fields      (field declarations; derivation is §9.5 above)
9.9 — Player-eligible provinces    (reads infrastructure_rating, settlement_attractiveness)
```

---

### 9.6 — Nomadic and Pastoral Population

The province model assumes settled population. But large and historically significant biomes — Eurasian steppe, Sahara, Arabian peninsula, Tibetan plateau, Mongolian plateau, Central Asian deserts — carried substantial populations that were mobile rather than fixed. A province model that ignores this reads these regions as near-empty, which is geographically and historically wrong.

**Physics basis:** Nomadic carrying capacity is determined not by agricultural yield from fixed land, but by the ratio of mobile forage to animal metabolic need. Steppe and savanna support large herds; herds support large pastoral populations. The constraint is water distribution and seasonal grazing range, not soil fertility in the agricultural sense.

```cpp
// Added to Province:
float  nomadic_population_fraction;   // 0.0–1.0; fraction of provincial population that is mobile
                                       // added on top of settled population_density
                                       // these people do not appear in fixed settlement attractiveness
float  pastoral_carrying_capacity;    // 0.0–1.0; how well the biome supports mobile grazing
```

**Derivation:**

```python
def seed_nomadic_population(province):
    # Nomadic population only in biomes with sufficient mobile grazing potential
    # but insufficient agricultural productivity for dense settled occupation
    
    pastoral_cap = 0.0
    
    if KoppenZone in {BSk, BSh}:          pastoral_cap = 0.65  // steppe: classic pastoral zone
    elif KoppenZone in {BWh, BWk}:        pastoral_cap = 0.20  // desert: sparse; oases anchor movement
    elif KoppenZone in {Aw}:              pastoral_cap = 0.45  // savanna: seasonal transhumance
    elif KoppenZone in {ET}:              pastoral_cap = 0.30  // tundra: reindeer pastoralism
    elif KoppenZone in {Dfc, Dfd}:        pastoral_cap = 0.15  // taiga fringe; limited
    
    # Reduce by terrain roughness (mountains break up grazing range)
    pastoral_cap *= (1.0 - province.terrain_roughness × 0.50)
    
    # Reduce where agricultural productivity already supports dense settlement
    # (agriculturalists outcompete pastoralists for good land)
    pastoral_cap *= max(0.0, 1.0 - province.agricultural_productivity × 1.5)
    
    province.pastoral_carrying_capacity = clamp(pastoral_cap, 0.0, 1.0)
    
    # Nomadic fraction: highest where pastoral capacity is high and settled capacity is low
    if pastoral_cap > 0.10:
        province.nomadic_population_fraction = pastoral_cap × 0.60
        # 0.60 scaling: pastoral carrying capacity is not fully realised at world gen;
        # nomadic populations were often below carrying capacity due to inter-group conflict
        # and seasonal variance — same logic as historical variance pass for settled population
    else:
        province.nomadic_population_fraction = 0.0
```

**Simulation behaviour:** Nomadic population is mobile — it crosses provincial boundaries seasonally. This is handled at simulation time as a `MigrationFlow` that follows seasonal grazing routes. The province stores `nomadic_population_fraction` as the average fraction resident in any given month; the simulation tracks the actual population with a `NomadGroup` agent that moves on a seasonal schedule seeded from the province graph.

**Political implications:** Nomadic populations in a province create a distinct governance challenge — they are present but do not pay property tax, resist fixed administrative control, and have interests (grazing rights, water access) that conflict with settled farmers. The TDD's existing `RegulatorScrutinyMeter` and community grievance system apply to them but require nomadic-specific grievance sources: enclosure of grazing land, dam construction blocking migration routes, forced sedentarisation policy.

**Explicitly out of scope for V1:** Full nomadic agent simulation. V1 represents nomadic population as a static field on Province. The `NomadGroup` mobile agent and seasonal migration routing are EX tier.

### 9.7 — Nation Capital Seeding

```python
def seed_nation_capitals(nations, provinces):
    """
    For each nation, select the highest settlement_attractiveness province
    as the administrative capital. Constraints prevent capitals in locations
    where stable governance is historically implausible.
    Capital is marked on the province and used by Stage 10 named feature
    generation (capital cities always receive a named feature entry) and by
    NPC government actor placement at simulation start.
    """
    for nation in nations:
        nation_provinces = [p for p in provinces if p.nation_id == nation.id]

        eligible = [
            p for p in nation_provinces
            if p.permafrost_type != PermafrostType.Continuous   # ungovernable in winter
            and p.elevation_avg_m < 4000                        # altitude ceiling for dense settlement
            and p.terrain_type not in {
                TerrainType.Badlands,
                TerrainType.ArcticTundra,
                TerrainType.GlacialLakeCountry,
            }
        ]
        if not eligible:
            eligible = nation_provinces   # fallback: no constraint applies

        capital = max(eligible, key=lambda p: p.settlement_attractiveness)
        capital.is_nation_capital = True
        nation.capital_province_id = capital.h3_index
```

```cpp
// Added to Province at Stage 9.7:
bool  is_nation_capital;   // true if this province is the designated administrative capital
                            // of its nation; used by Stage 10 named feature generation and
                            // NPC government actor placement; has no effect on player starting conditions
```

### 9.8 — Nation Formation Fields

Nation formation is fully specified in §9.5. This section documents the `predicted_from_attractiveness()` helper referenced by the archetype classifier and Stage 10.2, which is computed during the §9.5.4 border change seeding pass rather than in a separate stage.

```python
def predicted_from_attractiveness(province) -> float:
    """
    Baseline infrastructure expected from this province's settlement attractiveness.
    infra_gap = infrastructure_rating − predicted_from_attractiveness(province).
    Positive gap: province has more infrastructure than geography predicts → likely
    colonial development, deliberate policy, or resource extraction investment.
    Negative gap: infrastructure is suppressed below what geography would support →
    likely conflict, isolation, or historical neglect.
    """
    return province.settlement_attractiveness * 0.70
```

This gives `infra_gap = infrastructure_rating − (settlement_attractiveness × 0.70)`, approximating the Stage 9.2 baseline formula without flood_vulnerability and variance terms — those are the noise the gap is measuring around.

Full field declarations are in §9.5.5.

### 9.9 — Player Starting Province (Character Creation, Not World-Gen)

**The player does not start as a head of state, governor, or political figure. The player is a private individual — an entrepreneur, investor, or operator — subject to the same simulation rules as every NPC agent.**

World-gen's role is to produce a set of **player-eligible starting provinces** from which the player selects during character creation. The world generator does not assign a starting province — that is a player choice, not a pipeline output.

```python
def compute_player_eligible_provinces(provinces) -> list[H3Index]:
    """
    Returns provinces the game UI will offer as starting location options.
    Criteria: liveable, early-game accessible, not so isolated that the
    simulation has no meaningful economic activity nearby at Era 1.
    The player character will start as a private individual in whichever
    province they choose — owning nothing, holding no office.
    """
    eligible = []
    for p in provinces:
        if (
            p.settlement_attractiveness > 0.25          # some existing population
            and p.infrastructure_rating > 0.20          # road/port access; not wilderness
            and p.permafrost_type != PermafrostType.Continuous
            and p.elevation_avg_m < 4000
            and p.terrain_type not in {
                TerrainType.Badlands,
                TerrainType.ArcticTundra,
            }
            and not p.is_uninhabited_island              # no economy to participate in
        ):
            eligible.append(p.h3_index)
    return eligible
```

```cpp
// Added to Province at Stage 9.9:
bool  is_player_eligible_start;  // true if this province meets starting location criteria;
                                  // exposed to character creation UI as a selectable option;
                                  // has no effect on NPC simulation; purely a UI hint field
```

**What the player starts with in their chosen province is entirely outside world-gen scope** — starting capital, assets, relationships, and reputation are character creation parameters defined in the GDD. World-gen only determines whether a province is a viable starting location. The player has no privileged treatment in the simulation from that point forward.

---

## Stage 10 — World Commentary and Historical Pre-Population

This stage runs after all simulation data is finalized. It reads the completed province graph and generates three categories of output: **named features**, **province histories**, and **loading commentary**. These are narrative and informational outputs — they do not change any simulation fields. They make the world legible, memorable, and alive before the player's first tick.

Stage 10 is the primary justification for longer generation times. The simulation data is complete by end of Stage 9. Stage 10 is overhead that produces value disproportionate to its cost: a player spending 90 seconds watching their world materialize, reading specific true-to-the-world facts about it, arrives at tick 1 already knowing their world has texture. This is qualitatively different from staring at a progress bar.

---

### 10.0 — Text Generation: Hybrid Template System

Stage 10 produces natural-language text describing province histories, loading commentary, and sidebar facts. The mechanism is a **hybrid**: an LLM generates a large library of sentence fragments offline; the engine assembles fragments at runtime purely from that library, deterministically from seed. No API calls occur during world generation.

#### Why Hybrid

- **Pure template (hand-authored):** Deterministic, fast, no dependencies. Caps at hand-authored quality; sounds formulaic after the player reads a hundred provinces.
- **Pure LLM at runtime:** Rich, varied. Slow (API latency during generation), non-deterministic, network-dependent, expensive at scale.
- **Hybrid:** LLM quality with template performance. The library is generated once and ships with the game. Running the prompts again with higher N extends the library without touching the engine.

#### Fragment Schema

Text is not assembled from complete sentences — it is assembled from **fragments**: short clauses that connect grammatically and tonally. Each fragment occupies a **slot** in an assembly pattern. Fragments have context tags that the engine uses to select contextually appropriate combinations.

```json
{
  "id": "fd_ResourceDiscovery_cause_042",
  "slot": "cause",
  "event_type": "ResourceDiscovery",
  "context_tags": ["elevated", "remote", "low_infrastructure"],
  "text": "Prospectors working the {terrain_type} found {resource_name} deposits in {discovery_era}.",
  "variables": ["terrain_type", "resource_name", "discovery_era"],
  "tone": "matter_of_fact",
  "grammatical_role": "declarative",
  "connects_to": ["context", "consequence"]
}
```

**Variable substitution** replaces `{variable_name}` tokens with values derived from province simulation data:

```json
{
  "variable_map": {
    "terrain_type":     "province.terrain_type → display string",
    "resource_name":    "deposit.resource_type → display string",
    "discovery_era":    "historical_event.years_before_game_start → decade string",
    "nation_name":      "province.nation.fictional_name",
    "population_noun":  "province.population_density → 'settlers'|'miners'|'nomads'|'refugees'",
    "infrastructure_adj": "province.infrastructure_rating → 'rudimentary'|'sparse'|'developed'|'dense'",
    "magnitude_adv":    "historical_event.magnitude → 'modestly'|'significantly'|'catastrophically'"
  }
}
```

#### Assembly Grammar

Each `HistoricalEvent.description` is assembled from a **cause** fragment followed by one or two **context** or **consequence** fragments. The assembly engine selects fragments by matching `context_tags` against the province profile, then draws from matching fragments using the world seed.

```
description =
    cause_fragment(event_type, province_tags, seed)
    + [context_fragment(event_type, province_tags, seed+1)]   // optional; 60% inclusion
    + consequence_fragment(event_type, province_tags, seed+2)

// Combined: typically 2–4 sentences
```

**Combinatorial space per event type:**

```
30 cause fragments × 30 context fragments × 30 consequence fragments = 27,000 combinations
× variable substitution (terrain, resource, era, magnitude) → effectively inexhaustible
```

This is before context tag filtering reduces the eligible pool per province — which increases variety because provinces with unusual characteristics draw from rarer fragment subsets.

**`current_character` assembly** is a single-fragment draw from a pool organised by province archetype. Province archetypes are derived from the simulation fields. The full taxonomy follows.

#### Province Archetype Taxonomy

Archetypes capture the *dominant economic and historical character* of a province — the thing that makes it what it is, not a list of all its attributes. Each archetype maps to a distinct fragment pool for `current_character` text generation. Every province receives exactly one archetype; when multiple conditions apply, priority order determines which wins.

The 24 archetypes, in priority order (first match wins):

```python
def classify_province_archetype(province) -> str:
    """
    Returns one of 24 archetype labels used to index the current_character fragment pool.
    Priority order: more specific conditions first; general fallbacks last.
    """

    # --- Devastated / traumatic states (dominate character regardless of other attributes) ---

    if province.historical_trauma_index > 0.75 and province.infrastructure_rating < 0.30:
        return "war_scar"
        # A region whose identity is defined by recent destruction: bombed cities, cleared villages,
        # depopulated zones. The infrastructure damage IS the current character. Examples: post-war
        # Rhineland, Sarajevo in the 1990s, Grozny. Text tone: terse, present-tense loss.

    if province.historical_trauma_index > 0.65 and province.population_density < 0.20:
        return "hollow_land"
        # Forcibly depopulated or famine-emptied territory. Infrastructure may be intact but
        # feels abandoned. Examples: Irish west after the Famine, Emptied Dust Bowl counties.
        # Text tone: melancholy, silence, absence.

    # --- Resource-dominant (economy defined by what's in the ground) ---

    if province.resource_deposits:
        dominant = max(province.resource_deposits, key=lambda d: d.quantity × d.economic_value_multiplier(era=1))
        if dominant.quantity > NOTABLE_THRESHOLD × 1.5:
            resource_class = classify_resource_class(dominant.resource)

            if resource_class == "hydrocarbons":
                if province.infrastructure_rating > 0.60:
                    return "oil_capital"
                    # Modern extraction enclave: pipelines, company towns, foreign capital.
                    # Examples: Baku, Aberdeen, Dhahran. Text: functional, industrial, transactional.
                else:
                    return "oil_frontier"
                    # The deposit is there; extraction barely started or still prospective.
                    # Examples: pre-development Niger Delta, early Siberian fields.
                    # Text: anticipation, shantytown energy, tension between old and incoming.

            elif resource_class == "metals_precious":
                return "gold_rush"
                # An extraction economy built on precious metals — often violent, transient, unequal.
                # Examples: Witwatersrand, Klondike aftermath, Potosí centuries later.
                # Text: spent glamour, hard men, contested ownership.

            elif resource_class == "metals_industrial":
                return "mining_district"
                # Iron, copper, nickel, lead — the industrial backbone. Company towns, rail spurs,
                # smelter smoke. Examples: Ruhr analogs, Labrador iron ranges, Zambia Copperbelt.
                # Text: matter-of-fact hardness, union halls, shifts.

            elif resource_class == "energy_nuclear":
                return "uranium_territory"
                # Uranium-bearing province; politically sensitive; extraction tightly controlled.
                # Examples: Athabasca Basin, Niger uranium belt. Text: restricted, strategic.

            elif resource_class == "agricultural_commodity":
                return "plantation_economy"
                # A resource province but the resource is a cash crop: rubber, cotton, coffee,
                # cacao. Labour conditions and land ownership are the story.
                # Examples: Colonial Ivory Coast, Ceylonese tea estates. Text: unequal, hot.

    # --- Coastal and maritime character ---

    if province.port_capacity > 0.70 and province.infrastructure_rating > 0.65:
        return "major_port"
        # A city defined by its harbour: cranes, warehouses, multinational shipping agents,
        # the smell of salt and diesel. Examples: Rotterdam, Singapore, Hamburg, Mombasa.
        # Text: transactional, polyglot, always busy.

    if province.is_atoll or (province.island_isolation and province.coastal_length_km > 200):
        return "island_enclave"
        # A small island world with its own logic: fishing, tourism potential, strategic
        # basing value, vulnerability to sea level rise. Text: self-contained, watchful.

    if province.port_capacity > 0.40 and province.cold_current_adjacent:
        return "fishing_port"
        # An economy built on the sea's productivity: trawlers, processing plants, cold stores.
        # Examples: Reykjavik, Newfoundland outports, Peruvian anchoveta coast. Text: seasonal,
        # physical, tied to weather.

    # --- Agricultural character ---

    if province.agricultural_productivity > 0.80 and province.soil_type in {Alluvial, Mollisol}:
        return "breadbasket"
        # The richest agricultural land. Flat, dark soil, horizon-to-horizon crops.
        # Examples: Ukrainian Chernozem belt, Corn Belt, Nile delta. Text: quiet abundance,
        # seasonal rhythm, weather anxiety.

    if province.agricultural_productivity > 0.60 and province.KoppenZone in {Cfb, Cfa, Dfa, Dfb}:
        return "agrarian_interior"
        # Productive but not exceptional farmland. Mixed crops, small towns, established
        # infrastructure. The unremarkable middle of most continents. Text: steady, unhurried.

    if province.agricultural_productivity > 0.50 and province.KoppenZone in {BSk, BSh}:
        return "dryland_farm"
        # Marginal agricultural land where rainfall is the annual gamble. Wheat and grazing,
        # vulnerable to drought, populated below its potential. Examples: Argentine pampas edge,
        # Australian wheat belt, Great Plains shortgrass. Text: dry, exposed, cautious.

    # --- Landscape character (when geography dominates over economy) ---

    if province.terrain_type == TerrainType.Plateau and province.elevation_avg_m > 2500:
        return "high_plateau"
        # The world above the world: thin air, intense light, wide distances. Population sparse
        # but ancient. Examples: Andean altiplano, Tibetan plateau, Ethiopian highlands.
        # Text: elemental, slow, old.

    if province.terrain_type == TerrainType.GlacialLakeCountry:
        return "lake_district"
        # Scoured craton: thousands of lakes, thin soils, boreal forest, waterways everywhere.
        # Examples: Finnish Lakeland, Canadian Shield, Muskoka. Text: quiet, watery, seasonal.

    if province.KoppenZone in {BWh, BWk} and not province.has_artesian_spring:
        return "true_desert"
        # The uncompromising interior: nothing here by accident. Wind, rock, heat or cold.
        # Examples: Rub' al Khali, Taklamakan, Atacama interior. Text: absolute, indifferent.

    if province.is_oasis:
        return "oasis_settlement"
        # An island of life in the desert, defined entirely by its water.
        # Everything — the walls, the prices, the politics — centres on the spring.
        # Examples: Siwa, Kashgar, Tafilalt. Text: dense, guarded, ancient.

    if province.KoppenZone in {BSk, BSh, Aw, ET} and province.nomadic_population_fraction > 0.30:
        return "pastoral_steppe"
        # A province defined by movement rather than settlement: herds, seasonal camps,
        # contested grazing rights. Examples: Mongolian steppe, Kazakh grasslands, Sahel pastoral.
        # Text: horizontal, in-motion, suspicious of fences.
        # KoppenZones: BSk/BSh (cold/hot steppe; core pastoral zones — Mongolia, Kazakhstan, Sahel);
        #              Aw (tropical savanna; seasonal transhumance — West Africa, East Africa);
        #              ET (tundra; reindeer pastoralism — Siberia, Sami territories)
        # Previously had only {ET}, which would never match the real-world steppe archetypes.

    # --- Urban and industrial character ---

    if province.infrastructure_rating > 0.75 and province.population_density > 0.75:
        return "industrial_heartland"
        # Dense, built-out, economically central. Factories, rail yards, dense housing.
        # Not necessarily a port. Examples: Ruhr, Midlands, Ural industrial zone.
        # Text: functional, smoke-stained, employed.

    if (province.infrastructure_rating > 0.70
            and province.infra_gap > 0.15   # better than attractiveness predicts
            and any(e.type == ColonialDevelopment for e in province.history.events)):
        return "colonial_remnant"
        # Infrastructure built for extraction by an external power. The railways run to the
        # port, not between cities. Institutional legacies don't match the current population.
        # Examples: most of sub-Saharan Africa's rail networks. Text: misaligned, underused,
        # full of potential that costs more than it looks.

    # --- Frontier and peripheral ---

    if province.infrastructure_rating < 0.25 and province.resource_deposits:
        dominant = max(province.resource_deposits, key=lambda d: d.quantity)
        if dominant.quantity > MINIMAL_THRESHOLD:
            return "resource_frontier"
            # Extractable wealth but no infrastructure to reach it. The map shows the deposit;
            # the ground shows a dirt track. Text: raw, unfinished, speculative.

    if province.infrastructure_rating < 0.20 and province.population_density < 0.15:
        return "marginal_periphery"
        # Nothing here except the land and the people who couldn't leave. Hard winters or hard
        # heat, thin soil, long roads. Examples: Scottish Highlands outskirts, remote Siberian
        # districts, Andean fringe villages. Text: laconic, enduring.

    # --- Default fallback ---

    return "ordinary_interior"
    # The vast middle: adequately farmed, adequately settled, without a dominant
    # defining feature. Most provinces in most worlds are this. Text: unremarkable in
    # the way that safe and continuous are unremarkable — which is its own kind of wealth.
```

**Resource class helper:**

```python
def classify_resource_class(resource_id: str) -> str:
    mapping = {
        "CrudeOil":         "hydrocarbons",
        "NaturalGas":       "hydrocarbons",
        "Coal":             "hydrocarbons",      // coal is not oil but drives same 'extraction economy' archetype
        "Gold":             "metals_precious",
        "Diamonds":         "metals_precious",
        "PlatinumGroupMetals": "metals_precious",
        "IronOre":          "metals_industrial",
        "Copper":           "metals_industrial",
        "Nickel":           "metals_industrial",
        "Lead":             "metals_industrial",
        "Bauxite":          "metals_industrial",
        "Uranium":          "energy_nuclear",
        "Thorium":          "energy_nuclear",
        "RareEarths":       "metals_industrial",  // strategic but same character
        "Rubber":           "agricultural_commodity",
        "Cotton":           "agricultural_commodity",
        "CoffeeArabica":    "agricultural_commodity",
        "Cacao":            "agricultural_commodity",
        "SugarCane":        "agricultural_commodity",
        "Tea":              "agricultural_commodity",
    }
    return mapping.get(resource_id, "other")
```

**Archetype-to-fragment-pool mapping:**

Each archetype has exactly 50 `current_character` fragments in `template_library.json`, keyed under `current_character.{archetype}`. The province-specific seed ensures different provinces of the same archetype draw different sentences:

```python
fragment_index = hash(province.id, "current_character") % 50
current_character = template_library["current_character"][archetype][fragment_index]
```

**Archetype inventory (23 total):**

| Archetype | Fragment pool key | Defining condition |
|---|---|---|
| war_scar | `war_scar` | High trauma + low infrastructure |
| hollow_land | `hollow_land` | High trauma + very low population |
| oil_capital | `oil_capital` | Major hydrocarbon deposit + built-out infra |
| oil_frontier | `oil_frontier` | Major hydrocarbon deposit + undeveloped |
| gold_rush | `gold_rush` | Precious metals dominant |
| mining_district | `mining_district` | Industrial metals dominant |
| uranium_territory | `uranium_territory` | Nuclear energy resource dominant |
| plantation_economy | `plantation_economy` | Agricultural cash crop dominant |
| major_port | `major_port` | High port_capacity + infrastructure |
| island_enclave | `island_enclave` | Atoll or isolated island |
| fishing_port | `fishing_port` | Port + cold current fishery |
| breadbasket | `breadbasket` | Very high ag productivity + prime soil |
| agrarian_interior | `agrarian_interior` | Good farmland, temperate climate |
| dryland_farm | `dryland_farm` | Marginal farmland + steppe climate |
| high_plateau | `high_plateau` | Plateau terrain + high elevation |
| lake_district | `lake_district` | GlacialLakeCountry terrain |
| true_desert | `true_desert` | Desert climate + no spring |
| oasis_settlement | `oasis_settlement` | Desert + artesian spring |
| pastoral_steppe | `pastoral_steppe` | Tundra/steppe + high nomadic fraction |
| industrial_heartland | `industrial_heartland` | High infra + high density |
| colonial_remnant | `colonial_remnant` | High infra + colonial development event |
| resource_frontier | `resource_frontier` | Low infra + notable deposit |
| marginal_periphery | `marginal_periphery` | Low infra + low density |
| ordinary_interior | `ordinary_interior` | Default fallback |

Total fragment pools for `current_character`: 24 archetypes × 50 fragments = 1,200 sentences. (`ordinary_interior` is archetype #24 — the final else-branch — not a separate default pool.)

**Template size update:** `template_library.json` total fragment count revised upward from ~4,200–8,500 to ~5,400–9,700 to include the full archetype character pool.

#### Prompt Templates

The following prompts are sent to the LLM during the **offline library generation** phase. Each prompt targets one (`event_type`, `slot`) combination. The output is parsed into individual fragment JSON objects and appended to `template_library.json`.

**Master prompt structure:**

```
You are generating sentence fragments for a procedural world generation system.
Each fragment occupies a specific slot in a multi-sentence historical description.
Tone: matter-of-fact, geographically grounded, not dramatic.
Voice: omniscient narrator describing world history as settled fact.
Do not use the word "nestled". Do not use passive constructions where active is possible.
Return exactly {N} fragments as a JSON array. Each object must have keys:
  "text" (the fragment with {variable} placeholders),
  "context_tags" (array of applicable tags from: {TAG_LIST}),
  "tone" (one of: matter_of_fact | melancholy | terse | expansive).
No preamble. No commentary. JSON only.

Fragment slot: {SLOT}
Event type: {EVENT_TYPE}
Slot role: {SLOT_DESCRIPTION}
Province context tags available: {TAG_LIST}
Variables available for substitution: {VARIABLE_LIST}

Generate {N} distinct fragments. Each must be grammatically complete as a {GRAMMATICAL_ROLE}.
Vary: sentence length, opening word, active/passive balance, specificity level.
```

**Per-event-type prompt variants** extend the master with event-specific guidance:

```json
{
  "event_type": "ResourceDiscovery",
  "slot": "cause",
  "slot_description": "States what was discovered, where, and approximately when. Does not explain consequences yet.",
  "N": 50,
  "additional_guidance": "Vary the discoverer: prospectors, surveyors, farmers, herders, miners from elsewhere. Vary the discovery mechanism: accidental, systematic survey, following animal tracks to mineral licks, noticing surface staining.",
  "example_acceptable": "Copper-bearing rock outcrops were identified along the {terrain_type} escarpment during a survey in {discovery_era}.",
  "example_unacceptable": "In a momentous discovery that would change the region forever, brave prospectors found {resource_name}."
},
{
  "event_type": "ResourceDiscovery",
  "slot": "consequence",
  "slot_description": "States the lasting economic or demographic effect. Past tense. Should explain current province state.",
  "N": 50,
  "additional_guidance": "Vary outcomes: boom-then-bust, sustained extraction, abandoned, still active, ownership disputed, environmental legacy. Avoid implying the resource is necessarily exhausted.",
  "example_acceptable": "The {resource_name} workings drew labour from three neighbouring provinces and remain the largest employer in the district.",
  "example_unacceptable": "This led to a period of great prosperity for all who lived there."
}
```

**`current_character` prompt:**

```
Generate {N} single-sentence province character descriptions for the archetype: {ARCHETYPE}.
Each sentence describes what it feels like to be in this place — not its history, its present condition.
First-person is not allowed. No "you". Write as an outside observer.
Tone: understated, specific, never romantic or condescending.
Vary: what aspect of the province is foregrounded (economy, landscape, people, atmosphere, built environment).
The sentence must work without knowing the province name.

Archetype description: {ARCHETYPE_DESCRIPTION}

Generate {N} distinct sentences. Return as JSON array of strings. No preamble.
```

#### Library Structure

```json
{
  "schema_version": 1,
  "generated_at": "2024-01-15",
  "total_fragments": 8420,

  "fragments": {
    "HistoricalEvent": {
      "ResourceDiscovery": {
        "cause":       [ /* 50 fragment objects */ ],
        "context":     [ /* 50 fragment objects */ ],
        "consequence": [ /* 50 fragment objects */ ]
      },
      "ForcedRelocation": { ... },
      "BorderChange":     { ... }
      // ... 19 event types × 3 slots = 57 pools × 50 = 2,850 historical fragments
    },
    "current_character": {
      "war_scar":             [ /* 50 strings */ ],
      "hollow_land":          [ /* 50 strings */ ],
      "oil_capital":          [ /* 50 strings */ ],
      "oil_frontier":         [ /* 50 strings */ ],
      "gold_rush":            [ /* 50 strings */ ],
      "mining_district":      [ /* 50 strings */ ],
      "uranium_territory":    [ /* 50 strings */ ],
      "plantation_economy":   [ /* 50 strings */ ],
      "major_port":           [ /* 50 strings */ ],
      "island_enclave":       [ /* 50 strings */ ],
      "fishing_port":         [ /* 50 strings */ ],
      "breadbasket":          [ /* 50 strings */ ],
      "agrarian_interior":    [ /* 50 strings */ ],
      "dryland_farm":         [ /* 50 strings */ ],
      "high_plateau":         [ /* 50 strings */ ],
      "lake_district":        [ /* 50 strings */ ],
      "true_desert":          [ /* 50 strings */ ],
      "oasis_settlement":     [ /* 50 strings */ ],
      "pastoral_steppe":      [ /* 50 strings */ ],
      "industrial_heartland": [ /* 50 strings */ ],
      "colonial_remnant":     [ /* 50 strings */ ],
      "resource_frontier":    [ /* 50 strings */ ],
      "marginal_periphery":   [ /* 50 strings */ ],
      "ordinary_interior":    [ /* 50 strings */ ]
      // 24 archetypes × 50 = 1,200 character fragments
    },
    "loading_commentary": {
      "stage_2_erosion":    [ /* 30 geological description fragments */ ],
      "stage_3_hydrology":  [ /* 30 river-formation fragments */ ],
      "stage_4_atmosphere": [ /* 30 climate-pattern fragments */ ],
      "stage_8_resources":  [ /* 30 resource-deposit fragments */ ],
      "stage_9_population": [ /* 30 settlement-pattern fragments */ ]
      // ~5 stages × 30 = 150 loading fragments
    },
    "sidebar_facts": {
      "chokepoint":    [ /* 30 fragments about trade route significance */ ],
      "crater":        [ /* 30 fragments about impact structures */ ],
      "river":         [ /* 30 fragments about river systems */ ],
      "resource":      [ /* 30 fragments about deposits */ ],
      "climate":       [ /* 30 fragments about weather patterns */ ],
      "geology":       [ /* 30 fragments about rock and soil */ ],
      "population":    [ /* 30 fragments about settlement patterns */ ]
      // ~7 categories × 30 = 210 sidebar fragments
    }
  }
}
```

**Total library size:** ~4,200–8,500 fragments depending on generation depth. At 100 bytes average per fragment, under 1MB. Ships with the game as a data file. Expanding the library means running the prompts with higher N and appending — no schema changes.

#### Extending the Library

The prompt templates are the canonical specification. Running them produces more fragments. A modder adding a new `HistoricalEventType` writes the prompt variant for that type; running it appends 150 new fragments (50 × 3 slots) to the library; the engine handles it without modification.

---

#### Library Generation Tooling

The offline generation phase is a standalone tool separate from the game engine. It reads prompt templates, calls the LLM API, validates outputs, and appends to `template_library.json`. The tool runs once before shipping; it runs again when the library is extended. It is not run at player world generation time.

**Tool invocation:**

```bash
# Generate fragments for all prompt templates not yet in the library:
econlife-libgen generate \
    --templates prompts/template_library_prompts.json \
    --output data/template_library.json \
    --model claude-sonnet-4-20250514 \
    --skip-existing      # skip event_type+slot combinations already at target N
    --target-n 50        # fragments per pool

# Generate only a specific event type:
econlife-libgen generate \
    --event-type ResourceDiscovery \
    --output data/template_library.json

# Validate an existing library file without generating:
econlife-libgen validate \
    --input data/template_library.json \
    --schema data/template_library_schema.json
```

**Generation loop per prompt:**

```python
def generate_fragment_pool(prompt_variant, target_n, model):
    """
    Calls LLM API and returns validated fragment objects.
    Retries on validation failure. Deduplicates against existing pool.
    """
    prompt = build_prompt(MASTER_PROMPT_TEMPLATE, prompt_variant)
    
    max_attempts = 3
    for attempt in range(max_attempts):
        response = llm_api.complete(
            model   = model,
            prompt  = prompt,
            max_tokens = target_n × 120,   // ~120 tokens per fragment at 50-word average
        )
        
        try:
            fragments = json.parse(response.text)
        except JSONDecodeError:
            # LLM returned non-JSON; retry with stricter prompt
            prompt += "\nCRITICAL: Return ONLY a JSON array. No other text."
            continue
        
        # Validate each fragment object
        valid, invalid = validate_fragments(fragments, prompt_variant)
        
        if len(valid) >= target_n × 0.90:   // accept if 90%+ pass validation
            return valid[:target_n]
        
        # Too many invalid; retry
        if attempt < max_attempts - 1:
            log.warning(f"{len(invalid)} invalid fragments; retrying attempt {attempt+2}")
    
    # After max attempts: log failures, return whatever we got, flag for human review
    log.error(f"Pool incomplete after {max_attempts} attempts: {len(valid)}/{target_n}")
    return valid
```

**Validation rules per fragment object:**

```python
REQUIRED_KEYS = {"text", "context_tags", "tone"}
VALID_TONES   = {"matter_of_fact", "melancholy", "terse", "expansive"}

# Complete set of valid context_tags for fragment tagging and retrieval filtering.
# A fragment's context_tags declare when it is appropriate to use it.
# The fragment assembly system may filter by these tags (e.g. only use fragments
# tagged "coastal" for coastal provinces). Any tag not in this list is a validation error.
ALL_CONTEXT_TAGS = {
    # Geography
    "coastal", "inland", "island", "mountainous", "flatland", "arid", "tropical",
    "arctic", "temperate", "boreal", "deltaic", "floodplain", "highland",
    # Hydrology
    "river_access", "snowmelt_fed", "endorheic", "spring_fed", "oasis",
    # Economy
    "agricultural", "pastoral", "industrial", "extractive", "maritime", "fishing",
    "trade_hub", "subsistence",
    # Historical
    "colonial", "conflict", "traumatised", "frontier", "ancient_settlement",
    "border_territory", "depopulated",
    # Political
    "contested", "stable", "autonomous", "remote",
    # Resource
    "hydrocarbon", "mineral", "timber", "fishery", "geothermal",
}

def validate_fragment(fragment, prompt_variant):
    errors = []
    
    # Structural
    for key in REQUIRED_KEYS:
        if key not in fragment:
            errors.append(f"missing key: {key}")
    
    # Tone enum
    if fragment.get("tone") not in VALID_TONES:
        errors.append(f"invalid tone: {fragment.get('tone')}")
    
    # Text quality checks
    text = fragment.get("text", "")
    if len(text.split()) < 5:
        errors.append("fragment too short (<5 words)")
    if len(text.split()) > 80:
        errors.append("fragment too long (>80 words)")
    if "nestled" in text.lower():
        errors.append("banned word: nestled")
    if text.endswith("...") or text.endswith("…"):
        errors.append("fragment is incomplete (trailing ellipsis)")
    
    # Variable placeholders must match declared variables
    declared_vars = set(prompt_variant.get("variable_list", []))
    used_vars     = set(re.findall(r'\{(\w+)\}', text))
    unknown_vars  = used_vars - declared_vars
    if unknown_vars:
        errors.append(f"undeclared variables: {unknown_vars}")
    
    # Context tags must come from the declared tag list
    valid_tags = set(ALL_CONTEXT_TAGS)
    bad_tags   = set(fragment.get("context_tags", [])) - valid_tags
    if bad_tags:
        errors.append(f"invalid context tags: {bad_tags}")
    
    return len(errors) == 0, errors

def validate_fragments(fragments, prompt_variant):
    valid, invalid = [], []
    seen_texts = set()
    
    for f in fragments:
        ok, errors = validate_fragment(f, prompt_variant)
        
        # Deduplication within batch
        text_norm = f.get("text", "").lower().strip()
        if text_norm in seen_texts:
            errors.append("duplicate within batch")
            ok = False
        else:
            seen_texts.add(text_norm)
        
        if ok:
            valid.append(f)
        else:
            invalid.append({"fragment": f, "errors": errors})
    
    return valid, invalid
```

**Append-safe write:** The tool never rewrites the entire library file. It appends pools that are missing or below target N. This makes generation incremental — a partial run can be resumed, and adding new event types doesn't require regenerating existing pools.

```python
def append_to_library(library_path, new_pools):
    library = json.load(open(library_path)) if exists(library_path) else {"fragments": {}}
    
    for pool_key, fragments in new_pools.items():
        existing = library["fragments"].get(pool_key, [])
        
        # Deduplicate against existing library entries
        existing_texts = {f["text"].lower().strip() for f in existing}
        new_unique = [
            f for f in fragments
            if f["text"].lower().strip() not in existing_texts
        ]
        
        library["fragments"][pool_key] = existing + new_unique
        log.info(f"Pool {pool_key}: added {len(new_unique)} fragments; total {len(existing + new_unique)}")
    
    library["total_fragments"] = sum(len(v) for v in library["fragments"].values())
    library["generated_at"] = iso_now()
    json.dump(library, open(library_path, 'w'), indent=2)
```

**Full generation run estimate:**

| Pool type | Pools | Fragments each | API calls | Estimated time |
|---|---|---|---|---|
| HistoricalEvent (3 slots × 19 types) | 57 | 50 | 57 | ~8 min |
| current_character (24 archetypes) | 24 | 50 | 24 | ~3 min |
| loading_commentary (5 stages) | 5 | 30 | 5 | ~45 sec |
| sidebar_facts (7 categories) | 7 | 30 | 7 | ~1 min |
| **Total** | **93** | — | **93** | **~13 min** |

Total API cost at claude-sonnet-4-20250514 pricing is negligible for a one-time generation run — estimated at under $2 for the full initial library. Regeneration only replaces pools that failed validation or need expansion.

---

### 10.1 — Language Families and the Naming System

Names are the player's first interface with the world. A mountain range that sounds plausible — that could exist — grounds everything the player reads about it. A name that sounds machine-generated breaks that. The naming system is entirely data-driven: language families live in JSON config files, the engine reads them, and adding a new language family requires no code changes.

#### Architecture

Each nation is assigned a **language family** at world generation time, based on its geographical position and tectonic history. The language family determines the phoneme inventory, morpheme tables, grammatical patterns, and feature-type suffixes used when naming everything in that nation's territory.

```cpp
struct LanguageFamily {
    std::string  id;               // "germanic" | "nordic" | "english" | user-defined
    std::string  display_name;     // shown in world settings; "Germanic", "Nordic", etc.
    std::string  data_file;        // path to JSON: "languages/germanic.json"

    // Loaded from data_file at world gen time:
    // phonemes, morphemes, suffix tables, compounding rules — all in JSON
    // engine never hardcodes any of this
};
```

The engine's naming function signature:

```cpp
std::string generate_name(
    FeatureType      feature_type,     // what kind of thing is being named
    FeatureContext   context,          // physical characteristics feeding the name
    LanguageFamily&  language,         // which language family to use
    uint64_t         seed              // deterministic per-feature; same seed = same name
);
```

Every parameter that could vary between language families lives in the JSON file. The engine calls `generate_name()` the same way for every language. This is the extensibility guarantee: adding Slavic, Romance, Semitic, or any other family is a JSON authoring task, not an engine task.

#### Language Family JSON Structure

```json
{
  "id": "nordic",
  "display_name": "Nordic",

  "phonemes": {
    "consonants": ["b","d","f","g","h","j","k","l","m","n","p","r","s","t","v","sk","st","gr","br","tr","dr","kn","gn","fl","sl","sn","sp","sv"],
    "vowels": ["a","e","i","o","u","å","ø","æ","ei","au","oy"],
    "allowed_clusters_initial": ["br","dr","fl","fr","gr","kn","sk","sl","sn","sp","st","sv","tr"],
    "allowed_clusters_final":   ["nd","ng","nk","ld","rd","rn","sk","st","ft","lt","rt"]
  },

  "morphemes": {
    "roots": ["vik","fjord","dal","heim","berg","skog","holm","by","stad","mark","nes","havn","strand","vann","elv","foss","lid","bru","ard","orm","ulf","sig","vor","eld","kald","vid","stor","djup"],
    "descriptors": {
      "MountainRange":  ["fjell","koll","nuten","heii","vidda"],
      "River":          ["elv","å","bek","foss","strøm"],
      "Lake":           ["vatn","tjern","sjø","vann"],
      "Desert":         ["ørken","slette","vidde"],
      "Basin":          ["dal","botn","gryta"],
      "Strait":         ["sund","lei","eid"],
      "Bay":            ["fjord","vik","bukt"],
      "Island":         ["ø","øy","holm","holmen"],
      "Plateau":        ["vidda","høyde","flå"],
      "Cape":           ["nes","neset","odde"],
      "Crater":         ["gryta","ketelen","botn"],
      "Plain":          ["slette","flate","mark"]
    }
  },

  "compounding": {
    "style": "compound",
    "separator": "",
    "order": "descriptor_last",
    "example": "Storfjord — stor (large) + fjord"
  },

  "context_vocabulary": {
    "elevation_high":   ["stor","høy","fjell","grand"],
    "elevation_low":    ["lav","djup","dal"],
    "water_present":    ["vann","foss","sjø","elv"],
    "arid":             ["tørr","kald","vid"],
    "fertile":          ["grønn","rik","frukt"],
    "cold":             ["kald","frost","is","sne"],
    "volcanic":         ["eld","glød","heit"],
    "ancient":          ["gammel","ur","eld-"]
  },

  "phonotactics": {
    "max_syllables": 4,
    "min_syllables": 1,
    "prefer_syllables": 2,
    "avoid_sequences": ["aaa","iii","ts","dz"]
  }
}
```

Germanic and English follow the same schema with their own phoneme inventories, morpheme tables, and compounding rules.

#### V1 Language Families

Three families ship with the base game. Each covers a set of nations assigned to it at world generation time. Assignment uses a weighted random distribution seeded from the world seed — the same seed always produces the same language distribution.

The Nordic JSON (below) is the canonical reference schema. Germanic and English follow the same structure. All three are fully specified here so the Bootstrapper can generate names from any family without inferring anything from descriptions.

---

**Germanic** (`languages/germanic.json`)

Consonant-heavy; compound nouns formed by direct concatenation; the feature-type word appears as the final element. Evokes central European geography — names that feel like they have been worn smooth by centuries of use. Umlaut characters (ä, ö, ü) used internally; mapped to ASCII digraphs (ae, oe, ue) for engine output.

Representative outputs: *Steinbach*, *Grauwald*, *Eisenfels*, *Dunkeltal*, *Rotmark*, *Schwarzsee*, *Kaltgrund*

```json
{
  "id": "germanic",
  "display_name": "Germanic",

  "phonemes": {
    "consonants": ["b","d","f","g","h","k","l","m","n","p","r","s","t","v","w","z",
                   "br","dr","fl","fr","gr","kl","kr","pr","sch","sp","st","str","tr","zw","ch"],
    "vowels": ["a","e","i","o","u","au","ei","eu","ie","ue","ae","oe"],
    "allowed_clusters_initial": ["br","dr","fl","fr","gr","kl","kr","pr","sch","sp","st","str","tr","zw"],
    "allowed_clusters_final":   ["nd","ng","nk","ld","rd","rn","st","ft","lt","rt","cht","ck","rg","rk","rm","rb","rp","rs"]
  },

  "morphemes": {
    "roots": [
      "stein","wald","berg","bach","feld","mark","burg","brand","grau","rot","schwarz",
      "gold","eisen","kalt","dunkel","alt","ober","unter","gross","lang","hoch","tief",
      "neu","frei","wild","sturm","feuer","wasser","sand","licht","schatten","rauch",
      "nebel","frost","wind","tal","fluss","holz","sand","kies","erz","blei","braun",
      "weiss","blau","gruen","leer","rau","glatt","spitz","breit","eng","weit","tot",
      "voll","leer","rein","trocken","nass","heiss","alt","jung","stark","schwach"
    ],
    "descriptors": {
      "MountainRange": ["gebirge","berge","fels","grat","kamm","ruecken","horn","massiv","wand","zinnen"],
      "River":         ["bach","fluss","strom","wasser","graben","rinne","aue","furt"],
      "Lake":          ["see","teich","weiher","becken","sumpf","kessel"],
      "Desert":        ["wueste","heide","oede","steppe","flur","brache"],
      "Basin":         ["tal","becken","grube","senke","mulde","kessel","grund"],
      "Strait":        ["sund","kanal","enge","durchfahrt","pass","meerenge"],
      "Bay":           ["bucht","foerde","hafen","wiek","bogen"],
      "Island":        ["insel","werder","holm","eiland","aue"],
      "Plateau":       ["hochflaeche","tafel","hochland","riff","platte"],
      "Cape":          ["spitze","nase","eck","horn","zunge","kap"],
      "Crater":        ["kessel","grube","trichter","loch","becken","napf"],
      "Plain":         ["ebene","flur","aue","feld","weite","mark","flaeche"],
      "Forest":        ["wald","forst","hain","gehoelz","busch","dickicht"],
      "Peninsula":     ["halbinsel","zunge","nehrung","landzunge","sporn"],
      "Archipelago":   ["inseln","schaeren","gruppe","kette","ring"]
    }
  },

  "compounding": {
    "style": "compound",
    "separator": "",
    "order": "descriptor_first",
    "capitalise_result": true,
    "example": "Steinbach: Stein (stone) + bach (stream). Schwarzwald: schwarz (black) + wald (forest).",
    "elision_suffixes": ["e","en","er","s"],
    "genitive_s_probability": 0.12
  },

  "context_vocabulary": {
    "elevation_high":    ["hoch","ober","gipfel","berg","fels","kamm","spitz"],
    "elevation_low":     ["tief","unter","tal","grund","senke","sohle"],
    "water_present":     ["wasser","bach","see","fluss","aue","nass","feucht"],
    "arid":              ["trocken","duerr","oede","sand","staub","kahl"],
    "fertile":           ["frucht","reich","gruen","gold","satt","voll"],
    "cold":              ["kalt","frost","eis","schnee","grau","rauh"],
    "volcanic":          ["feuer","brand","glut","rauch","asche","schwarz"],
    "ancient":           ["alt","ur","grau","erz","stein","eisern"],
    "forested":          ["wald","holz","forst","dunkel","gruen","dicht"],
    "coastal":           ["strand","kuste","hafen","wasser","see","salz"],
    "mineral_iron":      ["eisen","rot","erz","grau","stahl"],
    "mineral_gold":      ["gold","reich","glanz","hell","gelb"],
    "mineral_coal":      ["schwarz","kohle","brand","dunkel","tief"],
    "exposed_windy":     ["sturm","wind","kahl","rau","oede","nackt"]
  },

  "phonotactics": {
    "max_syllables": 5,
    "min_syllables": 2,
    "prefer_syllables": 3,
    "avoid_sequences": ["aau","iie","uua","schsch","stst","kk","tt","pp"],
    "word_final_devoicing": true
  },

  "special_rules": {
    "umlaut_ascii_map": {"ä":"ae","ö":"oe","ü":"ue"},
    "compound_note": "When first element ends in consonant cluster and second begins with consonant cluster, insert linking vowel 'e': Stein+fels → Steinfels (no insert needed); Wald+rand → Waldrand. Avoid triple consonants."
  }
}
```

---

**Nordic** (`languages/nordic.json`)

Vowel-rich; feature type usually appears as a suffix or standalone second element. Evokes Scandinavian and Old Norse naming — stark, geographical, often describes exactly what a place is. Diacritics (å, ø, æ, ö) used internally; output as ASCII-safe equivalents for engine (aa, oe/o, ae, o).

Representative outputs: *Storfjord*, *Kaldvatn*, *Djupmark*, *Grønnelv*, *Skovholm*, *Uldvida*, *Isfjell*

```json
{
  "id": "nordic",
  "display_name": "Nordic",

  "phonemes": {
    "consonants": ["b","d","f","g","h","j","k","l","m","n","p","r","s","t","v",
                   "sk","st","gr","br","tr","dr","kn","gn","fl","sl","sn","sp","sv","ng","nd"],
    "vowels": ["a","e","i","o","u","aa","oe","ae","ei","au","oy","y"],
    "allowed_clusters_initial": ["br","dr","fl","fr","gr","kn","sk","sl","sn","sp","st","sv","tr"],
    "allowed_clusters_final":   ["nd","ng","nk","ld","rd","rn","sk","st","ft","lt","rt"]
  },

  "morphemes": {
    "roots": [
      "vik","fjord","dal","heim","berg","skog","holm","by","stad","mark","nes","havn",
      "strand","vann","elv","foss","lid","bru","ard","orm","ulf","sig","vor","eld","kald",
      "vid","stor","djup","grønn","hvit","svart","rod","gul","blaa","lang","kort","høy",
      "lav","bred","smal","rund","flat","bratt","glatt","tørr","vaat","ung","gammel",
      "sterk","vild","stille","fri","tom","full","ren","uren"
    ],
    "descriptors": {
      "MountainRange": ["fjell","koll","nuten","heii","vidda","rygg","egg","tind","knaus"],
      "River":         ["elv","aa","bek","foss","strøm","sund","eid","løp"],
      "Lake":          ["vatn","tjern","sjø","vann","pytt","myr"],
      "Desert":        ["ørken","slette","vidde","hei","flate","øde"],
      "Basin":         ["dal","botn","gryta","skål","søkk"],
      "Strait":        ["sund","lei","eid","straume","gap"],
      "Bay":           ["fjord","vik","bukt","kil","vik"],
      "Island":        ["ø","øy","holm","holmen","skjær"],
      "Plateau":       ["vidda","høyde","flaa","slette","plateau"],
      "Cape":          ["nes","neset","odde","pynt","tange"],
      "Crater":        ["gryta","ketelen","botn","skål","grop"],
      "Plain":         ["slette","flate","mark","eng","lende"],
      "Forest":        ["skog","lund","holt","kratt","li"],
      "Peninsula":     ["nes","halvøy","tange","odde","pynt"],
      "Archipelago":   ["øyer","skjærgaard","gruppe","krans","rekke"]
    }
  },

  "compounding": {
    "style": "compound",
    "separator": "",
    "order": "descriptor_last",
    "capitalise_result": true,
    "example": "Storfjord: stor (large) + fjord. Kaldvatn: kald (cold) + vatn (lake)."
  },

  "context_vocabulary": {
    "elevation_high":    ["stor","høy","fjell","grand","koll","bratt"],
    "elevation_low":     ["lav","djup","dal","flat","botn"],
    "water_present":     ["vann","foss","sjø","elv","vaat","blaa"],
    "arid":              ["tørr","kald","vid","øde","flat"],
    "fertile":           ["grønn","rik","frukt","god","full"],
    "cold":              ["kald","frost","is","sne","hvit","graa"],
    "volcanic":          ["eld","glød","heit","svart","rauk","brann"],
    "ancient":           ["gammel","ur","eld-","gamal","old"],
    "forested":          ["skog","grønn","tett","mørk","lund"],
    "coastal":           ["hav","strand","havn","salt","sjø","vik"],
    "mineral_iron":      ["jern","rod","erz","graa","malm"],
    "mineral_gold":      ["gull","rik","glans","lys","gul"],
    "mineral_coal":      ["svart","kol","mørk","djup","brand"],
    "exposed_windy":     ["storm","vind","kald","nakne","vid","raa"]
  },

  "phonotactics": {
    "max_syllables": 4,
    "min_syllables": 1,
    "prefer_syllables": 2,
    "avoid_sequences": ["aaa","iii","ts","dz","ngng"],
    "ascii_map": {"å":"aa","ø":"oe","æ":"ae","ö":"o","ü":"u","ä":"a"}
  }
}
```

---

**English** (`languages/english.json`)

Structurally more varied than Germanic or Nordic — four distinct compounding patterns with weighted selection. Heavy use of archaic English morphemes (*mere*, *tarn*, *beck*, *gill*, *fell*, *hurst*, *combe*) for natural features; modern constructions for high-infrastructure provinces. Possessive forms (*Harker's Reach*) introduce a sense of named history without requiring backstory.

Representative outputs: *Ironmere*, *Coldwater Reach*, *The Black Fen*, *Harker's Crossing*, *Stormheath*, *Dunmoor*, *Ashgill*, *Whitefell*, *Brackenfold*

```json
{
  "id": "english",
  "display_name": "English",

  "phonemes": {
    "consonants": ["b","d","f","g","h","k","l","m","n","p","r","s","t","v","w","y",
                   "bl","br","cl","cr","dr","fl","fr","gl","gr","pl","pr","sc","sk",
                   "sl","sm","sn","sp","st","sw","tr","tw","sh","ch","th","wh",
                   "shr","spl","spr","str","thr"],
    "vowels": ["a","e","i","o","u","ay","ee","ar","or","er","ow","oo","igh","air","ire","oy"],
    "allowed_clusters_initial": ["bl","br","cl","cr","dr","fl","fr","gl","gr","pl","pr","sc","sk","sl","sm","sn","sp","st","sw","tr","tw","wh","shr","spl","spr","str","thr"],
    "allowed_clusters_final":   ["ld","nd","nk","nt","rd","rk","rn","rp","rt","sk","sp","st","ft","lt","lk","mp","ng","nch","rth","rst","lf","rf"]
  },

  "morphemes": {
    "roots": [
      "black","grey","red","cold","stone","iron","dark","bright","long","high","low","deep",
      "swift","still","old","new","wild","broad","bare","hollow","sharp","broken","white",
      "green","ash","brack","silver","amber","dusk","storm","salt","bitter","fair","foul",
      "bleak","stark","lone","far","near","north","south","east","west","upper","nether",
      "great","little","lesser","common","king","queens","devils","monks","grey","blue","brown"
    ],
    "descriptors": {
      "MountainRange": ["ridge","range","heights","fells","peaks","crags","scarp","edge","back","tor","scar"],
      "River":         ["beck","burn","gill","brook","water","stream","run","fleet","bourne","rill","leat"],
      "Lake":          ["mere","tarn","water","pool","fen","broad","loch","mere"],
      "Desert":        ["heath","moor","waste","barrens","flats","fen","wold","plain"],
      "Basin":         ["dale","vale","hollow","combe","dene","bottom","bowl","slack"],
      "Strait":        ["reach","channel","sound","narrows","passage","gate","roads","roads"],
      "Bay":           ["bay","cove","haven","bight","creek","inlet","roads","hole"],
      "Island":        ["holm","island","isle","ayt","inch","eyot","ey"],
      "Plateau":       ["moor","upland","top","ridge","tableland","wold","down"],
      "Cape":          ["point","head","ness","nose","bill","nab","naze","foreland","brow"],
      "Crater":        ["hollow","pit","bowl","hole","ring","cup","sink"],
      "Plain":         ["fen","marsh","moor","flat","common","plain","levels","carrs","levels"],
      "Forest":        ["wood","forest","shaw","hurst","grove","copse","chase","weald","dene"],
      "Peninsula":     ["peninsula","neck","tongue","point","head","spit","nose"],
      "Archipelago":   ["islands","isles","skerries","rocks","group","scatter"]
    },

    "archaic": {
      "comment": "Selected preferentially for natural features in rural and wilderness provinces. Modern terms for high-infrastructure urban provinces.",
      "water_features":   ["mere","tarn","beck","gill","bourne","fleet","burn","rill","leat","dyke","carr","broad"],
      "elevated_terrain": ["fell","scar","howe","knowe","law","pike","tor","nab","noup","kame"],
      "lowland_terrain":  ["carr","fen","ley","ing","bottom","garth","slack","holme","haugh"],
      "settled_places":   ["wick","worth","thorpe","stow","holm","gate","bar","garth","fold","croft"]
    }
  },

  "compounding": {
    "style": "multi",
    "separator": "",
    "modes": [
      {
        "id": "descriptor_last",
        "weight": 0.45,
        "pattern": "{descriptor}{feature_word}",
        "example": "Ironmere, Blackstone, Coldwater, Ashgill, Stormheath"
      },
      {
        "id": "descriptor_first",
        "weight": 0.25,
        "pattern": "{descriptor} {feature_word}",
        "separator": " ",
        "example": "Black Fen, Cold Beck, Dark Fell, Long Reach, High Moor"
      },
      {
        "id": "possessive",
        "weight": 0.15,
        "pattern": "{name}'s {feature_word}",
        "separator": "'s ",
        "example": "Harker's Reach, Dunmore's Point, Aldric's Crossing",
        "possessive_pool": [
          "Harker","Dunmore","Aldric","Wulfstan","Oswin","Cynric","Leofwin",
          "Eadric","Thurstan","Godwin","Aelwyn","Ulfric","Merric","Calder",
          "Gareth","Hawthorn","Kenric","Eldric","Morven","Selwyn","Ingram",
          "Corvyn","Destan","Halwyn","Morcant","Sigric","Aldwin","Ceadda",
          "Forthred","Grimkel","Oslac","Wulfric","Aethelred","Ceolwulf"
        ]
      },
      {
        "id": "the_prefix",
        "weight": 0.10,
        "pattern": "The {descriptor} {feature_word}",
        "example": "The Black Fen, The Long Reach, The High Fell, The Bitter Mere"
      },
      {
        "id": "simple_archaic",
        "weight": 0.05,
        "pattern": "{archaic_root}{feature_word}",
        "example": "Brackenhollow, Ashcombe, Brackenfold, Whinmoor"
      }
    ],
    "capitalise_result": true,
    "infrastructure_skews_mode": {
      "comment": "Provinces with infrastructure_rating > 0.7 prefer descriptor_first and possessive. Wilderness provinces prefer descriptor_last and archaic.",
      "high_infra_weights":  [0.25, 0.35, 0.25, 0.10, 0.05],
      "low_infra_weights":   [0.55, 0.15, 0.10, 0.05, 0.15]
    }
  },

  "context_vocabulary": {
    "elevation_high":    ["high","long","fell","top","crest","scar","sharp","bare","bald"],
    "elevation_low":     ["low","hollow","bottom","deep","flat","slack","nether"],
    "water_present":     ["water","beck","burn","gill","fleet","broad","fen","marsh","mere"],
    "arid":              ["dry","bare","heath","waste","parched","grey","dust","brent"],
    "fertile":           ["green","rich","good","gold","broad","fair","sweet","lush"],
    "cold":              ["cold","grey","frost","bleak","bitter","white","stark","chill"],
    "volcanic":          ["black","ash","scorch","burn","smoke","dark","fire","brand"],
    "ancient":           ["old","elder","grey","hoar","stone","barrow","ash","elder"],
    "forested":          ["wood","shaw","hurst","dark","deep","weald","brack","den"],
    "coastal":           ["sea","shore","strand","haven","salt","wind","cliff","ness"],
    "mineral_iron":      ["iron","grey","ore","steel","black","rust","forge"],
    "mineral_gold":      ["gold","bright","fair","gleam","amber","yellow"],
    "mineral_coal":      ["black","coal","dark","pitch","soot","carbon"],
    "exposed_windy":     ["bare","bleak","scour","wind","storm","cold","grey","brant"]
  },

  "phonotactics": {
    "max_syllables": 4,
    "min_syllables": 1,
    "prefer_syllables": 2,
    "avoid_sequences": ["aae","ooe","uue","ngn","tst","shs","ww"],
    "allow_silent_letters": true
  }
}
```

#### Naming Rules

**Nation assignment:** Each nation is assigned one primary language family and optionally one secondary (for border regions). Nations sharing a border that belong to different families produce bilingual feature names on shared geography.

**Feature naming pipeline:**

```python
def name_feature(feature, province, language_family, seed):
    rng = seeded_rng(seed)

    # 1. Select physical context from province data
    context = FeatureContext(
        elevation    = province.elevation_avg_m,
        precipitation = province.precipitation_mm,
        soil_type    = province.soil_type,
        tectonic     = province.tectonic_context,
        feature_type = feature.type,
        size         = feature.area_km2 or feature.length_km,
    )

    # 2. Select descriptor from context vocabulary
    descriptor = select_descriptor(context, language_family, rng)

    # 3. Select feature-type suffix/root
    type_word = rng.choice(language_family.morphemes.descriptors[feature.type])

    # 4. Apply compounding rules
    name = apply_compounding(descriptor, type_word, language_family.compounding, rng)

    # 5. Phonotactic filter — reject names that violate the family's sound rules
    if not passes_phonotactics(name, language_family.phonotactics):
        return name_feature(feature, province, language_family, seed + 1)  # retry

    return name
```

**The name reflects the place.** A high-elevation, cold, rocky province in a Nordic-language nation produces names with `fjell`, `kald`, `stein`, `is` morphemes. A warm, well-watered river valley produces `grønn`, `elv`, `dal`. The physical character of the province is embedded in its name — the same way real place names encoded what settlers found when they arrived.

**Rivers propagate names upstream:** The river is named at its mouth. Each major tributary inherits a modified form — same root, different suffix — so the Valdenberg, the Upper Valdenberg, and the Valdenberg Fork all read as the same watershed system.

**Cross-border features get two names:** A mountain range on the border between a Germanic nation and a Nordic nation is called *Eisenfjeld* on one side and *Jernfjell* on the other. Both mean the same thing (iron mountain). The `NamedFeature.local_name` field carries the secondary name. NPCs in each nation use their own name. News events use the name of the nation where the story originates.

**Disputed features get diplomatic context:** See border disputes below.

#### Adding New Language Families

New families require only a new JSON file matching the schema above and a one-line registration in `language_families.json`:

```json
{
  "families": [
    { "id": "germanic", "data_file": "languages/germanic.json" },
    { "id": "nordic",   "data_file": "languages/nordic.json"   },
    { "id": "english",  "data_file": "languages/english.json"  }
  ]
}
```

Add a line, author the JSON, the engine picks it up. No recompilation. Families added by mods follow the same path.

#### `NamedFeature` Struct

```cpp
enum class FeatureType : uint8_t {
    MountainRange, River, Lake, Desert, Basin,
    Strait, Bay, Island, Plateau, Cape, Crater,
    Plain, Forest, Peninsula, Archipelago,
};

struct NamedFeature {
    uint64_t     id;
    FeatureType  type;
    std::string  name;              // primary name; language of the dominant nation
    std::string  local_name;        // secondary name if feature crosses language boundary; else ""
    std::string  language_family_id; // which family generated the primary name
    float        significance;      // 0.0–1.0; how often NPCs and news reference this feature
    std::vector<H3Index> extent;    // res-4 cells this feature spans

    // Physical data (populated from pipeline stages):
    float  length_km;               // rivers, coastlines, mountain ranges
    float  area_km2;                // basins, deserts, lakes, plains
    float  peak_elevation_m;        // mountain ranges; 0.0 for others
    bool   is_navigable;            // rivers: can goods travel by river barge on this feature?
    bool   is_disputed;             // spans a national border; diplomatic context seeded
    bool   is_chokepoint;           // straits: high trade significance; geopolitically referenced
};
```

**Border disputes:** Any significant feature spanning a national border receives `is_disputed = true` and generates a `DiplomaticContextEntry` — a background record of competing claims, historical treaties, and contested naming that the NPC political simulation can draw on when generating foreign policy stances, trade disputes, and territorial news events. These are not scripted; they are context that the simulation uses when it needs a reason for tension.

---

### 10.2 — Province Histories

History is what makes the simulation feel inhabited rather than generated. Every province has a past. The infrastructure gap between what the formula predicts and what actually exists is not random noise — it is the residue of specific things that happened. Stage 10.2 makes that residue legible.

Each province receives a `ProvinceHistory` struct. The history explains anomalies in the simulation data, provides context that NPCs carry in their memory, and gives the player a reason to care about a region before they invest in it. A province that looks marginal on the map may have been a colonial capital, an industrial heartland that collapsed, or a place that was deliberately depopulated within living memory. The player who reads the history before entering is better positioned than one who doesn't — and that asymmetry is intentional.

```cpp
enum class HistoricalEventType : uint16_t {
    // Settlement and growth
    FoundingEvent,              // when and why the province was first significantly settled
    TradeRouteEstablished,      // explains elevated infrastructure in otherwise marginal province
    ResourceDiscovery,          // gold rush, oil strike, mineral find; population spike
    PortDevelopment,            // deliberate infrastructure investment; explains port_capacity
    ColonialDevelopment,        // outside power built infrastructure for extraction; may explain
                                // high port_capacity + low local_ownership
    MigrationInflux,            // large population movement in; explains cultural mix, rapid growth

    // Disruption and decline
    PopulationCollapse,         // famine, plague, conquest, forced relocation; low pop
    InfrastructureDestruction,  // war, disaster; low infra despite high attractiveness
    ResourceDepletion,          // old mining province now depressed; extraction wound down
    EnvironmentalDisaster,      // flood, eruption, drought; current vulnerability context
    EconomicCollapse,           // regional depression, deindustrialisation; explains unemployment
    ForcedRelocation,           // deliberate depopulation by a state; trauma index elevated
    Famine,                     // crop failure or blockade; lasting demographic scar

    // Geopolitical
    BorderChange,               // province was part of a different nation; cultural legacy survives
    OccupationHistory,          // occupied by a foreign power; institutional distrust of authority
    IndependenceEvent,          // colonial holding that gained independence; explains institutional gaps
    CivilConflict,              // internal war or insurgency; infrastructure damage + political divide
    TreatyProvision,            // assigned to current nation by treaty; may be resented

    // Geological and environmental
    ImpactEvent,                // the asteroid strike; explains crater, mineral alteration
    VolcanicEvent,              // eruption that shaped the soil or destroyed the settlement
    FloodEvent,                 // catastrophic flood that created the alluvial plain
    ClimateShift,               // a province that was once wetter/drier; explains ruins of old farms
                                // or cities now below or above optimal conditions
};

struct HistoricalEvent {
    HistoricalEventType  type;
    int32_t              years_before_game_start; // e.g. -150 = 150 years before 2000 = year 1850
    std::string          headline;                // one sentence: what happened
    std::string          description;             // 2–4 sentences: context and detail
    float                magnitude;               // 0.0–1.0; scale of the event
    std::string          lasting_effect;          // one sentence: what this explains about now
    bool                 has_living_memory;       // within ~80 years; NPC memory can carry this
};

struct ProvinceHistory {
    std::vector<HistoricalEvent> events;          // chronological; 2–8 per province
    std::string                  summary;         // 3–5 sentences; shown in Geographic Encyclopedia
    std::string                  current_character; // one sentence; the feel of the place right now
    float                        historical_trauma_index; // 0.0–1.0; derived from event types and magnitude
                                                  // → elevated NPC grievance susceptibility
                                                  // → lower baseline social stability
                                                  // → higher political volatility
                                                  // → read by political simulation same as any province field
};
```

**The `current_character` field is load-bearing.** It is a single sentence that the UI displays when the player hovers over a province they have not yet entered. It is the province's self-description — written in the voice of someone who lives there, not in the voice of a data table.

Examples of what this field produces:

- *"A declining coal port whose docks are still busy but whose miners left a generation ago."*
- *"A quiet agricultural plain that has never been important to anyone, which is why it has never been destroyed."*
- *"The richest copper district on the continent, which explains why three nations have gone to war over it in the last two centuries."*
- *"A place that used to be a rainforest. The soil remembers, even if the trees are gone."*
- *"A city that rebuilt itself twice — once after the flood, once after the occupation — and carries the scars of both."*

These sentences are generated from the `events` array plus the province simulation fields. They are not templated strings with variable substitution — they are constructed from the actual history.

**Generation logic — reading simulation data backwards:**

```python
for province in all_provinces:
    history = ProvinceHistory()

    # --- Explain infrastructure anomalies ---
    infra_gap = province.infrastructure_rating - predicted_from_attractiveness(province)

    if infra_gap > 0.15:
        # Better than formula predicts — something built it up
        cause = rng.weighted_choice({
            TradeRouteEstablished:  0.30,
            ResourceDiscovery:      0.25,
            ColonialDevelopment:    0.20,
            PortDevelopment:        0.15,
            MigrationInflux:        0.10,
        }, weight_by=province_context(province))
        history.add(generate_event(cause, province, infra_gap))

    elif infra_gap < -0.15:
        # Worse than formula predicts — something damaged or suppressed it
        cause = rng.weighted_choice({
            InfrastructureDestruction: 0.25,
            EconomicCollapse:          0.20,
            ResourceDepletion:         0.20,
            PopulationCollapse:        0.15,
            ForcedRelocation:          0.10,
            CivilConflict:             0.10,
        }, weight_by=province_context(province))
        history.add(generate_event(cause, province, abs(infra_gap)))

    # --- Geology always generates at least one event ---
    history.add(generate_geological_origin(province))  # tectonic_context → founding geology narrative

    if province.has_impact_basin or any(c.age_ma < 500 for c in province.impact_craters):
        history.add(generate_impact_event(province))

    if province.tectonic_context in {Subduction, HotSpot} and rng.chance(0.3):
        history.add(generate_volcanic_event(province))

    # --- Resource context ---
    for deposit in sorted(province.resource_deposits, key=lambda d: -d.quantity):
        if deposit.quantity > NOTABLE_THRESHOLD and deposit.era_available == 1:
            history.add(generate_discovery_event(province, deposit))
            break  # one discovery narrative per province is enough

    # --- Environmental and climate ---
    if province.flood_vulnerability > 0.7:
        history.add(generate_flood_history(province))

    if province.KoppenZone != province.paleoclimate_zone:  # if climate has shifted
        history.add(HistoricalEvent(type=ClimateShift, ...))

    # --- Geopolitical context (from nation seeding in Stage 9) ---
    if province.border_change_count > 0:
        history.add(generate_border_history(province))

    # --- Compute derived fields ---
    history.events.sort(key=lambda e: e.years_before_game_start)
    history.historical_trauma_index = compute_trauma(history.events)
    history.summary                 = synthesize_summary(history.events, province)
    history.current_character       = derive_character(province, history)
```

**`historical_trauma_index` computation:**

```
trauma_index = 0.0

for event in history.events:
    trauma_weight = TRAUMA_WEIGHT[event.type]
        // ForcedRelocation: 0.9; Famine: 0.8; CivilConflict: 0.7
        // PopulationCollapse: 0.65; OccupationHistory: 0.6
        // InfrastructureDestruction: 0.4; EconomicCollapse: 0.35
        // ResourceDepletion: 0.2; FoundingEvent: 0.0
    recency_factor = 1.0 / (1.0 + event.years_before_game_start / 50.0)
        // events within 50 years have full weight; weight halves every 50 years
    trauma_index += trauma_weight × event.magnitude × recency_factor

trauma_index = clamp(trauma_index, 0.0, 1.0)
```

Recent atrocities weigh heavier than ancient ones. A forced relocation 10 years before game start has near-full weight. The same event 200 years ago has ~20% weight — still present in institutional memory and cultural identity, but less acute.

**`historical_trauma_index` downstream effects (simulation reads this field directly):**

```
NPC grievance threshold     -= trauma_index × 0.3   // less provocation needed to activate
community_stability_base    -= trauma_index × 0.25
political_volatility_base   += trauma_index × 0.35
opposition_formation_speed  *= (1.0 + trauma_index × 0.5)
```

A province with `historical_trauma_index = 0.8` is on a hair trigger. The same economic shock that produces mild discontent elsewhere produces a movement here. This is not a magic number — it is the accumulated weight of what happened to this place and how recently.

### 10.3 — Pre-Game Events

Some historical events occurred close enough to game start (within 40 in-game years) that they have living memory in the NPC population and active economic consequences. These are seeded as `PreGameEvent` records that the simulation can reference.

```cpp
struct PreGameEvent {
    HistoricalEventType  type;
    int32_t              years_before_start;   // 1–40 years before January 2000
    H3Index              epicenter_province;
    std::vector<H3Index> affected_provinces;
    float                magnitude;
    std::string          description;

    // Active economic effects at game start:
    float  infrastructure_damage;    // 0.0–1.0 reduction still present at game start
    float  population_displacement;  // fraction of population displaced or lost
    bool   has_active_claim;         // territorial claim still contested at game start
    bool   has_living_witnesses;     // NPC memory entries seeded for older NPCs in affected provinces
};
```

Examples of what this generates:
- A major earthquake 15 years before game start has partially rebuilt infrastructure, living survivors in the NPC population who have memory entries about it, and residual structural vulnerability.
- A resource discovery 30 years before game start explains why a province that would otherwise be marginal has elevated infrastructure and an NPC community that arrived during the boom.
- A border change 20 years before game start means some NPCs in the province have cultural memory of the previous administration and the political instability that surrounds the change is still active.

**NPC seeding:** NPCs generated for provinces with `has_living_witnesses = true` receive one `MemoryEntry` seeded at game start describing the event from their perspective. This is how a regulator NPC in a province devastated by an industrial disaster 10 years ago arrives with genuine institutional wariness that shapes their behavior toward the player — not as a scripted stance but as a memory they actually have.

### 10.4 — Loading Screen Commentary

The loading screen is not a progress bar. It is the world introducing itself.

```cpp
struct LoadingCommentary {
    // Displayed sequentially during generation; each appears as its stage completes
    std::string stage_1_text;   // tectonics: "The continental plates are settling..."
    std::string stage_2_text;   // erosion: specific to the world being generated
    std::string stage_3_text;   // hydrology: names the major rivers as they form
    std::string stage_4_text;   // atmosphere: describes the climate pattern emerging
    std::string stage_5_text;   // soils: what the land will grow
    std::string stage_6_text;   // biomes: the forest and grassland distribution
    std::string stage_7_text;   // features: describes notable special features found
    std::string stage_8_text;   // resources: what the ground holds
    std::string stage_9_text;   // population: where people settled and why
    std::string stage_10_text;  // history: a notable pre-game event
    std::string stage_11_text;  // final: "The world is ready."

    // Sidebar facts: 8–15 short facts displayed in rotation during long stages
    std::vector<std::string> sidebar_facts;
};
```

**Commentary is world-specific, not generic.** Stage 2 text doesn't say "carving river valleys." It says: *"The Valdren River is cutting its canyon through limestone. By the time civilisation arrives, the gorge will be 400 metres deep — too steep to bridge easily, too long to route around."* Stage 8 text doesn't say "seeding resources." It says: *"The Ankhara Basin holds one of the largest untapped oil fields in this world. It won't be accessible until someone figures out how to drill in permafrost."*

**Sidebar facts** are short, specific, true-to-this-world statements generated from the simulation data:
- *"The Veldrath Strait is 43 km wide at its narrowest point. Every tonne of cargo moving between the northern and southern seas passes through it."*
- *"The Cratonwood Shield hasn't had a significant earthquake in 400 million years. The ore deposits it contains have been accumulating since before complex life existed."*
- *"Province Kareth sits at the intersection of three drainage basins. When the spring floods come, water arrives simultaneously from three directions."*
- *"The Surath Desert receives 18mm of rainfall per year. The city of Surath exists because of a single aquifer discovered 200 years ago."*

All sidebar facts are generated from named features, province data, and history records — they are accurate descriptions of actual simulation state, not placeholder text.

### 10.5 — Encyclopedia and In-Game Reference

Stage 10 produces `world_encyclopedia.json` alongside `world.json`. This file is read by the UI layer only — the simulation tick never reads it. It is the player-facing reference layer: all geography, history, and narrative generated by Stage 10 is serialised here in a queryable structure.

#### Schema

```json
{
  "schema_version": "1.0",
  "world_seed":     "uint64 — the seed that generated this world",
  "generated_at":   "ISO-8601 timestamp",
  "planet_name":    "string — generated name of the primary simulatable body",

  "provinces": {
    "{h3_index}": {
      "province_id":       "H3Index (string)",
      "province_name":     "string — generated place name",
      "archetype":         "string — one of 24 archetype labels; see taxonomy",
      "current_character": "string — single sentence; displayed on hover",
      "summary":           "string — 3–5 sentences; shown in Geographic Encyclopedia panel",

      "history": {
        "events": [
          {
            "type":                   "HistoricalEventType string",
            "years_before_game_start":"int32 — negative = years before 2000",
            "headline":               "string — one sentence",
            "description":            "string — 2–4 sentences",
            "magnitude":              "float 0.0–1.0",
            "lasting_effect":         "string — one sentence",
            "has_living_memory":      "bool"
          }
        ],
        "historical_trauma_index": "float 0.0–1.0"
      },

      "sidebar_facts": [
        "string — 1–3 sidebar fact sentences specific to this province; see 10.4"
      ]
    }
  },

  "named_features": [
    {
      "feature_id":       "string — stable UUID generated from world seed + feature type + location",
      "type":             "NamedFeatureType string — Mountain | River | Desert | Lake | Strait | etc.",
      "name":             "string — generated name from language system",
      "provinces":        ["H3Index array — all provinces this feature spans"],
      "headline_fact":    "string — one sentence: the most interesting true thing about this feature",
      "description":      "string — 2–3 sentences: geographic and economic context",
      "is_disputed":      "bool — spans a national border with competing claims",
      "diplomatic_context_entry": {
        "present": "bool",
        "summary": "string | null — one sentence: the nature of the dispute"
      }
    }
  ],

  "pre_game_events": [
    {
      "event_id":                "string — stable UUID",
      "type":                    "HistoricalEventType string",
      "years_before_start":      "int32 — 1–40",
      "epicenter_province":      "H3Index",
      "affected_provinces":      ["H3Index array"],
      "magnitude":               "float 0.0–1.0",
      "description":             "string — 2–4 sentences",
      "infrastructure_damage":   "float 0.0–1.0 — residual at game start",
      "population_displacement": "float 0.0–1.0 — fraction displaced or lost",
      "has_active_claim":        "bool",
      "has_living_witnesses":    "bool"
    }
  ],

  "loading_commentary": {
    "stage_texts": {
      "stage_1":  "string — tectonics commentary",
      "stage_2":  "string — erosion commentary; names a specific river or canyon",
      "stage_3":  "string — hydrology; names major rivers as they form",
      "stage_4":  "string — atmosphere; describes the emerging climate pattern",
      "stage_5":  "string — soils; what the land will grow",
      "stage_6":  "string — biomes; forest and grassland distribution",
      "stage_7":  "string — special features found",
      "stage_8":  "string — resources; what the ground holds",
      "stage_9":  "string — population; where people settled and why",
      "stage_10": "string — history; a notable pre-game event",
      "stage_11": "string — final: 'The world is ready.'"
    },
    "sidebar_facts": [
      "string — 8–15 short facts; drawn from named features and simulation data"
    ]
  },

  "world_statistics": {
    "planet_age_gyr":             "float",
    "total_province_count":       "int",
    "habitable_province_count":   "int — settlement_attractiveness > 0.15",
    "ocean_province_count":       "int",
    "total_named_features":       "int",
    "total_pre_game_events":      "int",
    "most_common_archetype":      "string",
    "highest_trauma_province":    "H3Index",
    "largest_named_river_length_km": "float",
    "largest_named_desert_area_km2": "float",
    "star_class":                 "string — from Phase 0 star data",
    "solar_constant_wm2":         "float"
  }
}
```

#### Access patterns

The UI queries this file in three contexts:

**Province hover** (`provinces[h3_index].current_character`) — loaded on demand; single string read. No latency issue even for 84,000-province worlds since the JSON is province-keyed.

**Geographic Encyclopedia panel** (`provinces[h3_index].summary` + `history.events[]`) — loaded when player opens the encyclopedia for a specific province. Includes full event list.

**Loading screen** (`loading_commentary.*`) — loaded once at generation time; very small.

**Named feature lookup** (`named_features[]` filtered by `provinces` membership) — used when player clicks on a labelled geographic feature. Linear scan acceptable; feature count is small relative to province count.

#### File size estimates

| World size | Province count | Estimated file size |
|---|---|---|
| Earth-analog | ~84,000 | 180–320 MB (full); 15–25 MB (minimal) |
| Mars-analog | ~82,000 | 170–300 MB (full) |
| Super-Earth | ~340,000 | 700–1,200 MB (full); 60–100 MB (minimal) |

`"minimal"` commentary depth generates only `current_character`, `archetype`, and `sidebar_facts` per province — no full `history.events[]` or `summary`. The `loading_commentary` and `world_statistics` blocks are always generated regardless of depth setting.

**Compression:** `world_encyclopedia.json` should be gzip-compressed on disk. Compression ratio for this text-heavy repetitive structure is typically 8–12×, reducing the Earth-analog full file to 20–40 MB on disk. The simulation never reads this file at runtime, so decompression time is not a concern for the game loop — only for the initial load into the UI encyclopedia cache.

### 10.6 — Runtime Budget

Stage 10 is CPU-bound text generation, not numerically intensive. Runtime scales with province count and commentary depth.

| World size | Province count | Commentary depth | Stage 10 time |
|---|---|---|---|
| Earth-analog | ~84,000 | Full | 15–35s |
| Earth-analog | ~84,000 | Minimal (sidebar facts only) | 3–8s |
| Mars-analog | ~82,000 | Full | 12–30s |
| Super-Earth | ~340,000 | Full | 60–140s |
| Super-Earth | ~340,000 | Minimal | 12–30s |

`commentary_depth` is a scenario file parameter:

```json
"commentary_depth": "full"   // "full" | "minimal" | "none"
```

`"none"` skips Stage 10 entirely. `"minimal"` generates only sidebar facts and current_character strings. `"full"` generates complete ProvinceHistory, PreGameEvent, and NamedFeature records. Default: `"full"`.

---

## Stage 11 — Output

The pipeline outputs two files:

- **`world.json`** — all simulation fields; identical format to the GIS pipeline. No engine code differences between a GIS-seeded and procedurally generated world.
- **`world_encyclopedia.json`** — named features, province histories, pre-game events, and loading commentary. Read by the UI layer only; never read by the simulation tick.

### Scenario File Parameters

```json
{
  "world_generation": {
    "mode": "procedural",
    "seed": 8472910,
    "province_count": 180,
    "tectonic_plate_count": 6,
    "landmass_fraction": 0.35,
    "climate_model": "full_koppen",
    "prevailing_wind_direction": "westerly",
    "resource_abundance_scale": 1.0,
    "archipelago_probability": 0.08,
    "mountain_coverage_target": 0.20,
    "glaciation_intensity": 1.0,
    "quantity_scale_factor": 1.0,
    "raster_resolution_override": null,
    "commentary_depth": "full",
    "planetary_body": "earth_analog"
  },
  "planetary_bodies": [
    {
      "id": "earth_analog",
      "body_name": "Earth-analog",
      "body_type": "Terrestrial",
      "radius_km": 6371.0,
      "surface_gravity_ms2": 9.81,
      "surface_pressure_kpa": 101.3,
      "atmospheric_density_kgm3": 1.225,
      "axial_tilt_degrees": 23.4,
      "rotation_period_hours": 24.0,
      "solar_distance_au": 1.0,
      "magnetic_field_strength": 1.0,
      "planet_age_gyr": 4.6,
      "crustal_thickness_km": 35.0
    }
  ]
}
```

**`planet_age_gyr` is an input; `mantle_heat_flux` is a derived output.** Stage 1 computes `mantle_heat_flux` from `planet_age_gyr` via `derive_mantle_heat_flux()` — see §1 for the formula. The scenario file must specify `planet_age_gyr`; it must not specify `mantle_heat_flux` directly. Doing so bypassed the age-consistent physics and broke the link between uranium/thorium depletion modifiers (Stage 8) and geothermal availability, which all derive from the same `planet_age_gyr` root.

*Previously `"mantle_heat_flux": 0.065` appeared here and `planet_age_gyr` was absent — a C-01/C-02 inconsistency. The `0.065` value is the Earth-analog output of `derive_mantle_heat_flux(4.6)` and is now recovered correctly via the formula rather than being hardcoded.*

`raster_resolution_override`: `null` = recommended res-7 raster. Set to `6` for faster generation on super-Earth scale worlds. No longer required to meet the sub-120s target for Earth or Mars analogs.

`commentary_depth`: `"full"` | `"minimal"` | `"none"`. Controls Stage 10 output depth. Default `"full"`. See Stage 10.6 for runtime budget by setting.

---

## Planetary Parameters, Gravity, and Multi-Body Architecture

### Purpose and Tier

This section specifies the data structures needed to run the world generation pipeline on non-Earth planetary bodies — Mars-analogs, super-Earths, moons — and to support orbital transport in late-game expansion content.

**Tier:** `PlanetaryParameters` struct and gravity-adjusted pipeline formulas are **EX** — they extend the world generator without touching V1 simulation systems. Orbital transport as a transit mode and multi-body `WorldState` are **V3+** (expansion content; not yet specified beyond architecture constraints established here).

The game has no forced end state. Era 5 is open-ended. Generational play through heirs can run decades or centuries of simulated time. Planetary expansion content is a natural arc for that open future — not a hard design commitment at this stage, but the V1 data structures must not foreclose it. The `PlanetaryParameters` struct costs nothing to add to the world gen pipeline now.

### `PlanetaryParameters` Struct

All pipeline stages that currently hardcode Earth-specific constants (`EARTH_GRAVITY = 9.81`, atmospheric scale height, erosion rates) read from this struct instead.

```cpp
enum class BodyType : uint8_t {
    Terrestrial,   // rocky; atmosphere possible; full pipeline applies
    IcyMoon,       // ice shell over liquid ocean; limited pipeline; resource-focused
    Asteroid,      // no atmosphere; no hydrology; resource-only body
    SpaceStation,  // artificial; no world gen; infrastructure-only
};

enum class HydrologyMode : uint8_t {
    Active,        // liquid water present; full Stage 3 simulation
    PalaeoActive,  // ancient channels only; dry now; trace channels from terrain, skip flow sim
    Cryogenic,     // water as ice only; glacier flow; no liquid channels
    None,          // airless body; Stage 3 skipped entirely
};

struct PlanetaryParameters {
    std::string  body_name;
    BodyType     body_type;

    // Gravity
    float surface_gravity_ms2;      // m/s²; Earth=9.81; Mars=3.72; Moon=1.62
    float radius_km;
    float escape_velocity_kms;      // derived: sqrt(2 × g × r); stored for transit use
                                    // Earth=11.2; Mars=5.0; Moon=2.4

    // Atmosphere
    float surface_pressure_kpa;     // Earth=101.3; Mars=0.6; Moon≈0.0
    float atmospheric_density_kgm3; // surface; Earth=1.225; Mars=0.020
    float atmospheric_scale_height_km; // derived: kT/(m_air×g); Earth=8.5; Mars=11.1
                                    // Mars higher than Earth: lower g holds atm. loosely

    // Climate drivers
    float axial_tilt_degrees;       // Earth=23.4; Mars=25.2; affects seasonality amplitude
    float rotation_period_hours;    // Earth=24.0; Mars=24.6; Moon=708.7
                                    // slow rotation → weak Coriolis → simple climate bands
    float solar_distance_au;        // Earth=1.0; Mars=1.52
    float solar_constant_wm2;       // derived: 1361 / solar_distance²; drives temp baseline
    float magnetic_field_strength;  // 0.0–1.0 normalized; Earth=1.0; Mars≈0.0; Moon≈0.0
                                    // low field → elevated surface radiation → health modifier

    // Geology
    float planet_age_gyr;           // SCENARIO INPUT: billions of years; root of all age-dependent physics
                                    // drives Stage 1 mantle_heat_flux derivation, Stage 8 age modifiers,
                                    // and radiogenic helium accumulation
    float mantle_heat_flux;         // DERIVED (not a scenario input): W/m²; computed at Stage 1 init
                                    // via derive_mantle_heat_flux(planet_age_gyr, planet_mass_earth)
                                    // Earth analog (4.6 Gyr) = 0.065; stored here for downstream reads
                                    // drives volcanism probability and geothermal deposit weights
    float crustal_thickness_km;     // thicker = less tectonic activity

    // Pipeline control
    HydrologyMode hydrology_mode;   // derived at pipeline init from pressure + temp + gravity
};
```

### Derived Constants

These are computed once from `PlanetaryParameters` at pipeline initialization and used throughout the 10 stages:

```
escape_velocity_kms    = sqrt(2 × surface_gravity_ms2 × radius_km × 1000) / 1000

atmospheric_scale_height_km =
    (BOLTZMANN_CONSTANT × mean_surface_temp_K)
    / (MEAN_AIR_MOLECULAR_MASS × surface_gravity_ms2)
    / 1000.0
    // Earth: 8.5 km  — density halves every ~5.5 km altitude
    // Mars:  11.1 km — starts so thin the scale height is academic
    // Super-Earth (2g): ~4.3 km — very compressed atmosphere; low ceiling for aviation

solar_constant_wm2     = 1361.0 / (solar_distance_au × solar_distance_au)

hydrology_mode = classify_hydrology(surface_pressure_kpa, surface_gravity_ms2, solar_constant_wm2)
    // Active:       pressure > 10 kPa AND temperature range supports liquid water
    // PalaeoActive: pressure ≤ 1 kPa AND terrain shows channel features (Mars)
    // Cryogenic:    temperature too low for liquid; ice present
    // None:         pressure < 0.001 kPa (effectively vacuum)
```

### How Gravity Propagates Through the Pipeline

**Stage 2 — Erosion**

Fluvial erosion force scales with gravitational acceleration. Water flowing downhill carries more kinetic energy under higher gravity, cuts more aggressively.

```
erosion_rate_adjusted =
    erosion_rate_base
    × (planet.surface_gravity_ms2 / EARTH_GRAVITY) ^ 0.5

// Earth (9.81):        multiplier = 1.00  — baseline
// Mars (3.72):         multiplier = 0.62  — terrain stays sharper; slopes preserved
// Moon (1.62):         multiplier = 0.41  — negligible fluvial erosion; impact/volcanic only
// Super-Earth (19.6):  multiplier = 1.41  — aggressive rounding; deep valleys cut fast
```

Glacial erosion scales similarly — ice flows more slowly under low gravity, producing less pronounced glacial landforms on lower-gravity bodies.

On bodies with `HydrologyMode::None` or `HydrologyMode::Cryogenic`, fluvial erosion is set to zero. Terrain on airless or frozen bodies is dominated by impact cratering and volcanic processes, which are added as a separate pass controlled by `mantle_heat_flux` and `planet_age_gyr`.

**Stage 3 — Hydrology**

`HydrologyMode` controls which sub-stages execute:

| Mode | Stage 3 behavior |
|---|---|
| `Active` | Full simulation: drainage basins, river networks, lakes, deltas, alluvial fans |
| `PalaeoActive` | Channel detection from terrain gradient only; no flow simulation; outputs dry valley features |
| `Cryogenic` | Glacier flow routing only; outputs ice sheet extents and subglacial lake positions |
| `None` | Stage 3 skipped; `river_access = 0.0` on all provinces |

**Stage 4 — Atmosphere**

Atmospheric scale height changes where weather forms and how sharply it thins with altitude:

```
// Rain shadow calculation: orographic lift depends on how atmosphere interacts with terrain.
// On high-gravity worlds (compressed atmosphere), moderate terrain produces significant rain shadows.
// On low-gravity worlds (extended atmosphere), terrain must be much taller to produce same effect.

orographic_lift_adjusted =
    province.terrain_roughness
    × MOUNTAIN_LIFT_COEFFICIENT
    × (EARTH_SCALE_HEIGHT / planet.atmospheric_scale_height_km) ^ 0.7
```

Prevailing wind strength is driven by the Coriolis effect, which scales with rotation rate. A slow-rotating body (`rotation_period_hours > 200`) has weak Coriolis — climate bands are simple latitude rings without complex mid-latitude weather patterns. The rain shadow algorithm still runs but the prevailing wind is nearly absent; precipitation is dominated by latitude-band moisture alone.

```
coriolis_factor = min(1.0, EARTH_ROTATION_RATE / planet_rotation_rate_rad_s)
prevailing_wind_strength *= coriolis_factor
```

Magnetic field strength affects surface radiation, which feeds into the health system for workers in exposed provinces (high-altitude, polar, airless surface):

```
// On Province, at Stage 4:
float surface_radiation_sv_yr =
    BASE_COSMIC_RADIATION
    × (1.0 - planet.magnetic_field_strength) ^ 2
    × altitude_exposure_factor(elevation_avg_m, atmospheric_scale_height_km)
// Earth sea level: ~0.002 Sv/yr (negligible)
// Mars surface: ~0.23 Sv/yr (meaningful health penalty for unshielded workers)
// Airless moon surface: ~0.60 Sv/yr (requires shielding infrastructure)
```

### Aviation and Atmospheric Flight

Lift scales with atmospheric density. The economic viability of aviation as a transport mode changes dramatically by planetary body.

```cpp
struct AviationProfile {
    bool   aviation_possible;         // false if surface_pressure_kpa < 1.0
    float  max_payload_fraction;      // fraction of Earth baseline payload capacity
                                      // derived: (atm_density / EARTH_ATM_DENSITY) ^ 0.7
    float  cost_multiplier;           // vs. Earth baseline air freight cost
                                      // derived: inverse of payload_fraction
    float  max_range_km;              // practical range given atmospheric conditions
    bool   requires_specialized_craft; // true if density < 0.1× Earth; non-standard design needed
};

// Derived from PlanetaryParameters at pipeline init:
AviationProfile compute_aviation_profile(PlanetaryParameters p) {
    if (p.surface_pressure_kpa < 1.0)
        return { .aviation_possible = false };

    float density_ratio = p.atmospheric_density_kgm3 / EARTH_ATM_DENSITY;
    float payload_frac  = pow(density_ratio, 0.7);

    return {
        .aviation_possible          = true,
        .max_payload_fraction       = payload_frac,
        .cost_multiplier            = 1.0 / max(payload_frac, 0.01),
        .max_range_km               = BASE_AIR_RANGE_KM × sqrt(density_ratio),
        .requires_specialized_craft = density_ratio < 0.10,
    };
}
```

**Practical outcomes by planetary body:**

| Body | Atm. density | Aviation viable? | Payload vs. Earth | Notes |
|---|---|---|---|---|
| Earth | 1.225 kg/m³ | Yes | 100% | Baseline |
| Mars | 0.020 kg/m³ | Marginal | ~3% | Rotorcraft only; very limited range; specialized design required |
| Super-Earth (2g) | ~2.5 kg/m³ | Yes | ~175% | Dense air helps lift; 2g raises fuel cost; short-range economics favorable |
| Moon | ~0.0 kg/m³ | No | 0% | Ballistic trajectories only; no atmosphere |

`AviationProfile` is stored on `PlanetaryParameters` at world load. The transit system reads it when evaluating air freight route viability for a given world.

### Orbital Transport — Architecture Constraints (V3+)

Orbital transport is not implemented in V1 or EX. These architecture constraints exist to ensure V1 transit data structures do not foreclose it.

The governing equation is the **Tsiolkovsky rocket equation**:

```
m_propellant / m_total = 1 - e ^ (−Δv / v_exhaust)

// Chemical propellants (kerosene/LOX): v_exhaust ≈ 4.4 km/s

// Earth surface to LEO:    Δv ≈ 9.4 km/s  → propellant fraction = 0.882 → 11.8% payload
// Mars surface to LMO:     Δv ≈ 3.8 km/s  → propellant fraction = 0.580 → 42.0% payload
// Moon surface to LLO:     Δv ≈ 1.8 km/s  → propellant fraction = 0.335 → 66.5% payload
// Super-Earth surface LEO: Δv ≈ 18.0 km/s → propellant fraction = 0.983 →  1.7% payload
```

The super-Earth launch problem is severe by design — a 2g world is nearly inaccessible from the surface with chemical propulsion. This is historically accurate and creates meaningful strategic constraints for expansion content.

**Delta-v between bodies** (from low orbit):

| Route | Δv (km/s) |
|---|---|
| Earth LEO → Moon | 3.9 |
| Earth LEO → Mars | 5.5 |
| Moon → Mars | 5.8 |
| Earth LEO → Earth escape | 3.2 |

**Transit system constraint:** When orbital transport is implemented, it must extend `TransportMode` with an `Orbital` type and add `RocketTransitProfile` alongside the existing `RouteProfile`. Launch windows are not continuous — they repeat on synodic periods. A trade route by rocket has window-gated delivery timing, which breaks continuous supply chain assumptions. Buffer stock becomes mandatory for any operation dependent on orbital transport.

```cpp
// V3+ only — architecture placeholder:
struct RocketTransitProfile {
    float  delta_v_required_kms;
    float  propellant_mass_fraction;    // from Tsiolkovsky; drives cost
    float  launch_cost_per_kg;
    float  transit_time_days;           // Hohmann transfer or direct
    bool   requires_launch_window;      // true for interplanetary
    float  window_frequency_days;       // synodic period
    float  radiation_exposure_sv;       // accumulated crew dose; feeds health system
};
```

### Multi-Body Celestial System (V3+)

If the game world includes simulatable moons or orbital stations, they require their own `WorldState` instances at the appropriate LOD level.

```cpp
// V3+ only — architecture placeholder:
struct CelestialBody {
    uint64_t            id;
    std::string         fictional_name;
    BodyType            type;
    PlanetaryParameters params;
    uint64_t            parent_body_id;    // 0 for primary planet

    // Orbital mechanics
    float  orbital_period_days;            // relative to parent
    float  orbital_distance_km;            // semi-major axis
    float  orbital_inclination_degrees;

    // Simulation
    SimulationLOD  lod_level;
    WorldState*    world_state;            // non-null if LOD 0 or 1; null for LOD 2
    float          resource_index;         // statistical summary for LOD 2 bodies only
};
```

LOD rules for non-primary bodies:
- **LOD 2 (index):** Body has a `resource_index` float. Contributes to resource pricing if extraction is occurring. No province simulation.
- **LOD 1 (simplified):** Body has province-level aggregated economy. Active development occurring. No individual NPC simulation.
- **LOD 0 (full):** Player or major NPC operation based there. Full simulation. Extreme environment modifiers (`surface_radiation_sv_yr`, low-gravity production modifiers, vacuum infrastructure cost) active.

### Generation Time Estimates

The pipeline's runtime scales primarily with the fine raster (res-7) cell count, dominated by Stage 2 (erosion), Stage 3 (hydrology), and Stage 4 (atmosphere). Stage 10 (World Commentary) is CPU-bound text generation, not numerically intensive — its cost scales with province count and `commentary_depth` setting.

**Target:** Sub-120 seconds on a modern 8-core consumer CPU for Earth and Mars analogs. Super-Earth at full resolution is 100–240s, which is within acceptable range. The 60–90 second experience the player sees is a loading screen with active Stage 10 commentary, not a blank progress bar.

**Reference hardware:** 8-core modern consumer CPU (AMD Ryzen 9, Apple M4, Intel i9). Parallel stages use all cores. GPU path (CUDA/Metal) available for erosion and hydrology; atmosphere propagation has directional dependency and is partially sequential.

**Planet parameters:**

| Body | Surface area | Land area | Res-7 land cells | Res-4 provinces |
|---|---|---|---|---|
| Earth-analog | 510 M km² | 149 M km² (29%) | 28.6 M | ~84,000 |
| Mars-analog | 145 M km² | 145 M km² (all land) | 27.8 M | ~82,000 |
| Super-Earth (2× radius) | 2.04 B km² | ~600 M km² (est.) | 115 M | ~340,000 |

**Generation time by stage and platform:**

| Stage | Earth CPU | Earth GPU | Mars CPU | Mars GPU | Super-Earth CPU | Super-Earth GPU |
|---|---|---|---|---|---|---|
| Stage 1 (Tectonics) | <1s | <1s | <1s | <1s | 2–3s | <1s |
| Stage 2 (Erosion) | 15–30s | 10–25s | 8–20s | 6–15s | 60–120s | 40–90s |
| Stage 3 (Hydrology) | 3–8s | 1–3s | 2–5s | 1–2s | 12–35s | 4–12s |
| Stage 4 (Atmosphere) | 5–15s | 2–6s | 1–3s | <2s | 20–60s | 8–25s |
| Stage 5–9 (soils, biomes, features, resources, population) | 3–6s | 3–6s | 3–6s | 3–6s | 10–25s | 10–25s |
| Stage 10 (Commentary, full) | 15–35s | 15–35s | 12–30s | 12–30s | 60–140s | 60–140s |
| Stage 10 (Commentary, minimal) | 3–8s | 3–8s | 3–8s | 3–8s | 12–30s | 12–30s |
| **Total (full commentary)** | **40–95s** | **30–75s** | **27–67s** | **22–54s** | **165–385s** | **125–295s** |
| **Total (minimal commentary)** | **28–68s** | **18–47s** | **18–45s** | **13–32s** | **115–245s** | **65–155s** |

**Mars** generates faster than Earth despite similar land cell count: `HydrologyMode::PalaeoActive` skips full flow simulation (Stage 3 is channel detection only); thin atmosphere simplifies Stage 4; smaller pre-game event pool reduces Stage 10 text generation.

**Super-Earth** at full resolution and full commentary is 165–385s CPU — outside the sub-120s target. Two mitigations:

1. `"commentary_depth": "minimal"` brings it to 115–245s. Still long but acceptable for a once-per-world-creation event.
2. `"raster_resolution_override": 6` drops Stage 2–4 cost by ~7×, bringing the total to 35–80s at the cost of coarser within-province terrain detail (province-level climate, resources, and population outputs are unchanged).

Both options are player-controlled in the scenario file. Default remains `null` / `"full"` — the richest output is the default experience.

**Uncertainty range:** ±50% on all estimates. Implementation quality, cache efficiency, and parallelization depth are the dominant variables. A naive single-threaded implementation runs 5–10× slower. A well-optimized GPU implementation runs 2–3× faster than the GPU estimates above.

---

---

## Solar System Generation — Phase 0

### Purpose and Relationship to WorldGenPipeline

The 11-stage WorldGenPipeline generates a single planetary body. The Solar System Generator is **Phase 0** — it runs before the WorldGenPipeline and determines what bodies the system contains. The WorldGenPipeline then runs once per simulatable body. Non-simulatable bodies (asteroid belts, comet populations, gas giants not yet reached) exist in the solar system data as statistical aggregates.

```
Phase 0: SolarSystemGenerator
    → Star
    → OrbitalBody list (planets, belts, comet populations)
    → per simulatable body: WorldGenPipeline (Stages 1–11)
    → outputs solar_system.json
```

**Tier:** Solar system generation is **EX**. The V1 game uses a single Earth-analog planet with a Sun-analog star. Phase 0 runs silently in the background even for V1 — it generates the solar context that feeds `PlanetaryParameters.solar_constant_wm2` and `PlanetaryParameters.solar_distance_au`, and it seeds the comet event calendar. Nothing in V1 gameplay requires the player to know there is a Phase 0. In EX and V3+ content, the solar system becomes visible and interactable.

---

### Phase 0 Pipeline

```
SolarSystemGenerator {
    Phase 0.1: generate_star()            → spectral type, luminosity, age, habitable zone, frost line
    Phase 0.2: place_planets()            → orbital architecture; Titius-Bode spacing + variance
    Phase 0.3: classify_planets()         → body type, PlanetaryParameters, HydrologyMode per planet
    Phase 0.4: generate_moons()           → moon count and properties; formation mechanism
    Phase 0.5: place_asteroid_belts()     → belt location, composition, Kirkwood gaps, mining index
    Phase 0.6: generate_comet_population()→ Kuiper belt + Oort cloud populations; named periodic comets
    Phase 0.7: generate_solar_weather()   → stellar activity class; CME frequency; flare schedule
    Phase 0.8: select_home_world()        → which body the player starts on; must be in habitable zone
    Phase 0.9: output_solar_system_json() → solar_system.json; feeds per-body WorldGenPipeline
}
```

Each phase is deterministic from the world seed. Same seed = same solar system.

---

### 0.1 — Star Generation

The star determines everything downstream: planetary temperatures, habitability, volcanic activity rates, the frost line that separates rocky from gas planets, and the solar weather threat level.

```cpp
enum class SpectralType : uint8_t {
    O,   // blue supergiant; T > 30,000 K; lifespan ~millions of years; no complex life
    B,   // blue-white; T 10,000–30,000 K; lifespan ~tens of millions of years
    A,   // white; T 7,500–10,000 K; Sirius analog; ~1–2 Gyr lifespan; habitable but brief
    F,   // yellow-white; T 6,000–7,500 K; Procyon analog; good habitable zone; ~3–5 Gyr
    G,   // yellow; T 5,200–6,000 K; Sun analog; ~10 Gyr lifespan; V1 default
    K,   // orange; T 3,700–5,200 K; dimmer; ~15–30 Gyr lifespan; arguably best for life
    M,   // red dwarf; T < 3,700 K; very common; tidal locking; intense flares; ~trillion year lifespan
};

struct Star {
    uint64_t       id;
    std::string    fictional_name;
    SpectralType   spectral_type;

    // Physical properties
    float  mass_solar;                  // relative to Sun = 1.0
    float  luminosity_solar;            // relative to Sun = 1.0; derived from mass
    float  radius_solar;
    float  temperature_k;               // surface temperature
    float  age_gyr;                     // age in billions of years; affects geological activity of planets

    // Habitable zone (liquid water possible on surface)
    float  hz_inner_au;                 // inner edge; derived: 0.95 × sqrt(luminosity_solar)
    float  hz_outer_au;                 // outer edge; derived: 1.37 × sqrt(luminosity_solar)

    // Frost line: where water ice condenses; rocky planets form inside, gas giants outside
    float  frost_line_au;               // derived: ~2.7 × sqrt(luminosity_solar)
                                        // Sun analog: 2.7 AU; K-type: ~1.5 AU; M-type: ~0.3 AU

    // Stellar activity
    float  flare_activity_index;        // 0.0–1.0; M-type young stars: 0.9+; old G-type: 0.1
    float  solar_cycle_years;           // duration of activity cycle; Sun = 11.0; varies ±3 years
    bool   is_binary;                   // second star present; affects orbital stability
};
```

**Spectral type distribution in the generation:** Weighted random draw that favors realistic stellar frequency (M-type stars are ~75% of all stars but poor for gameplay without heavy mitigation; G and K-type are preferred for interesting habitable zones).

```python
spectral_type_weights = {
    M: 0.20,   # reduced from real ~75%; M-type gameplay is hostile without tech
    K: 0.35,   # upweighted; K-type is arguably better for life than G; good gameplay
    G: 0.30,   # Sun analog; familiar; default
    F: 0.10,   # brighter; shorter-lived; more volcanic rocky planets
    A: 0.04,   # complex life barely possible; interesting for short-arc scenarios
    B: 0.01,   # hostile; essentially no habitability
    O: 0.00,   # never generated; no gameplay value
}
```

**Luminosity derived from mass (main sequence):**
```
luminosity_solar ≈ mass_solar ^ 3.5     // for main sequence stars
temperature_k    ≈ 5778 × (luminosity_solar / radius_solar²) ^ 0.25
```

---

### 0.2 — Orbital Architecture: Planet Spacing

Real planetary systems follow a geometric progression in orbital distances — not perfectly regular, but statistically consistent. The Titius-Bode law captures this approximately for our solar system and similar patterns appear in observed exoplanet systems.

**Modified Titius-Bode spacing:**

```python
def place_planets(star, seed):
    rng = seeded_rng(seed)

    # Draw system parameters
    k   = rng.uniform(1.60, 2.10)   # spacing ratio between successive orbits
                                     # Solar System: ~1.73 between inner planets
    a0  = rng.uniform(0.25, 0.55)   # innermost planet distance in AU
    n_planets = rng.int(4, 10)       # total planet count; Solar System = 8

    orbits = []
    for i in range(n_planets):
        base_distance = a0 × k^i
        variance      = rng.normal(0, base_distance × 0.12)  # ±12% variance
        distance_au   = max(base_distance + variance, prev_distance × 1.15)
                        # enforce minimum separation
        orbits.append(distance_au)

    return orbits

# After placing orbits, classify each by zone:
for distance in orbits:
    if distance < star.frost_line_au:
        type = classify_rocky(distance, star)    # TerrestrialPlanet or SuperEarth
    elif distance < star.frost_line_au × 4:
        type = classify_gas(distance, star)      # GasGiant (likely)
    else:
        type = classify_outer(distance, star)    # IceGiant or DwarfPlanet
```

**Why the frost line matters for composition:** Inside the frost line, only high-melting-point materials condense during planet formation — silicates and metals. Outside it, water ice also condenses, dramatically increasing available building material. This is why gas giants form beyond the frost line (more material available) and why rocky planets are small (less to work with).

---

### 0.3 — Planet Classification

Each orbit becomes an `OrbitalBody` with type, physical properties, and — if simulatable — a `PlanetaryParameters` instance.

```cpp
enum class OrbitalBodyType : uint8_t {
    TerrestrialPlanet,  // rocky; ≤ 1.5 Earth radii; thin or no atmosphere possible
    SuperEarth,         // rocky; 1.5–4.0 Earth radii; high gravity; thick atmosphere
    MiniNeptune,        // gas-dominated; 1.5–3.5 Earth radii; dense H/He envelope
    GasGiant,           // Jupiter/Saturn analog; > 50 Earth masses; no solid surface
    IceGiant,           // Uranus/Neptune analog; water/methane/ammonia interior
    DwarfPlanet,        // Pluto analog; large trans-neptunian object; no moon system
    AsteroidBelt,       // aggregate region; BeltProfile; not a single body
    CometPopulation,    // statistical population; KuiperBelt or OortCloud subtype
    Moon,               // satellite; parent_body_id set
    TrojanCluster,      // L4/L5 Lagrange point bodies; minor; aggregate
};

struct OrbitalBody {
    uint64_t          id;
    std::string       fictional_name;
    OrbitalBodyType   type;
    uint64_t          parent_body_id;   // star_id for planets; planet_id for moons; 0 = star

    // Orbital elements
    float  semi_major_axis_au;          // average distance from parent
    float  orbital_period_years;        // Kepler: T² ∝ a³ / M_star
    float  eccentricity;                // 0.0 = circular; 0.2+ = notably elliptical
                                        // high eccentricity → temperature swings; harder to habitize
    float  inclination_degrees;         // from ecliptic; >30° = misaligned; >90° = retrograde
    float  longitude_of_periapsis;      // where closest approach to star occurs

    // Physical properties (solid bodies only; 0.0 for belts/populations)
    float  mass_earth;
    float  radius_earth;
    float  density_gcc;                 // g/cm³; rocky ~5.5; gas ~1.3; ice ~2.0
    float  surface_gravity_ms2;         // derived: g = G × M / r²

    // Habitability flags
    bool   is_in_habitable_zone;
    bool   is_tidally_locked;           // orbital period ≈ rotation period
                                        // likely for planets within 0.5 AU of M/K stars
    bool   has_magnetosphere;           // derived from mass, rotation, core composition
                                        // no magnetosphere → high surface radiation

    // Simulation link (V3+: non-null if WorldGenPipeline run on this body)
    PlanetaryParameters*  planetary_params;   // null for gas giants, belts, populations
    WorldState*           world_state;        // null until actively simulated

    // Sub-objects
    std::vector<uint64_t> moon_ids;
    BeltProfile*          belt;         // non-null for AsteroidBelt, CometPopulation
};
```

**Tidal locking rule:** Any planet within approximately `0.5 × sqrt(luminosity_solar)` AU of its star has `is_tidally_locked = true`. Locked planets have one hemisphere permanently facing the star (extreme heat) and one permanently dark (extreme cold). Habitable regions, if any, exist in the narrow terminator band between them. This creates a fundamentally different settlement pattern to Earth — not biome bands from pole to equator, but a ring around the day-night divide.

---

### 0.4 — Moon Generation

```cpp
enum class MoonFormationMechanism : uint8_t {
    GiantImpact,        // large collision ejected material; forms one large moon
                        // Earth-Moon analog; moon has similar composition to planet
    CoAccretion,        // formed simultaneously with planet from disk material
                        // regular orbit; prograde; composition differs from planet
    Capture,            // captured from asteroid belt or interplanetary space
                        // often retrograde; often irregular orbit; foreign composition
    TidalSpinoff,       // ring material coalesced into moon; Saturnian analog
};

// Moon properties appended to OrbitalBody with type == Moon:
struct MoonProfile {
    MoonFormationMechanism formation;
    float    orbital_distance_planetary_radii;  // how far from planet surface
    bool     is_retrograde;                     // opposite orbital direction to planet's rotation
    bool     is_tidally_locked_to_parent;       // same face always toward planet (like our Moon)
    float    tidal_heating_flux;                // W/m²; significant for inner moons of gas giants
                                                // Europa analog: tidal heating → subsurface ocean
    bool     has_subsurface_ocean;              // derived: tidal_heating sufficient + ice shell
};
```

**Moon count by planet type (probabilistic):**

| Planet type | Typical moon count | Large moon probability | Notes |
|---|---|---|---|
| TerrestrialPlanet | 0–2 | 15% | Large moon requires giant impact |
| SuperEarth | 0–3 | 25% | More mass → more likely to capture or form large moon |
| GasGiant | 4–80 | 95% | Always has major moon system; Galilean analog likely |
| IceGiant | 5–30 | 70% | Significant moon system; Titanian analog possible |
| DwarfPlanet | 0–2 | 20% | Pluto-Charon binary common |

**Tidal heating and subsurface oceans:** Inner moons of gas giants experience intense tidal flexing from the planet's gravity. If a moon is in orbital resonance with other moons (as Io, Europa, and Ganymede are in 1:2:4 resonance), the resonance forces sustained tidal heating. This can melt ice to produce subsurface liquid water oceans despite the moon being far from the star's habitable zone. These are priority targets for late-game resource extraction and colonization.

---

### 0.5 — Asteroid Belts

Asteroid belts form where planet formation was frustrated — typically where gravitational resonances with a gas giant prevented material from accreting into a single body. In our solar system, Jupiter's 2:1 resonance with the belt is the dominant mechanism. The belt never formed a planet not because there wasn't enough material, but because Jupiter kept stirring it up.

```cpp
enum class BeltCompositionType : uint8_t {
    C_Type,   // carbonaceous; outer belt; water-rich; organic material; most common
    S_Type,   // silicaceous; inner belt; rocky silicates; some metals; moderate value
    M_Type,   // metallic; rare; iron-nickel; very high value; targets for mining
    Mixed,    // transition zones; composition gradient across belt
    Icy,      // trans-neptunian; ice-dominated; comet precursors
};

struct KirkwoodGap {
    float  resonance_ratio_n;      // numerator of resonance (e.g. 3 in 3:1)
    float  resonance_ratio_d;      // denominator (e.g. 1 in 3:1)
    float  gap_center_au;          // where the gap falls
    float  gap_width_au;           // how wide the depleted zone is
};

struct BeltProfile {
    float  inner_edge_au;
    float  outer_edge_au;
    float  total_mass_earth;       // e.g. real asteroid belt: ~0.0004 Earth masses
    float  peak_density_au;        // where mass is most concentrated

    // Composition fractions (sum to 1.0):
    float  c_type_fraction;        // 0.0–1.0
    float  s_type_fraction;
    float  m_type_fraction;
    float  other_fraction;

    std::vector<KirkwoodGap> gaps; // resonance gaps; generated from gas giant orbital periods

    // Resource and gameplay properties:
    float  mining_accessibility;   // 0.0–1.0; derived from delta-v to reach and return
                                   // inner belt (lower delta-v): higher accessibility
    float  collision_frequency;    // relative collision rate; affects debris cloud events
    bool   has_notable_bodies;     // true if any asteroid > 500 km diameter present
                                   // named bodies become expedition targets in V3+
};
```

**Kirkwood gap generation:** For each gas giant in the system, compute the orbital resonances that fall within the belt's orbital range:

```python
def compute_kirkwood_gaps(belt, gas_giants):
    gaps = []
    for giant in gas_giants:
        for n in range(1, 5):
            for d in range(1, 4):
                if gcd(n, d) == 1:   # only lowest-form ratios
                    gap_au = giant.semi_major_axis_au × (d/n) ^ (2/3)
                              # Kepler: a ∝ T^(2/3); resonant period = (n/d) × giant period
                    if belt.inner_edge_au < gap_au < belt.outer_edge_au:
                        gaps.append(KirkwoodGap(
                            resonance_ratio_n = n,
                            resonance_ratio_d = d,
                            gap_center_au     = gap_au,
                            gap_width_au      = 0.03 + 0.02 × n,  # stronger resonance = wider gap
                        ))
    return gaps
```

**Composition gradient:** Material closer to the frost line has higher volatile content (C-type dominant). Material closer to the star has been more thoroughly baked (S-type dominant). M-type metallic bodies are rare across the whole belt but concentrated in the middle zones. The gradient is smooth with local variation.

**Named notable bodies:** Any asteroid > 500 km in diameter within a belt is named in Stage 10.1 using the same language family system as planetary features. These become expedition and mining targets in V3+ content, giving the solar system proper nouns before the player ever reaches them.

---

### 0.6 — Comet Populations

Two structurally different populations produce comets with different gameplay signatures.

```cpp
enum class CometPopulationSubtype : uint8_t {
    KuiperBelt,   // 30–55 AU; remnant planetesimals; low inclination; source of short-period comets
    ScatteredDisc, // 30–100 AU; dynamically excited; highly eccentric orbits; transitional population
    OortCloud,     // 1,000–100,000 AU; isotropic; source of long-period comets; perturbed by stars
};

struct CometPopulation {
    // Inherits BeltProfile fields for physical extent
    CometPopulationSubtype  subtype;
    float  inner_edge_au;
    float  outer_edge_au;
    float  total_estimated_objects;         // statistical count; not individually simulated
    float  median_object_diameter_km;
    float  composition_ice_fraction;        // water ice, CO2, methane, ammonia
    float  composition_dust_fraction;
    float  composition_organic_fraction;    // complex carbon compounds; prebiotic interest

    std::vector<NamedComet> periodic_comets; // individually tracked comets with known periods
};

struct NamedComet {
    uint64_t     id;
    std::string  name;                      // named via Stage 10.1 language family system
    float        perihelion_au;             // closest approach to star
    float        aphelion_au;               // farthest point (semi-major axis = average)
    float        orbital_period_years;      // short-period: < 200; long-period: > 200
    float        inclination_degrees;       // short-period: low; long-period: any
    float        eccentricity;              // short-period: 0.5–0.9; long-period: >0.98
    float        nucleus_diameter_km;
    float        next_perihelion_year;      // relative to game start year 2000
                                            // if negative: last perihelion was before game start

    // Gameplay hooks:
    bool   is_impactor_candidate;           // orbit intersects home world's orbit at low MOID
    float  impact_probability_per_pass;     // 0.0–1.0; very small for most; peaks at 0.001
    bool   is_scientifically_notable;       // bright; triggers observation opportunities
    bool   is_economically_notable;         // V3+: large enough / close enough for extraction
};
```

**Short-period comets (Kuiper Belt origin):**
- Orbital periods 3–200 years
- Low inclination (< 30° from ecliptic)
- Predictable returns; appear as scheduled events in the simulation calendar
- Scientifically notable if perihelion < 1.5 AU (bright enough to observe from home world)
- A comet with a 75-year period returns roughly once per character lifetime — a genuine once-in-a-lifetime event for older characters

**Long-period comets (Oort Cloud origin):**
- Orbital periods > 200 years; many >10,000 years
- Random inclination; some retrograde
- Functionally unpredictable on human timescales; appear as random events weighted by comet population density
- Very rarely: impact threat (orbit passes through habitable zone on eccentric path)

**Comet event calendar generation:**

```python
def seed_comet_events(periodic_comets, game_start_year=2000):
    events = []
    for comet in periodic_comets:
        # Calculate all perihelion passes within a plausible game duration
        # Assume game can run to year 2500 in generational play (500 years)
        year = comet.next_perihelion_year
        while year < 2500:
            event = CometEvent(
                comet_id         = comet.id,
                perihelion_year  = year,
                visibility       = compute_visibility(comet, year),
                impact_check     = comet.is_impactor_candidate,
            )
            events.append(event)
            year += comet.orbital_period_years

    # Add random long-period comet arrivals
    for decade in range(2000, 2500, 10):
        if rng.chance(LONG_PERIOD_COMET_FREQUENCY_PER_DECADE):
            events.append(generate_random_longperiod_comet(decade, rng))

    return sorted(events, key=lambda e: e.perihelion_year)
```

**Comet events drive simulation consequences:**
- Visible comet (perihelion < 1.5 AU): cultural event; news cycle; public awareness bump; funding opportunities for space programs
- Very bright comet (perihelion < 0.5 AU): once-per-century scale; massive cultural moment; possible economic disruption from fear; historical record entry in Stage 10 if it occurred before game start
- Near-miss (MOID < 0.01 AU): planetary defense news cycle; real threat discussion; possible political response
- Impact: catastrophic; only possible for genuine impactor candidates; probability is very small per pass but non-zero across centuries of generational play

---

#### Comet Event Wiring to Simulation

The comet event calendar is generated at Phase 0 as a sorted `Vec<CometEvent>`. The simulation tick needs to fire these events at the correct in-game year. This is done via the `DeferredWorkQueue` — the same mechanism used for any future-scheduled simulation event (loan maturities, construction completions, political term expiries).

**`CometEvent` struct:**

```cpp
struct CometEvent {
    std::string  comet_id;           // references NamedComet in solar system data
    int32_t      perihelion_year;    // year this pass occurs; relative to game epoch (2000)
    float        peak_magnitude;     // visual magnitude at perihelion; lower = brighter
                                      // <0: naked eye, very bright; 0–3: naked eye; 3–6: binoculars
    float        moid_au;            // minimum orbit intersection distance; threat indicator
    bool         is_impactor_candidate;  // only true if trajectory intersects habitable zone
};
```

**Insertion into DeferredWorkQueue at simulation start:**

```python
def load_comet_events_into_queue(simulation, comet_calendar):
    """
    Called once at simulation initialisation, after world.json is loaded.
    Inserts all future comet events into the DeferredWorkQueue as SimulationEvent objects.
    Past events (perihelion_year < game_start_year) are skipped; they are in Stage 10 history.
    """
    for event in comet_calendar:
        if event.perihelion_year <= simulation.game_start_year:
            continue  // past events handled by Stage 10 ProvinceHistory; not simulation events
        
        trigger_tick = year_to_tick(event.perihelion_year, simulation.ticks_per_year)
        
        simulation.deferred_work_queue.push(
            trigger_tick = trigger_tick,
            event        = SimulationEvent(
                type     = SimulationEventType.CometPerihelion,
                payload  = event,
                priority = EventPriority.Medium,   // fires after economic tick, before news generation
            )
        )
    
    log.info(f"Loaded {len(future_events)} comet events into DeferredWorkQueue")
```

**`SimulationEventType.CometPerihelion` handler:**

```python
def handle_comet_perihelion(event: CometEvent, world_state):
    """
    Called by the simulation tick when a CometEvent fires from the DeferredWorkQueue.
    Generates appropriate news, cultural effects, and threat assessment.
    """
    
    # --- Threat assessment ---
    if event.is_impactor_candidate and event.moid_au < 0.01:
        # Near-miss or impact threat: generate planetary defense political event
        world_state.news_queue.push(NewsEvent(
            type        = NewsType.CometThreat,
            headline    = f"{get_comet_name(event.comet_id)} will pass within {event.moid_au:.3f} AU",
            geopolitical_trigger = True,   // spawns emergency discussion in UN-equivalent bodies
            funding_opportunity  = True,   // planetary defense budget proposals become viable
        ))
        
        # Actual impact: low probability draw at this point
        if event.is_impactor_candidate and rng.chance(IMPACT_PROBABILITY_PER_CLOSE_PASS):
            schedule_impact_event(event, world_state)   // EX-tier mechanic; V3+
            return
    
    # --- Visibility-based cultural effect ---
    if event.peak_magnitude < 0.0:
        # Extremely bright: Hale-Bopp or brighter; generational cultural event
        world_state.cultural_awareness_index += BRIGHT_COMET_CULTURAL_BONUS
        world_state.news_queue.push(NewsEvent(
            type     = NewsType.CometSpectacle,
            headline = f"{get_comet_name(event.comet_id)}: visible in daylight",
            scope    = NewsScope.Global,
        ))
        # Add to running encyclopedia entry for living-memory events
        world_state.encyclopedia_events.append(EncyclopediaEvent(
            type      = "CometSpectacle",
            year      = world_state.current_year,
            magnitude = event.peak_magnitude,
        ))
        
    elif event.peak_magnitude < 3.0:
        # Naked-eye comet: notable but not generational
        world_state.news_queue.push(NewsEvent(
            type  = NewsType.CometSpectacle,
            scope = NewsScope.Regional,
        ))
    
    # --- Space program funding signal ---
    if event.peak_magnitude < 2.0 or event.moid_au < 0.05:
        # Prominent comet increases public willingness to fund astronomy and space programs
        for faction in world_state.political_factions:
            if faction.policy_interest("space_program"):
                faction.polling_boost += COMET_SPACE_FUNDING_BOOST
```

**DeferredWorkQueue contract:**

The DeferredWorkQueue accepts any `SimulationEvent` with a `trigger_tick`. Events fire in tick-order; within the same tick, in `priority` order. The comet wiring requires no changes to the DeferredWorkQueue itself — `CometPerihelion` is just another `SimulationEventType` registered with the event dispatcher. This keeps the comet calendar mechanism consistent with all other deferred events in the simulation.

**Pre-game events:** Comet passes whose `perihelion_year` falls within the pre-game history window (game_start_year − 150 to game_start_year) are handled differently from simulation events. They are passed to Stage 10.2 history generation, where a very bright pass (magnitude < 1.0) or near-miss generates a `HistoricalEvent` of type `EnvironmentalDisaster` (sub-type: comet), with appropriate `lasting_effect` text ("The comet of {year} is still cited when people discuss the possibility of asteroid defence funding").

---

### 0.7 — Solar Weather

The star's activity class determines the frequency and severity of space weather events. Solar weather has caused infrastructure damage in recorded history since the telegraph era — the vulnerability is not to electronics specifically, but to **any networked long conductor**. Long copper lines act as antennas for geomagnetically induced currents. The damage scales with what infrastructure exists at the time, not with the era.

**Historical record establishing the baseline:**

| Event | Year | Peak Dst (nT) | Primary damage |
|---|---|---|---|
| Carrington Event | 1859 | est. −850 to −1,750 | Telegraph networks globally destroyed; fires in offices; operators shocked through disconnected lines; auroras at equatorial latitudes |
| Chapman-Silverman event | 1872 | est. comparable to Carrington | Telegraph disruption; limited records |
| Low-Latitude Aurora Storm | 1882 | ~−300 | Widespread telegraph disruption North America and Europe |
| New York Railroad Storm | 1921 | est. −900+ | Railroad signal and switching equipment fires; New York telephone exchange burned; submarine cable disruption |
| 1909 Storm | 1909 | ~−400 | Major telegraph disruption; trans-Atlantic cable disruption |
| Quebec Blackout | 1989 | −589 | Quebec power grid collapse (9-hour blackout); satellite drag; radio blackout |

The 1921 storm is particularly relevant: it struck an electrified railroad network and early telephone exchange — infrastructure that would be Era 1 or early Era 2 equivalent in the game. The pre-game history window (up to 150 years before game start) captures events from approximately 1850 onward, well within the documented active solar weather record.

```cpp
enum class SolarWeatherClass : uint8_t {
    Quiet,      // old G or K-type; low activity; rare major events
    Moderate,   // typical middle-aged G-type; solar cycle present; periodic major events
    Active,     // young G-type or active F-type; frequent moderate events
    Flaring,    // active K or M-type; intense frequent flares; high radiation environment
};

// Infrastructure vulnerability by type — determines what a solar event damages
// These are checked against what the affected province/nation actually has at event time
enum class ConductorInfraType : uint8_t {
    TelegraphNetwork,    // Era 1: long copper wire networks; highly vulnerable; low recovery cost
    TelephoneNetwork,    // Era 1–2: similar vulnerability to telegraph; urban concentration
    ElectrifiedRailroad, // Era 1–2: long conductive track; switching/signal equipment vulnerable
    PowerGrid,           // Era 2–3: long transmission lines; transformers most vulnerable component
                         // transformer replacement: weeks to months lead time
    CommunicationsCable, // Era 1–4: submarine and buried cables; significant induced current risk
    SatelliteSystem,     // Era 3–5: direct radiation + atmospheric drag; total loss possible
    SolidStateElectronics, // Era 3–5: unshielded circuits; vulnerable to direct EMP
};

struct SolarEventEffects {
    // Indexed by ConductorInfraType; applied when province has that infrastructure
    float  telegraph_disruption_prob;       // fraction of telegraph capacity lost per major event
    float  telephone_disruption_prob;
    float  railroad_signal_disruption_prob; // signal/switching damage; track itself unaffected
    float  power_grid_disruption_prob;      // fraction of grid capacity lost; transformer damage
    float  satellite_disruption_prob;       // fraction of satellites degraded or lost
    float  electronics_disruption_prob;     // unshielded solid-state equipment damage fraction
    float  recovery_time_ticks;             // baseline; scales with damage severity
};

struct SolarWeatherProfile {
    SolarWeatherClass   activity_class;
    float  solar_cycle_years;               // activity cycle duration; Sun = 11.0
    float  cme_frequency_per_year;          // average coronal mass ejections per year
    float  major_event_frequency_years;     // average years between Carrington-class events
                                            // Quiet: ~500 yr; Moderate: ~150 yr; Active: ~50 yr; Flaring: ~20 yr

    SolarEventEffects  minor_event;         // moderate storms; several per solar cycle maximum
    SolarEventEffects  major_event;         // Carrington-class; rare; affects all conductor types

    float  hardening_cost_multiplier;       // infrastructure cost to harden against events
                                            // applies per ConductorInfraType independently
                                            // hardened infrastructure ignores disruption_prob
};
```

**Disruption probabilities by event class (Moderate star, representative values):**

| Infrastructure type | Era available | Minor storm | Major (Carrington-class) | Recovery |
|---|---|---|---|---|
| Telegraph network | Era 1 | 30% disrupted | 85% destroyed | Days–weeks |
| Telephone network | Era 1 | 20% disrupted | 70% disrupted | Days–weeks |
| Electrified railroad signals | Era 1–2 | 15% disrupted | 60% disrupted; fire risk | Days |
| Power grid (long-haul) | Era 2 | 10% disrupted | 65% disrupted; transformer loss | Weeks–months |
| Submarine cables | Era 1+ | 5% disrupted | 40% disrupted | Weeks |
| Satellite systems | Era 3 | 20% degraded | 50% lost | Months–years |
| Unshielded electronics | Era 3 | 5% damaged | 35% damaged | Replacement |

**Power grid transformer damage** is the most economically severe outcome on a modern grid. Large high-voltage transformers are manufactured to order with lead times of 12–18 months. A Carrington-class event that damages multiple transformers simultaneously creates a cascade — no power, no pumping, no refrigeration, disrupted supply chains — that unfolds over months. The `recovery_time_ticks` field for power grid damage should be significantly higher than for telegraph damage.

**Solar weather events in gameplay:**

The event fires whenever the `major_event_frequency_years` probability triggers during the simulation. The system checks what conductor infrastructure the affected provinces have at the time of the event — not what era they are in — and applies the appropriate disruption probabilities. A nation that is still on telegraph in year 2020 (because it is underdeveloped or isolated) takes telegraph damage. A nation that hardened its grid after a previous event takes reduced damage.

```python
def apply_solar_event(event, all_provinces, world_state):
    for province in all_provinces:
        if not in_geomagnetic_storm_zone(province, event.intensity):
            continue  # high-latitude provinces more affected; equatorial less
        
        for infra_type in ConductorInfraType:
            if province.has_infrastructure(infra_type):
                if province.is_hardened(infra_type):
                    continue  # hardening investment pays off
                
                disruption = event.effects[infra_type].disruption_prob
                if random() < disruption:
                    province.apply_infrastructure_damage(infra_type, event.magnitude)
                    world_state.queue_news_event(SolarStormDamageEvent(province, infra_type))
```

**Carrington-class events in pre-game province history:**

The pre-game history window extends 150 years before game start (approximately 1850 onward), which overlaps substantially with documented solar weather history. Stage 10.2 generates `HistoricalEvent` records for any major solar event that falls within this window.

The event type is `EnvironmentalDisaster` with a sub-tag `solar_storm`. The `lasting_effect` field records what infrastructure was damaged and what the province did about it — whether it rebuilt identically (and remains vulnerable), invested in hardening (and explains anomalous infrastructure investment), or was so underdeveloped that telegraph disruption was merely inconvenient rather than catastrophic.

A province that experienced a 1920s-analog railroad storm may have unusually robust signal infrastructure relative to its development level — a historical curiosity that the player can discover and exploit (or that makes the province more resilient to solar events during play).

The pre-game window of 150 years gives the simulation approximately 1–2 expected Carrington-class events on a Moderate star to draw from, consistent with the real historical record (1859, 1921 are the two best candidates within that window).

---

### 0.8 — Home World Selection

The player's starting planet must satisfy minimum habitability criteria.

```python
def select_home_world(orbital_bodies, star):
    candidates = [
        body for body in orbital_bodies
        if body.type in {TerrestrialPlanet, SuperEarth}
        and body.is_in_habitable_zone
        and body.planetary_params.surface_pressure_kpa > 10.0
        and body.planetary_params.hydrology_mode == Active
    ]

    if not candidates:
        # Fallback: nearest body to habitable zone center; note in commentary
        # that conditions are marginal; adjust starting conditions accordingly
        candidates = [nearest_to_hz_center(orbital_bodies, star)]

    # Prefer Earth-mass; penalize tidal locking; penalize extreme eccentricity
    scored = [(score_habitability(b, star), b) for b in candidates]
    return max(scored, key=lambda x: x[0])[1]
```

For V1 (Earth-analog scenario), the home world is seeded deterministically: ~1.0 AU, ~1.0 Earth mass, G-type star, non-tidally-locked. The selection algorithm is bypassed. For EX procedural generation, the algorithm runs and the player gets whatever the system produced — which may be a super-Earth slightly inside the habitable zone with a thick atmosphere and 1.4g, or an Earth-mass planet at the outer edge with significant ice cover.

---

### 0.9 — Output: `solar_system.json`

```json
{
  "star": {
    "id": "star_001",
    "fictional_name": "Vardek",
    "spectral_type": "G",
    "luminosity_solar": 0.97,
    "age_gyr": 4.6,
    "hz_inner_au": 0.93,
    "hz_outer_au": 1.32,
    "frost_line_au": 2.66,
    "flare_activity_index": 0.12,
    "solar_cycle_years": 11.3
  },
  "orbital_bodies": [ ... ],
  "comet_events": [ ... ],
  "home_world_id": "planet_003",
  "solar_weather": { ... }
}
```

The `solar_constant_wm2` in each planet's `PlanetaryParameters` is derived from `luminosity_solar` and `semi_major_axis_au` rather than hardcoded — this is the fix to the existing `PlanetaryParameters` struct where the formula assumed a Sun-analog:

```
solar_constant_wm2 = (star.luminosity_solar × 1361.0) / (semi_major_axis_au²)
// Previously: 1361.0 / solar_distance_au²  — incorrect for non-Sun stars
```

---

### Solar System Gameplay Integration

| System | Tier | Notes |
|---|---|---|
| Phase 0 runs silently for V1 | V1 | Feeds `solar_constant_wm2`; seeds comet event calendar |
| Comet events in news/culture | V1 | Bright comets appear as news events; no interaction required |
| Solar weather — telegraph/telephone/railroad damage | V1 | Era 1 infrastructure vulnerable from game start; conductor type checked, not era |
| Solar weather — power grid transformer damage | V1–2 | Grid infrastructure triggers when player builds or inherits it; weeks–months recovery |
| Historical solar storm events in Province History | V1 | Pre-game window 150 years; 1–2 expected Carrington-class events; explains anomalous infra hardening |
| Solar weather — satellite disruption | EX | Era 3+ infrastructure; direct radiation + atmospheric drag |
| Solar weather — unshielded electronics damage | EX | Era 3+ solid-state equipment |
| Infrastructure hardening market | EX | Per-type hardening investment; cost multiplier from `SolarWeatherProfile` |
| Asteroid belt visible in system map | EX | Visual; statistical resource index shown |
| Named asteroid expeditions | V3+ | Robotic mission dispatched; resource payload returned after transit |
| Belt mining operations | V3+ | Continuous extraction; requires orbital station |
| Gas giant moon colonization | V3+ | Full WorldGenPipeline on target moon; extreme environment ops |
| Planetary transit logistics | V3+ | Multi-planet trade; window-gated; Tsiolkovsky cost |
| Impact threat response | V3+ | Planetary defense industry; deflection mission; political response |

---

### Solar System Generation Time

Phase 0 is entirely O(planet_count) and O(comet_population_size) — no raster computation, no spatial algorithms. It runs in under 2 seconds for any system configuration on any hardware.

The comet event calendar covers up to 500 years of simulated time (2000–2500). For a system with 20 named periodic comets and a standard long-period arrival rate, this generates at most a few hundred calendar entries. Negligible.

Phase 0 is not included in the per-body WorldGenPipeline timing tables — it is absorbed into pipeline startup overhead.

---

## Open Questions / Deferred Items

| Item | Description | Priority |
|---|---|---|
| Transit terrain cost config values | `terrain_type_multiplier[]` and `link_type_base_cost[]` arrays need tuning values in config | High |
| H3 orientation algorithm | Exact algorithm for steering pentagon cells to ocean during generation not yet specified | High |
| `groundwater_reserve` ratification | Fully specified in Stage 3; needs TDD struct alignment | Low |
| Desertification progression | Can climate stress shift a province's KoppenZone over time? Mechanic undefined | Medium |
| Volcanic eruption event arc | Destroy then fertilize; event system integration not specified | Medium |
| Karst cave network as facility location | `has_karst` flag exists; smuggling route mechanic not yet specified | Low (EX) |
| Offshore province model | Continental shelf and deep sea provinces not addressed; needed for offshore drilling | Low (EX) |
| Water scarcity / irrigation cost mechanic | `has_artesian_spring` and `spring_flow_index` now seed arid province water access; irrigation cost formula still unspecified | Medium |
| Canyon chokepoint transit | `has_canyon_system` flag set; transit cost formula uses ProvinceLink but pass routing not specified | Medium |
| GIS pipeline H3 adoption | Spatially joining Natural Earth province centroids to H3 res-4 cells at GIS pipeline time; not yet specified in World Map doc | High |
| `surface_radiation_sv_yr` integration with health system | Field defined on Province; health mechanic for exposed workers not yet specified in TDD | Low (EX) |
| Paleo-hydrology channel detection algorithm | `HydrologyMode::PalaeoActive` behavior described; algorithm not specified | Low (EX) |
| Impact cratering pass for airless bodies | Airless body terrain is crater-dominated; province terrain_type classification needs variant | Low (EX) |
| `historical_trauma_index` wiring to political simulation | Field defined with formula and downstream modifiers; TDD political NPC behaviour does not yet read it | Medium |
| Pre-game NPC memory seeding mechanism | `PreGameEvent.has_living_witnesses` seeds NPC memory entries; tick step not yet specified | Medium |
| Tidally locked planet settlement pattern | `is_tidally_locked` flag defined; Stage 9 formula incorrect for terminator-band habitability | Low (EX) |
| Subsurface ocean moon WorldGenPipeline | `has_subsurface_ocean` flag defined; no pipeline stages for ice-shell body type | Low (EX) |
| Binary star system planet placement | `Star.is_binary` flag defined; Titius-Bode algorithm assumes single star | Low (EX) |
| Planetary defense response mechanic | `NamedComet.is_impactor_candidate` flag exists; impact handler noted as V3+ in comet wiring | Low (V3+) |
| NomadGroup mobile agent routing | V1 uses static `nomadic_population_fraction`; seasonal migration routing deferred to EX | Low (EX) |
| Thermohaline circulation / AMOC analog | Surface currents modelled; deep ocean conveyor not distinguished; AMOC weakening unspecified | Low (EX) |
| `world_encyclopedia.json` access performance | Schema specified; query performance for 84K-province worlds at UI layer not yet profiled | Low |

---

## Config File Structure

All tunable constants referenced throughout this document are assigned to named config files. Inline literals are an architectural smell — any value that affects balance, feel, or physics should be adjustable without recompilation. The Bootstrapper reads these files at generation time.

### `config/world_generation.json` — Core pipeline constants

```json
{
  "stage_2": {
    "MOUNTAIN_LIFT_COEFFICIENT":         0.80,
    "PRECIPITATION_EFFICIENCY":          0.65
  },
  "stage_3": {
    "SNOWPACK_RETENTION_FACTOR":         0.70,
    "MELT_DECAY_KM":                   400.0,
    "SNOWMELT_TO_RIVER_ACCESS_SCALE":    0.60,
    "ARTESIAN_FLOW_SCALE":               0.50,
    "LARGE_RIVER_THRESHOLD":          50000.0,
    "TIDAL_MIXING_THRESHOLD":            0.35,
    "LOW_TIDE_THRESHOLD":                0.20,
    "COASTAL_SAND_SCALE":                0.004,
    "RIVER_SAND_SCALE":                  0.30
  },
  "stage_4": {
    "MONSOON_FLOOD_BONUS":               0.25,
    "MONSOON_VARIANCE_PENALTY":          0.35,
    "EARTH_TROPICAL_OCEAN_KM2":    70000000.0,
    "warm_current_delta":                4.5,
    "cold_current_delta":                3.0,
    "cold_current_precip_suppression":   0.30,
    "cold_current_fishery_bonus":        0.35
  },
  "stage_8": {
    "QUANTITY_SCALE_FACTOR":             1.0
  }
}
```

### `config/resource_deposits.json` — Deposit quantity tables

Contains `DEPOSIT_MEAN`, `MINIMUM_VIABLE_DEPOSIT`, and `ERA_LOCKS`. See §8.4 tables for full values. JSON structure:

```json
{
  "deposit_mean": {
    "IronOre":  { "CratonInterior": 2.5, "SedimBasin": 1.2, "Subduction": 0.8, ... },
    "Copper":   { "Subduction": 2.0, "HotSpot": 1.2, "ContCollision": 0.8, ... }
  },
  "minimum_viable_deposit": {
    "IronOre": 0.10, "Copper": 0.08, "Gold": 0.03, "Uranium": 0.05,
    "Lithium": 0.05, "Diamonds": 0.02, "Coal": 0.15, "CrudeOil": 0.10
  },
  "era_locks": {
    "IronOre": 1, "Coal": 1, "CrudeOil": 1, "NaturalGas": 1,
    "Uranium": 2, "RareEarths": 2, "SolarPotential": 2, "WindPotential": 2,
    "Lithium": 3, "PlatinumGroupMetals": 3
  }
}
```

### `config/simulation.json` — Runtime simulation constants

```json
{
  "irrigation": {
    "SALINISATION_RATE_PER_TICK":        0.02,
    "SALINISATION_DAMAGE_THRESHOLD":     0.40,
    "AQUIFER_CRITICAL_THRESHOLD":        0.15
  },
  "archetype": {
    "NOTABLE_THRESHOLD":                 0.60,
    "MINIMAL_THRESHOLD":                 0.25
  }
}
```

### `config/comet_events.json` — Phase 0 comet calendar constants

```json
{
  "LONG_PERIOD_COMET_FREQUENCY_PER_DECADE": 0.15,
  "BRIGHT_COMET_CULTURAL_BONUS":            0.08,
  "COMET_SPACE_FUNDING_BOOST":              0.12,
  "IMPACT_PROBABILITY_PER_CLOSE_PASS":      0.003
}
```

All config files are loaded once at pipeline init before any stage runs. Constants referenced in code throughout this document by their `ALL_CAPS` names resolve to values in these files. No tunable numeric literal appears inline in engine code — if a number needs changing for balance or scenario tuning, it changes in JSON, not in source.

---

## Gap Taxonomy vs. Existing Spec

| Gap | Classification | Status |
|---|---|---|
| `geology_type` field missing from Province struct | [BLOCKER] | Resolved: `GeologyType` enum defined in Stage 1 |
| Viable crop type table not specified | [BLOCKER] | Resolved: specified in Stage 6; stored in crop_viability.csv |
| `TerrainType` enum not defined | [PRECISION] | Resolved: defined in Stage 2 |
| `precipitation_seasonality` not wired to drought probability | [PRECISION] | Partially resolved: monsoon modifier in Stage 4; per-tick drought formula still TBD |
| `geographic_vulnerability` is prose table, not numeric derivation | [PRECISION] | Resolved: derivation rule specified in Stage 4 |
| Island isolation / maritime-only adjacency not encoded | [PRECISION] | Resolved: `ProvinceLink.LinkType` makes maritime links first-class; `island_isolation` flag on GeographyProfile |
| Transit terrain multiplier between province pairs | [SCOPE] | Resolved: `ProvinceLink.transit_terrain_cost` field; formula specified; config values TBD |
| Mountain pass chokepoint mechanic | [SCOPE] | Resolved: `is_mountain_pass` flag; pass links get reduced terrain cost in Stage 2 |
| Water scarcity mechanic for arid zones | [SCOPE] | Resolved: `irrigation_potential`, `irrigation_cost_index`, `salinisation_risk` fields; full formula in Stage 5 |
| Desertification progression | [SCOPE] | Open — deferred |
| `Province.adjacent_province_ids` breaks for islands | [PRECISION] | Resolved: replaced by `std::vector<ProvinceLink> links` |
| No hierarchy between province and nation | [PRECISION] | Resolved: H3 parent/child chain provides Province→Region→Nation hierarchy from index |
| GIS pipeline uses arbitrary uint32_t province IDs | [INCONSISTENCY] | Open: GIS pipeline must adopt H3 indexing to match procedural pipeline output format |
| Pipeline hardcodes Earth-specific constants | [BLOCKER] | Resolved: `PlanetaryParameters` struct; all constants read from struct; gravity-adjusted formulas in Stages 2 and 4 |
| No aviation viability model for non-Earth atmospheric density | [PRECISION] | Resolved: `AviationProfile` derived from `PlanetaryParameters`; stored at world load; read by transit system |
| No end-date constraint documented | [INCONSISTENCY] | Resolved: Status and Scope section corrected; GDD §19 "no forced end state" is authoritative |

---

## Change Log

**v0.18 (Pass 5 Session 4)**
- Added "H3 Pentagon Handling [CORE]" section in Globe Discretization; specifies detection, treatment, and determinism of H3's 12 fixed pentagon cells
- Documents pentagon adjacency (5 neighbors instead of 6), area calculation, coastline generation, and neighbor interpolation
- Specifies preprocessing step for pentagon detection and logging
- Adds fields to SimCell: `is_pentagon`, `is_pentagon_ocean`, `is_pentagon_land`, `neighbor_count`
- Clarifies that H3 library functions handle pentagon cases transparently
- Area calculations use `h3_cell_area_km2()` for correctness
- Updated companion document references (GDD v1.7, TDD v29, World Map & Geography v1.2)
- Resolves RWA-B4 blocker

**v0.3**
- Corrected Status and Scope: removed incorrect "2000–2025 on a single planet" framing; game has no forced end state (GDD §19); Era 5 is open-ended
- Added `PlanetaryParameters` struct with all pipeline-driving constants
- Added `HydrologyMode` enum: Active / PalaeoActive / Cryogenic / None; controls Stage 3 behavior
- Added `BodyType` enum: Terrestrial / IcyMoon / Asteroid / SpaceStation
- Added `AviationProfile` struct; derived from planetary density; read by transit system for air freight viability
- Specified gravity-adjusted erosion formula in Stage 2 (scales as g^0.5)
- Specified gravity-adjusted orographic lift formula in Stage 4
- Specified Coriolis factor from rotation period; affects prevailing wind strength
- Added `surface_radiation_sv_yr` field on Province; derived from magnetic field strength and altitude
- Added `RocketTransitProfile` as V3+ architecture placeholder; Tsiolkovsky equation documented
- Added `CelestialBody` struct as V3+ architecture placeholder; multi-body LOD rules specified
- Added generation time estimates table for Earth / Mars / Super-Earth on CPU and GPU paths
- Added `raster_resolution_override` to scenario file parameters; res-6 override for large-world generation
- Updated Stage 10 scenario file to include `planetary_bodies` array and `planetary_body` reference
- Updated open questions: 3 new items added (radiation health integration, paleo-hydrology algorithm, impact cratering)
- Updated gap taxonomy: 3 new gaps resolved; 1 inconsistency corrected

**v0.2**
- Added Globe Discretization section: H3 hierarchical hexagonal grid decision and rationale
- Specified H3 resolution stack (res 2–9); res 9 as fine raster for generation; res 4 as base simulation province unit
- Defined `SimCell` struct: `Province` with `H3Index` replacing `uint32_t id`
- Defined `ProvinceLink` struct: replaces flat `adjacent_province_ids`; maritime links first-class; `transit_terrain_cost` field
- Defined `LinkType` enum: Land | Maritime | River
- Specified dual-scale architecture: res-9 raster for pipeline physics; res-4 province graph for runtime
- Specified pentagon handling strategy: ocean placement via H3 orientation; transparent fallback for any land pentagons
- Added `is_mountain_pass` flag to Stage 2 derived fields
- Specified transit cost formula using ProvinceLink fields
- Resolved maritime adjacency [PRECISION] gap
- Resolved transit terrain multiplier [SCOPE] gap
- Flagged GIS pipeline H3 adoption as open item

**v0.1**
- Initial design draft; not yet ratified against TDD v16
- Defines 10-stage pipeline architecture
- Specifies TectonicContext, TerrainType, AridType, SoilType, GeologyType, PermafrostType enums
- Specifies resource probability table by TectonicContext
- Specifies KoppenZone → viable crop table (resolves GIS pipeline [BLOCKER])
- Specifies geographic_vulnerability derivation rule (resolves [PRECISION] gap)
- Defines geology_type enum and lookup path (resolves deposit grade [BLOCKER])
- Identifies 9 open questions for future iteration

---

*EconLife — Procedural World Generation v0.18*
*11-stage pipeline: tectonics → erosion → hydrology → atmosphere → soil → biomes → features → resources → population → commentary → export*
*Companion document to GDD v1.7, TDD v29, World Map & Geography v1.2, R&D & Technology v2.3*
