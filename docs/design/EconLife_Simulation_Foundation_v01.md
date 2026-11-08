# EconLife — Simulation Foundation: Layers, Contracts & Clocks (v01)

*Status: **adopted decisions + proposed contracts** (2026-10-08).*
*Companion to GDD v1.7, TDD v29, WorldGen v0.18, Feature Tier List, Mechanical History Generation Plan, World Spectrum & Evolution Plan, Logistics & Political Scale v01.*
*Origin: the vision analysis `EconLife_Vision_Foundation_Analysis_2026-10-08` (project report) and Christian's answers to its decisions V1–V6, D5 and D8.*

---

## 0. The vision, in one paragraph

EconLife's foundation is a simulation of everything the economy stands on: a star and its planet, gravity and the speed of light, plates that build mountains, erosion that wears them down, water and climate, soils, the life that evolved on it, and the resources that geology and biology actually put where they are. The human world (people, knowledge, economy, polities, war) runs on top of that substrate, and every layer is the cause of the next. Reference points: Dwarf Fortress, Songs of Syx, Cataclysm: DDA.

**Foundation fidelity is V1. Play scope can wait.** (decision V1)

---

## 1. Decision record (2026-10-08, settled)

| ID | Question | Decision | Consequence for docs/code |
|---|---|---|---|
| V1 | What does "simulate everything" mean for V1? | **Build the foundation properly first; play scope can wait.** | Mechanical history is re-tiered V1 (foundation). Playable pre-modern entry is EX. The play start stays January 2000. |
| V2 | What is "evolution"? | **Biota generator now (species pool + traits + biosphere timeline); the contract must allow a real speciation simulation later.** | §2 L4 contract. Speciation simulation = EX. |
| V3 | Planet scale | **Coarse physical generation of the whole globe; the LOD 0 window (6 provinces) is refined from it.** | WorldGen Stages 1–8 run planet-wide on H3 res 2–3. |
| V4 | Speed of light | **Information travels with its carrier in every era; `c` is the floor.** | Supersedes "information is instant". §4. |
| V5 | Era numbering | **Docs follow `eras.csv` (Era 8 = 2000); prefer `era_key`. Explore alternatives to the era concept.** | Renumbered R&D, Commodities, WorldGen and Tier List (+7). Alternatives: §5. |
| V6 | Real Earth or generated world | **Procedural world is the V1 default; a GIS Earth is a later scenario.** | GDD §2 and World Map Conflict 2 updated. |
| V7 | How the world is shown | **The planet is a globe. Zooming in moves down the H3 hierarchy, from the whole planet to regions, provinces and cities.** | Each zoom level is an H3 resolution (planet res 0–3, province res 4, settlement res 6–9, streets res 10+). Finer data exists only where the simulation has refined the cell (the LOD window, V3); a coarser view shows the conserved aggregate of its children, never a separately stored value. Needs B1 (every cell has a latitude/longitude). Rendering the globe is UI work after B2. |
| V8 | What sets a place's climate | **Latitude is only the sunlight. Temperature and climate zones follow from the land and sea around a place, ocean currents, prevailing winds, elevation and mountain barriers.** | Climate is computed on the planet-wide grid (B7), after land/sea (B2), plates (B3) and erosion (B5) exist. The archetype temperature blend and the dice-rolled ocean currents in today's Stage 4 are removed, not tuned. |
| D5 | AGI, fusion, BCI | **Not cut. They come once the simulation is mature and enable travel to other planets and beyond.** | Tier List: new "Deep Future" section replaces the Cut List entry. |
| D8 | World Class | **A classification of the generated world; later a player-facing choice of starting planet.** | World Class is derived (§2), presets choose planet/biota parameters (EX for the player UI). |

---

## 2. Layers and their contracts

**Rules.**
- R-1: During generation, layer N reads only the outputs of layers < N. No layer reads a later layer, and no economic label seeds a physical field.
- R-2: Feedback from a higher layer to a lower one exists only at runtime, only where it is physically real (extraction depletes deposits, clearing changes soil, emissions change climate), and only through deltas with conservation.
- R-3: Each physical quantity has exactly one source of truth.
- R-4: Coarse everywhere, fine where the player is. The whole planet is generated coarsely; the LOD 0 window is refined from it.
- R-5: The No-Rails Rule applies to world generation. A field with no mechanism yet is set to a neutral zero and listed as a gap. It is never set to a fitted outcome (no modern cohorts in a Neolithic world, no random `historical_trauma_index`).

```
L0 Cosmos          once          → StellarContext
L1 Planet body     once          → PlanetaryParameters (single source of truth)
L2 Lithosphere     once, global  → plates, rock, stress, deposits
L3 Surface         once, global  → erosion, climate, hydrology, soils   (runtime: climate, soil)
L4 Biosphere       once, global  → biota pool, biosphere timeline, NPP   (runtime: stocks)
   ── derived: WorldClass = f(L1..L4) ──
L5 Human biology   history+play  → cohorts, disease, nutrition, hardiness
L6 Society         history+play  → knowledge, economy, polities, links with latency
   ── derived: province archetype label = f(L2..L6) ──
L7 Agents          play          → NPCs, player (LOD 0 window only)
```

### L0 Cosmos (parameters are V1; a playable solar system is Deep Future)

```cpp
struct StellarContext {               // produced by WorldGen Phase 0
    float luminosity_solar;           // L/L_sun
    float insolation_wm2;             // at the home planet's orbit; Earth = 1361
    float year_length_days;           // orbital period, in home-planet days
    uint8_t moon_count;
    float tidal_amplitude_m;          // from moon mass/distance + solar term; Earth ≈ 0.5 open ocean
    float impact_rate_per_myr;        // drives Stage 7 craters and the comet calendar
    float solar_activity_index;       // CME/flare frequency (WorldGen §0.7)
};
```

### L1 Planet body (V1)

`PlanetaryParameters` (`simulation/core/world_state/geography.h`) is the **only** place that holds surface gravity, radius, atmosphere, rotation, axial tilt, magnetic field and age. Everything else derives from it.

| Derived field | Formula / source | Replaces |
|---|---|---|
| `hazard_settings.gravity_g` | `surface_gravity_ms2 / 9.81` | free dial in `world_class.h` |
| `hazard_settings.radiation` | from `magnetic_field_strength` and atmospheric column (WorldGen §Planetary, `surface_radiation_sv_yr`) | free dial |
| `hazard_settings.atmosphere` | from pressure/composition vs. human tolerance | free dial |
| `hazard_settings.geology` | from `mantle_heat_flux` and the share of active-boundary cells | free dial |

### L2 Lithosphere (V1, planet-wide)

- **Plates carry a velocity.** Boundary type comes from the **relative motion** at the boundary (converging → subduction or collision by density; diverging → rift or spreading; parallel → transform), not from age and chance.
- **Deposits are seeded from data-driven deposit types**, not a hard-coded `ResourceType` enum. File: `packages/base_game/geology/deposit_types.csv` (proposed).

```
deposit_type_key, tectonic_contexts, host_rocks, min_plate_age_gyr, requires_biosphere,
element_1, grade_1, element_2, grade_2, ..., byproducts, depth_dist, size_dist
# e.g. porphyry_cu_mo_au, vms_zn_pb_cu_ag, bif_fe, placer_au_sn, evaporite_k_na,
#      laterite_ni_co, greisen_sn_w, sedimentary_coal (requires_biosphere=land_plants),
#      petroleum_system (requires_biosphere=marine_life)
```

A deposit then states the **elements** it contains, with grade and by-products. That is the "grunnstoffer" layer: real ore geology, not a chemistry simulation.

### L3 Surface (V1, planet-wide)

Order: **Stage 2 erosion → Stage 4 atmosphere → Stage 3 hydrology → Stage 5 soils.** Hydrology needs precipitation; the code already runs atmosphere first.
- Erosion uses a stream-power law, scaled by gravity per WorldGen §Planetary. It produces valleys, floodplains, deltas, alluvial fans and placer concentration.
- Latitude and longitude are read from the H3 cell, never drawn.
- **Climate (V8).** Latitude sets only top-of-atmosphere sunlight (from the star, orbit and axial tilt in L0/L1). Everything else is a mechanism on the global grid:
  - *Land and sea:* the heat capacity of water damps the seasons; continentality is the real distance to the coast along the grid, not a proxy from coastline length.
  - *Prevailing winds:* the circulation cells (Hadley, Ferrel, polar) follow from rotation rate and the equator-to-pole contrast, and carry heat and moisture across the grid.
  - *Ocean currents:* surface gyres are driven by those winds and bent by the continents' shapes. That gives warm western-boundary currents (Gulf Stream, Kuroshio) and cold eastern-boundary upwelling (Humboldt, Benguela). A coast gets its current from where it sits in its ocean basin, never from a dice roll.
  - *Elevation:* temperature falls with the lapse rate; mountains force air up, so the windward side gets the rain and the leeward side lies in rain shadow. Moisture is carried and spent along the wind, so it is conserved.
  - *Climate zones* (Köppen) are read off the resulting monthly temperature and precipitation, never assigned.
  - *Scale-back (documented):* an annual-mean energy balance with prescribed circulation belts and a steady-state gyre per basin, not a general circulation model. It is good enough to place Norway's ice-free fjords and Atacama's desert, and can be replaced later.

### L4 Biosphere (V1 as a generator; EX as a simulator)

```cpp
struct BiosphereTimeline {           // when life did what on this planet
    float life_origin_gyr_ago;
    float oxygenation_gyr_ago;       // gates banded iron formations, oxidised soils
    float land_plants_gyr_ago;       // gates coal (lignin) and terrestrial soils
    float marine_life_gyr_ago;       // gates petroleum systems, limestone, phosphate
};
struct Taxon {                       // one entry in a biome's species pool
    std::string key;                 // generated, data-driven
    TaxonKind kind;                  // plant | animal | microbe
    bool domesticable;               // crop or livestock candidate
    float pathogen_reservoir;        // 0..1 contribution to disease load
    float predator_pressure;         // 0..1
    float yield_per_km2;             // food/fibre/timber potential, real units
    std::vector<std::string> products; // goods it can become
};
struct BiomeBiota { std::vector<Taxon> pool; float npp_gC_m2_yr; };
```

- `hazard_settings.disease` and `hazard_settings.predators` **derive** from the pool, as cell-weighted sums of `pathogen_reservoir` and `predator_pressure`.
- Agriculture becomes possible where domesticable taxa exist. Crops stop being `ResourceDeposit`s.
- **Replaceability (V2):** a future speciation simulation must produce the same `BiosphereTimeline` + `BiomeBiota`. Consumers never know which one ran.

### WorldClass (derived, D8)

`world_class()` (`simulation/core/world_gen/world_class.h`) keeps its scale (Garden 1–3 … Extreme 14+, Earth ≈ 12) but takes its inputs from L1–L4 instead of free settings. **Presets** (garden, earthlike, deathworld, …) become sets of planetary and biota parameters. Exposing that choice to the player is EX.

### L5–L7

These already exist (cohorts, nutrition/health/schooling, hardiness, knowledge per province, grain logistics, polity stress, war, NPCs, player). This document adds only §4 (latency and physical time), §5 (era) and §6.1 (units).

---

## 3. The world.json contract

Both world sources (procedural V1, GIS later) produce the same output: per H3 cell, the L1–L4 fields above. The founding seed (minimal population) and the mechanical-history run then build L5–L6 on it. A GIS world fills L2–L4 from data instead of from generation and runs the same history.

---

## 4. Information latency (V4)

**Rule:** information moves with a carrier. Its delay over a link is the link's length divided by the fastest communication technique the sending society holds and can use on that link, and never less than `length / c`.

```cpp
struct LinkTransit {                  // extends ProvinceLink (same link grain_logistics uses)
    float mass_cost_per_tonne_km;     // existing: what hauling costs
    float info_latency_days;          // NEW: length_km / comm_speed_km_per_day(technique, link_type)
};
// comm_speed by technique (data-driven, real units): runner ~50 km/d, mounted relay ~150-300 km/d,
// sail (wind-dependent), optical telegraph, electric telegraph ≈ minutes, radio/network ≈ c.
```

**What reads it:** orders between a polity's centre and its provinces (control radius, Logistics §0), price information between markets (arbitrage lags), news and evidence propagation (media, investigators), and diplomacy and war.

**[SCENARIO]** *When* an order is sent between provinces 300 km apart by a society whose fastest technique is a mounted relay at 200 km/day, *then* it arrives no earlier than 1.5 days later.
**[SCENARIO]** *When* both ends hold an electric telegraph on the link, *then* latency ≤ 1 tick.

### 4.1 Four times, kept apart (R1 F-03)

A message carries physical time. The integrator only decides *when the simulation looks at it*. Those are different quantities and must never overwrite each other.

| Name | Meaning | Type, unit | Set by |
|---|---|---|---|
| `physical_event_time` (E) | the instant the sender committed the message | `int64` seconds | the sending mechanism |
| `physical_arrival_time` (A) | the instant the carrier delivers it | `int64` seconds, `A = E + ceil(latency)` | the link, at send time, once |
| `simulation_processing_time` (P) | the orchestrator step in which the receiver first sees it | `uint32` tick (the step's `current_tick`) | the deferred-work drain |
| `actor_action_time` (R) | the instant stamped on anything the receiver does about it | `int64` seconds | the receiving mechanism |

**Axis.** Physical time is integer seconds on one axis per world, with 0 at the start of tick 0. Tick `t` covers `[t·86 400, (t+1)·86 400)`. Years are 365 days (`kTicksPerYear`), matching the calendar in code. `int64` seconds spans ±2.9·10¹¹ years, which covers Deep Time without a second representation.

**Units and rounding.**
- Latency is computed in `double` from the link (km, km/day or m/s) and converted to seconds with **ceil**, never round or truncate. The `c` floor is a lower bound, so rounding may only push arrival later. 0.2 days becomes exactly 17 280 s.
- A message between two distinct actors always has a latency of at least 1 s after rounding, because the distance or handling time is greater than zero. Consequently `A > E` holds for every message.
- `P` is the first step whose `current_tick ≥ due_tick`, where `due_tick = ceil(A / 86 400)` in ticks. This is the existing `DeferredWorkItem::due_tick` drain rule (`due_tick <= current_tick`, `drain_deferred_work.cpp`). The payload keeps `A`, and `due_tick` is derived from it, never the other way round.

**Invariant.** `E < A ≤ P_seconds` and `R ≥ A`. Here `P_seconds = P·86 400` is the instant the processing step represents.

### 4.2 When the receiver can react

The receiver can act on a message in step `P` and in no earlier step. Its reaction is a new event with `E' = R`, where `R = max(A, P_seconds)`. That reaction is then subject to its own latency, so `A' = R + ceil(latency')`. A reaction is never back-dated into a step the integrator has already closed, because those effects are already applied.

What this guarantees **at every stride**:
1. `A` is identical whatever the stride, since it is computed once at send time from `E` and the link.
2. Nothing reacts before `A`.
3. The reaction lag `R − A` is less than one step (one day in Play, up to 31 days in History at 12 steps/year).
4. Messages processed in the same step are handled in the order of §4.3.

What it does **not** guarantee: identical downstream outcomes at different strides. A coarser stride quantises reactions to its step, so a reply sent at day 12.2 at stride 365 is sent at day 30 or 31 at stride 12. Mechanisms that integrate over time may use `A` to apportion the step (for example, an exposure accrues from `A` to the step's end, not for the whole step). That is a per-mechanism choice and must be stated by the mechanism, not assumed by the integrator. The stride warning in CLAUDE.md ("Integration stride") applies: a measurement taken at a coarse stride measures the integration as well as the model.

### 4.3 Ordering and tie-breaking

The order in which pending messages are processed is a **total order** on the key

`(A, E, sender_id, receiver_id, kind, sender_seq)`

Each part of the key:
- `kind` is the message type's stable id.
- `sender_seq` is a per-sender `uint64` counter, incremented at each send.

Each sender is processed by exactly one thread in a tick (province-parallel dispatch), so `sender_seq` is deterministic without a global lock. The key never depends on container iteration order, heap layout, pointer values or thread scheduling. This is the same principle `DeferredWorkComparator` already applies to `(due_tick, type, subject_id, payload)`. Messages extend that comparator; they do not get a second queue.

**Causal order holds without a vector clock.** A reply to message M is created no earlier than `A_M`, and has latency of at least 1 s. So its own arrival satisfies `A' > A_M` and it sorts after M under the key.

### 4.4 Per clock

- **Deep Time** (world generation, L0–L4). There are no actors and no messages. Process time is geological and is stated in years (`int64`) or Myr (`double`) by each generator. Nothing from Deep Time enters the message queue.
- **History** (12 steps/year, `current_tick` advances 30 or 31 ticks per step; see `society_evolution_harness.h`). The sender's `E` is the instant of the step that sent it (`current_tick·86 400`). This is the coarse integrator's resolution for *event* times, and it is documented as such. `A` is exact from there on. A message is processed at the first step at or after `A`.
- **Play** (1 tick = 1 day). Same rules with a step of one day. `E` may carry sub-day precision when the sending mechanism has it (a scheduled departure time, for example). Otherwise it is the start of the tick.

### 4.5 History → Play transition, and save/load

- The tick axis is continuous from History into Play. If a rebase is ever introduced, it shifts `E`, `A`, `R` and `due_tick` by the same integer offset and changes nothing else.
- Pending messages cross the transition unchanged, with their `E`, `A` and `sender_seq`. They are not re-stamped to the transition instant.
- A save writes, for each pending message, `E`, `A`, sender, receiver, kind, `sender_seq` and the payload, in key order. It also writes each sender's next `sender_seq`. `due_tick` is derived on load. Loading therefore reproduces the same drain order as an uninterrupted run. This matches the existing save rule for `DeferredWorkQueue` (`persistence_module.cpp`, `write_deferred_work_queue`).

### 4.6 Scenarios

**[SCENARIO] Sub-day latency survives a coarse stride.**
- *Given* a message sent at `E = day 10 + 0` on a link with 0.2 days of latency, *then* `A = 10·86 400 + 17 280` s at every stride.
- At stride 365 it is processed on tick 11 (`due_tick = ceil(10.2) = 11`), and a reply has `E' = 11·86 400`.
- At stride 12 it is processed at the first step with `current_tick ≥ 11`, and a reply has `E' =` that step's instant.
- In both cases the stored `A` is the same, and no reaction is stamped before `A`.

**[SCENARIO] Identical arrival timestamps.** *Given* two messages with the same `A` to one receiver from different senders, *then* they are processed in `(E, sender_id, …)` order. The order is identical whether they were enqueued in either order, and identical after a save/load between enqueue and processing.

**[SCENARIO] History → Play.** *Given* a message sent in the last History step with `A` three days into Play, *then* it is processed on Play tick `ceil(A / 86 400)`, carrying its original `E` and `A`.

**[SCENARIO] Save/load of pending information.** *Given* a world with pending messages that is saved and reloaded, *then* the sequence of `(A, receiver, kind)` processed over the next N ticks is identical to the uninterrupted run, and the next `sender_seq` for every sender is identical.

**[SCENARIO] Causal order independent of container order.** *Given* the same messages inserted into the pending set in a shuffled order (shuffled by a seeded `DeterministicRNG`, several seeds), *then* the processing sequence is identical for all permutations. A reply always processes after the message that caused it.

*This section is a contract only (R1 F-03). No scheduling infrastructure is implemented under it. The first mechanism that sends messages (B5 polity orders, or C1 price information) implements it, and implements these scenarios as tests.*

---

## 5. Alternatives to "era" (V5 follow-up, proposal)

### What era does today (measured in code, 2026-10-08)

1. **Gate content.** `era_available` on goods, recipes and deposits is compared with **one global** `state.technology.current_era` (`production_module.cpp:247,336,487`).
2. **Set the economic regime** (`eras.csv: economic_regime` → subsistence, barter, coinage …).
3. **Label the climb.** The world era is the **frontier province's** position on the technology wheel (`technology_module.cpp:77,138`).

**The defect:** knowledge is per province (Realism Roadmap R6), but the gate is global. A province that has never worked bronze can run bronze recipes once the frontier province turns the era, and a backward province gets the frontier's whole recipe book. The content data has the same flaw: `copper_ore` and `tin_ore` are `era_available = 8`, so no one anywhere can have a Bronze Age.

### Options

| Option | What it is | Verdict |
|---|---|---|
| A. Global era gate (today) | One number gates everyone | Wrong once polities diverge (a rail in another dress: "it rises with the era"). |
| B. Per-polity era | Each polity has its own era index and gates its own content | Fixes the global leak. Still bundles all of an era's content together. |
| **C. Technique gating** *(recommended)* | Every good/recipe/facility names the technology node(s) it needs (`required_nodes`). A province can use it when it **knows** the node and **has the means** (the wheel's existing know × means test). "Era" becomes a **derived label** per polity, a historian's name for where it sits on the wheel. The regime is derived from institutions present (money, markets, wage labour), not from the era row. | Matches the wheel already in the code (502 nodes, 5 spokes), removes the last calendar-shaped gate, and lets societies mix (Song China ≠ Britain). Recipes already carry `key_technology_node`. |
| D. Material/energy ages | The label is derived from the dominant material and energy source in use (stone, bronze, iron, wood, coal, oil, nuclear …) | Good **display** for C, not a mechanism. |

### Recommendation

Use **C as the mechanism and D as the display**.
- `eras.csv` stays as the calibration ladder and the label table.
- `era_available` is replaced by `required_nodes` in the goods, recipes and deposit-types CSVs.
- `economic_regime` is derived.
- Migration: (1) add `required_nodes` and keep `era_available` as a fallback; (2) switch the gates in `production` to per-province know × means; (3) remove `era_available` once every content row has nodes; (4) correct pre-modern content (copper, tin and bronze in the bronze spoke …).

**[SCENARIO]** *When* province A knows `bronze_smelting` and has the means, and neighbour B does not, *then* A can run the bronze recipe and B cannot, in the same tick.

---

## 6. Clocks

| Clock | Step | Runs | Note |
|---|---|---|---|
| Deep time | once at world creation | L0–L4 generation | Planet-wide, coarse |
| History | 12 orchestrator steps / year (CLAUDE.md "Integration stride") | L5–L6, planet at LOD 2 | Founding seed → entry date |
| Play | 1 tick = 1 day | L5–L7 in the LOD 0 window, plus L3/L4 runtime feedback | Snapshot from history via persistence |

Every process states its clock. A process that runs per tick must be correct at both the history and the play stride ("a per-tick rate is only correct if every tick runs"). Physical times inside a clock follow §4.1.

### 6.1 Units and precision (R1 F-09)

There is no units framework. Each quantity uses one unit, named in the field's suffix, and conversion happens only at the boundaries listed here. New fields follow the convention already in the code.

| Quantity | Unit and type | Existing examples | Notes |
|---|---|---|---|
| Simulation time | tick = 1 day, `uint32 current_tick`; 365-day year (`kTicksPerYear`) | `world_state.h`, `deferred_work.h` | Steps and gates are counted in ticks. |
| Physical time (events, arrivals) | seconds, `int64`, 0 at the start of tick 0 | §4.1 (contract, not yet in code) | Latency is ceil-rounded to seconds. |
| Geological time | years `int64`, or Myr `double`, stated per generator | world generation | Never mixed with ticks. |
| Distance, length | km, `float` in world state, `double` in computation | `shared_border_km`, `coastal_length_km`, `length_km` | Suffix `_km`. |
| Elevation, depth | m, `float` | `elevation_avg_m` | Suffix `_m`. |
| Area | km², `float` stored, `double` from H3 | `area_km2` | |
| Mass | tonnes | `transport_cost_per_km_per_tonne`, `tonnes_per_deposit_unit` | Goods quantities are in each good's own unit (CSV). A bridge to tonnes is an explicit, documented constant, never implicit. |
| Position | `H3Index` (`uint64`) is authoritative | `ProvinceLink::neighbor_h3`, `Province::h3_index` | |
| Latitude, longitude | degrees `float`, derived from `H3Index` (`h3_utils::cell_center_lat_lng`) | `GeographyProfile::latitude/longitude` | A cache. Recomputable from the cell. `float` resolves about 1 m at 180°, far below a res-4 cell (~47 km across). |

**Precision at layer boundaries.**
- Generation computes in `double` and stores in the world-state type (usually `float`).
- Any quantity that is derived from another (lat/lng from H3, area from H3) is recomputed from its source rather than carried through a lossy chain.
- Persistence writes exactly the stored type and nothing else (`write_float` for `float`, `write_u64` for `H3Index`). A save/load round trip is therefore bit-exact.
- Accumulations use `double` in canonical order (good_id asc, province_id asc), as CLAUDE.md requires.
- A value crossing from one layer to another keeps its unit. If a consumer needs another unit, it converts at its own boundary with a named function, never by a bare literal in an expression.

---

## 7. Implementation order (foundation first)

| Phase | Work | Closes |
|---|---|---|
| A | This document; renumbered eras; Tier List, GDD, World Map and WorldGen aligned | Doc contradictions D1–D8 |
| B1 | Latitude/longitude from the H3 cell | F2 (quick, testable) |
| B2 | Planet-wide coarse pass + LOD 0 window refinement | F1 |
| B3 | Plates with velocity; boundary type from relative motion | L2 |
| B4 | Province archetype becomes a derived label; remove `apply_archetype` and archetype deposits as seeds | F2 |
| B5 | Stage 2 erosion | F3 |
| B6 | `PlanetaryParameters` as the single source; derive the hazard axes | F4 |
| B7 | Climate on the global grid (V8): sunlight by latitude and season, land/sea heat capacity, wind belts, wind-driven ocean gyres, lapse rate, moisture carried along the wind, Köppen read off the result | V8 |
| C1 | Data-driven deposit types with elements and grades | F7 |
| C2 | Biota generator + biosphere timeline; crops off `ResourceDeposit`; fossil fuels gated | F6 |
| D | Information latency on links | F5 |
| E | Technique gating (§5) + pre-modern content, enough for history to industrialise | F8 |

### Ratchet scenarios

- **[SCENARIO]** *When* the view zooms out from a set of cells to their H3 parent, *then* every conserved quantity shown at the parent equals the sum over its children. (V7)
- **[SCENARIO]** *When* two coasts sit at the same latitude on the western and eastern edges of one ocean basin, *then* the coast with the poleward warm current is warmer in winter than the one with cold upwelling. (B7)
- **[SCENARIO]** *When* a mountain range lies across the prevailing wind, *then* the leeward side receives less precipitation than the windward side, and the moisture missing on the leeward side fell on the windward side. (B7)
- **[SCENARIO]** *When* two provinces are H3 neighbours, *then* their latitudes differ by at most one cell diameter. (B1)
- **[SCENARIO]** *When* a world is generated, *then* the physical pass covers the whole sphere (Σ cell area ≈ 4πr²) and the LOD 0 provinces are a subset of it. (B2)
- **[SCENARIO]** *When* a province sits on a converging continental–continental boundary, *then* its pre-erosion elevation exceeds the mean of non-boundary cells on the same plates. (B3)
- **[SCENARIO]** *When* a world is generated, *then* no deposit exists whose type is not allowed for its cell's tectonic context and host rock. (C1)
- **[SCENARIO]** *When* `surface_gravity_ms2 = 14.7`, *then* `hazard_settings.gravity_g == 1.5`. (B6)
- **[SCENARIO]** *When* `land_plants_gyr_ago == 0`, *then* no coal deposit exists. (C2)
