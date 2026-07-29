# Module: subsistence

## Purpose
The commons food economy of the dawn. Before firms, facilities and markets, the whole
province population works the land directly (forage / subsistence farming / hunting /
fishing / herding): food is produced from the province's natural capital by labour, against
a saturating carrying ceiling (fixed land feeds only so many). From that food balance the
module publishes the master variable of the pre-market arc — the surplus over what a
sustainable society must produce — banks the year's net into the province granary, exposes
the haulable grain surplus for the ox-cart catchment, decides how many residents the
farmers do NOT need (the grounded specialist share), assigns each resident a livelihood,
and accrues the surplus as proto-capital to the resident heads (evenly in the egalitarian
commons; concentrated to an emergent lord stratum under manorialism).

Province-parallel: each province's food balance is fully independent. Runs every tick; the
granary stock folds a whole year's net once per year. Regime-gated to the pre-market arc —
inert in market eras, where the modern production/market machinery feeds the population.

Design: docs/design/EconLife_Origin_Economy_and_Early_Jobs_v01.md (§1 the master variable,
§2 the early jobs, §3 production without facilities);
docs/design/EconLife_Mechanical_History_Generation_Plan.md (Band 1).

## Cadence & gating
- **Per tick** for the surplus signal, the haulable grain surplus, livelihood assignment and
  proto-capital accrual (all rates are per-tick quantities).
- **Annual** for the granary: at `current_tick > 0 && current_tick % SubsistenceConfig.ticks_per_year == 0`
  the year's net food (`output - need - spoilage`, x ticks_per_year) is folded into
  `food_store`. On every other tick `food_store_replacement` republishes the unchanged
  stock (idempotent).
- **Regime-gated** to `SubsistenceConfig.active_regimes` — by default the whole pre-market
  arc (`subsistence`, `barter`, `coinage`, `money`, `feudal`, `mercantile`, `industrial`).
  In any other regime (e.g. `modern`) `execute_province` returns before emitting anything.
- **Manorial sub-gate**: `SubsistenceConfig.manorial_regimes` (`feudal`, `mercantile`,
  `industrial`) switch proto-capital distribution from the even commons split to the
  lord tithe. The earlier regimes stay egalitarian.
- Province-parallel (`is_province_parallel() == true`); `execute()` is an intentional no-op
  and `has_global_post_pass()` is the default `false`, so it is never invoked.
- `runs_after: ["technology"]` (needs `current_era` resolved). No `runs_before`.

## Inputs (from WorldState)
- `technology.current_era` + `era_catalog.by_index()` -> `EraDefinition.economic_regime` —
  the regime gate, the specialist ceiling selector, and the manorial gate
- `technology.knowledge_level` (K) — raises the carrying ceiling (the Malthusian escape),
  saturating: `1 + knowledge_productivity_max * K / (K + knowledge_productivity_halfsat)`;
  also the clearance term that wanes the predator food penalty
- `tech_effects_for_era(current_era).food_mult` — food techs (plough, irrigation, heavy
  plough, watermill) raise the ceiling
- `provinces[]`: `id` (RNG seed material), `region_id` (delta key),
  `agricultural_productivity`, `geography.arable_land_fraction`,
  `geography.forest_coverage`, `fisheries.current_stock` — the natural-capital blend
- `provinces[].cohort_stats` (must be non-null): `total_population` (mouths and hands),
  `working_age_fraction` (labour share; `<= 0` falls back to 0.6), `food_store` (the
  granary this tick's spoilage and build demand are computed from)
- `hazard_settings.seasonality` — chronic food penalty measured RELATIVE to
  `earth_hazard().seasonality` (earthlike is neutral), and the dial scaling the episodic
  bad-harvest year; `hazard_settings.predators` — chronic herd losses, waning with
  knowledge; `hazard_settings.atmosphere` — planetary ceiling penalty, never wanes
- `world_seed`, `current_tick` — the harvest RNG is seeded per (world, harvest YEAR,
  province): `world_seed ^ (year * 0x9E3779B97F4A7C15) ^ (province.id << 29) ^ 0x4A12E57`,
  where `year = current_tick / ticks_per_year`, so a bad harvest is consistent across every
  tick of that year and varies year to year
- `npc_indices_by_home_province[province_idx]` + `significant_npcs[]`: `id`, `capital`
  (the emergent lord ranking), `occupation` (change detection)
- `occupation_catalog.in_layer(1)` (food producers) and
  `in_layer_for_era(2, current_era)` (era-unlocked specialists: elder -> scribe once
  writing exists -> scholar with formal scholarship)

## Outputs (to DeltaBuffer)
One `RegionDelta` per active province with population, plus at most one `NPCDelta` per
resident significant NPC. No deferred work items; no serialized module state.
- `RegionDelta.subsistence_surplus_replacement` (**replacement**; `cohort_stats->subsistence_surplus_ratio`)
  — the long-run food signal that paces population: `output / (need + full_upkeep)` where
  `full_upkeep = granary_spoilage_rate * granary_reserve_years * need`. Reads 1.0 when the
  society exactly feeds itself AND replaces the grain that spoils out of a full reserve, so
  the population settles at its sustainable ceiling and LEAVES the upkeep as the permanent
  surplus that funds the specialist class. `apply_region_deltas` floors it at 0 and admits
  at most 10 (a non-finite/blow-up sentinel, not gameplay).
- `RegionDelta.food_store_replacement` (**replacement**; `cohort_stats->food_store`) — the
  granary. Annually `clamp(food_store + (output - need - spoilage) * ticks_per_year, 0, target_store)`
  with `target_store = granary_reserve_years * need * ticks_per_year` (the physical capacity);
  otherwise the unchanged stock. Applied floored at 0.
- `RegionDelta.grain_surplus_replacement` (**replacement**; `cohort_stats->grain_surplus`) —
  absolute haulable surplus `max(0, output - need)`, the grain `grain_logistics` moves under
  the ox law. Applied floored at 0.
- `NPCDelta.new_occupation` (**replacement**; `NPC.occupation`) — emitted only when the
  chosen livelihood differs from the current one. The first `specialists` residents (by
  resident-vector position) take era-unlocked Layer-2 roles round-robin; everyone else takes
  a Layer-1 food role round-robin.
- `NPCDelta.capital_delta` (**additive**; `NPC.capital`) — this tick's proto-capital share,
  emitted only when positive. Pool = `proto_capital_rate * (output - need)`, distributed via
  `proto_share_for()`: the even split in the commons; under manorialism a peasant base of
  `total * (1 - tithe) / n` for everyone plus `total * tithe / lords` on top for each lord.
  Conserved — summed over all residents it equals the pool exactly.

## Configuration (`SubsistenceConfig`, core/config/package_config.h)
- Gates: `active_regimes`, `manorial_regimes`
- Need & natural capital: `per_capita_food_per_tick`, `weight_agricultural_productivity`,
  `weight_arable_land`, `weight_forest_forage`, `weight_fisheries`
- Production curve: `ceiling_per_capital_unit`, `labor_half_saturation` (labour at
  `1 - 1/e` of the ceiling)
- Knowledge coupling: `knowledge_productivity_max`, `knowledge_productivity_halfsat`
- Granary: `granary_reserve_years`, `granary_spoilage_rate`, `granary_build_rate`,
  `ticks_per_year`
- Hazard channels: `seasonality_food_penalty`, `seasonality_failure_base_rate`,
  `seasonality_failure_severity`, `predator_food_penalty`, `predator_clearance_halfsat`,
  `atmosphere_cap_penalty`
- Specialization: `max_specialist_fraction` (fallback for an unlisted regime) and the
  per-regime ceilings `specialist_ceiling_{subsistence,barter,coinage,money,feudal,mercantile,industrial}`
  (institutional capacity, monotonically rising along the arc)
- Distribution: `proto_capital_rate`, `manorial_tithe_rate`, `manorial_lord_fraction`

## Preconditions
- `technology` has run this tick, so `current_era` is resolved and `era_catalog.by_index()`
  returns a definition.
- `province_idx < provinces.size()`, `provinces[idx].cohort_stats != nullptr`, and
  `total_population > 0` — otherwise the province is skipped and its fields keep whatever
  they held (default 1.0 / 0.0 on a fresh world).
- `occupation_catalog` is loaded if livelihoods are expected; with empty layer lists the
  occupation is left unchanged and only proto-capital is written.
- `ticks_per_year > 0` (see Failure Modes).
- `npc_indices_by_home_province` is sized to the province count for the resident pass; if
  it is shorter, or the province has no residents, the `RegionDelta` is still emitted and
  the NPC pass is skipped.

## Postconditions
- Every active province with population has published all three cohort fields
  (`subsistence_surplus_ratio`, `food_store`, `grain_surplus`) for this tick.
- The specialist share is GROUNDED, not dialled: `labor_needed` inverts the saturating
  output curve for `desired_output = need + granary_demand` (spoilage + reserve build),
  `farmers_needed = labor_needed / working_fraction`, and
  `specialists = population - farmers_needed` bounded to
  `[0, population * specialist_ceiling(regime)]`. Rising knowledge lifts the ceiling, so
  fewer farmers are needed and more hands are freed — no heuristic does that work.
- The stratum is published TWICE: `specialist_fraction` (what the society holds, with
  generational inertia) and `supported_specialist_fraction` (what this year's harvest can
  pay for). The GAP between them is elite overproduction, which `structural_demography`
  consumes — people raised to expect a place the land no longer provides.
- Actual output is harvested by the farmers who remain:
  `output = base_ceiling * (1 - exp(-farm_labor / labor_half_saturation))`.
- TOWNSFOLK DO NOT FARM (2026-07-28). The non-farmers are the UNION of the institutional
  stratum and the people actually living in the town —
  `non_farmers = max(specialists_people, cohort_stats.urban_population)`, a set union
  rather than a bound, since the town is a subset of the stratum whenever the stratum is
  larger. Before this a town cost the harvest nothing, so migration could pour the whole
  population into the towns and urbanisation ran away past 95%.
- SOIL RENEWAL IS ABSOLUTE, NOT PROPORTIONAL (2026-07-28). The sustainable harvest is
  measured against the land's PRISTINE natural capital (`soil_health = 1`), not its
  current worn state: weathering, rainfall and nitrogen fixation are properties of the
  place, not of how depleted the topsoil already is. Measuring against current capital
  made `soil_health` cancel out of the pressure ratio, leaving the land with no
  restoring force — earthlike settled at 10-20% of pristine fertility and stayed there
  for 14,000 years.
- Every resident head holds a livelihood drawn from Layer 1 or the era-unlocked Layer 2.
- Proto-capital is conserved across the residents of the province, and the lords are the
  wealthiest residents by `capital` (ties by `NPC.id`).
- The granary never exceeds `target_store` and never goes below 0.

## Invariants
- Pure-commons: this module touches NO goods, recipes, facilities, markets or firms. Food is
  an abstract per-province balance produced from natural capital and labour.
- Deterministic. All randomness is the harvest-failure draw from a `DeterministicRNG` seeded
  by (`world_seed`, harvest year, `province.id`) — no time, no thread id, no `std::rand`.
  Province-parallel deltas merge in ascending province index order.
- The lord ranking sorts by `capital` descending with `NPC.id` ascending as tie-break, then
  re-sorts the selected positions ascending before use — stable regardless of iteration or
  core count.
- `natural_capital_of()`, `subsistence_output()` and `lord_count()` are pure statics defined
  INLINE in the header on purpose: `core/world_gen/premarket_genesis.cpp` (entry
  materialization) runs the SAME law and core must not need module object code to link. One
  law, one source of truth — do not fork them.
- `surplus_ratio()`, `harvest_failure_factor()`, `predator_food_factor()`,
  `atmosphere_ceiling_factor()`, `proto_share_for()` and `specialist_count()` are pure and
  unit-tested directly.
- Proto-capital is conserved: distribution reshapes the same pool, it never mints.
  Manorialism raises inequality without changing the total.
- `specialist_ceiling()` is monotone non-decreasing along
  subsistence <= barter < coinage < money < feudal < mercantile < industrial; an unlisted
  active regime falls back to `max_specialist_fraction`.
- Ceiling factors are multiplicative and each is `<= 1` (harvest failure, predators,
  atmosphere) or `>= 1` (knowledge, food tech), so a pristine, knowledge-free world is
  exactly neutral.

## Known Issues (spec is the contract; implementation currently diverges)
- **Missing regime-exit reset of the surplus signal.** `cohort_stats->subsistence_surplus_ratio`
  is written ONLY by this module, and `population_aging` reads it unconditionally in EVERY
  era as its `birth_surplus` fertility term. The contract is therefore the same as
  `warfare`'s: on the first tick after the regime gate closes, the module MUST publish a
  one-time `subsistence_surplus_replacement = 1.0` (behaviour-neutral "fed") for every
  province — and, for the same reason, `grain_surplus_replacement = 0.0` — tracked by a
  dirty flag so the reset fires once and the module then stays silent.
  **Today it does not**: `execute_province` returns before emitting anything on regime
  exit, so whatever the last pre-market tick wrote (e.g. 1.4, or 0.6 in a famine) persists
  forever and keeps biasing modern fertility. Only a world that starts in a market era is
  safe, because the field defaults to 1.0. (The `famine_surplus` channel in
  `population_aging` is separately commons-gated, so the leak is confined to the birth
  channel.) Fix belongs on the publisher side — consumers must not have to know the gate.
- **Two behaviour-shaping clamps are calibration, not mechanism**, and are flagged against
  the Grounding Doctrine for review rather than defended here: the seasonality factor is
  clamped to `[0.3, 1.3]`, and the specialist share is clamped to
  `population * specialist_ceiling(regime)`. The specialist ceilings are documented as
  institutional capacity (pre-industrial economies ran ~80-90% farmers) and the food balance
  usually binds first, but neither clamp arrives as a physical limit.

## Failure Modes
- `province_idx >= provinces.size()`, `cohort_stats == nullptr`, or `total_population == 0`:
  return without emitting. Fields keep their previous/default values.
- Era index not in the catalog (`by_index()` returns null): treated as inactive; no output.
- `working_age_fraction <= 0`: falls back to 0.6 (a sane working-age share) rather than
  producing zero labour.
- `labor_half_saturation <= 0`: falls back to 1.0 to avoid a divide-by-zero in the
  saturation term.
- `ticks_per_year == 0`: the harvest-year computation guards it (falls back to 365) but the
  annual granary test `current_tick % cfg_.ticks_per_year == 0` does NOT — a package that
  configures 0 divides by zero. Treat `ticks_per_year > 0` as a config precondition; the
  missing guard is a hardening item.
- `natural_capital <= 0` (barren province) or `desired_output >= base_ceiling` (the land
  cannot reach the target even with everyone farming): all labour farms,
  `specialists = 0`, output is whatever the full labour force can raise; a sustained deficit
  drains the granary and then fires famine in `population_aging` (a legitimate outcome, not
  an error).
- Resident indices past `significant_npcs.size()`: skipped individually.
- A resident with no occupation change and a non-positive proto share produces no
  `NPCDelta` at all (the buffer stays sparse).
- Non-finite or out-of-range published values are contained by the apply layer (surplus
  floored at 0 / admitted to 10; `food_store` and `grain_surplus` floored at 0).

## Performance Contract
- Province-parallel; one worker per province (max 6).
- Per province per tick: O(1) arithmetic for the food balance, O(R) for the resident
  livelihood/proto-capital pass, plus O(R log R) for the lord ranking sort in manorial
  regimes only, where R = resident significant NPCs.
- No allocation in the non-manorial path beyond the delta vectors.
- Target: negligible (< 1ms across all provinces); must not erode the 200ms tick target.

## Dependencies
- runs_after: ["technology"]
- runs_before: []
- Consumers of its outputs: `grain_logistics` (`grain_surplus`, and it takes
  `SubsistenceConfig` at construction for the granary targets), `population_aging`
  (`subsistence_surplus_ratio` for births, `food_store` for the famine gate),
  `knowledge` (population-weighted `subsistence_surplus_ratio` as the scarcity pressure
  term), `warfare` (`food_store` provisions campaigns), and firm genesis / entry
  materialization (founder capital from accumulated proto-capital; shares
  `natural_capital_of`/`subsistence_output`).

## Test Scenarios
Pinned in `simulation/tests/unit/subsistence_test.cpp`:
- `subsistence_output`: zero without land or without labour; monotone in labour; bounded by
  `ceiling_per_capital_unit * natural_capital`; rises with natural capital.
- `surplus_ratio`: `output / (population * per_capita_food_per_tick)`; exactly 1.0 with no
  mouths to feed.
- Regime gate: active across `subsistence`..`industrial`, inert in `modern`.
- Specialist ceiling monotone across the arc; unlisted regime falls back to
  `max_specialist_fraction`.
- `execute_province`: a fertile dawn province emits `subsistence_surplus_replacement > 1`;
  a modern-era province emits nothing.
- `specialist_count`: 0 at surplus <= 1; a share of residents freed above it, bounded by
  `max_specialist_fraction`.
- Livelihoods: all 10 residents of a surplus dawn province get an occupation and at least
  one is Layer 2.
- Proto-capital: a surplus province credits the resident head; a deficit province credits
  nobody.
- Manorialism: stratified regimes only; the tithe concentrates to lords and the
  distribution sums back to the pool; lordship is EMERGENT — the mid-list wealthiest head
  (id 107) collects the tithe, not an array-position lord.
- Hazard channels: harvest failure never fires at `seasonality = 0`, its rate rises with the
  dial and its depth is the exact formula; predators cut food and the cut wanes to ~0 at high
  knowledge (half at `predator_clearance_halfsat`); the atmosphere penalty is planetary.

Not yet covered (should exist per this spec):
- `test_regime_exit_resets_surplus_signal`: run one active tick, advance to a market era,
  tick again, and verify a one-time `subsistence_surplus_replacement == 1.0` (and
  `grain_surplus_replacement == 0.0`) for every province, followed by silence on later
  ticks.
- `test_province_parallel_determinism`: one simulated year across 6 provinces on 1 core vs
  6 cores; verify bit-identical surplus, granary, grain surplus and NPC deltas.
