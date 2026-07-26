# Module: knowledge

## Purpose
The engine that lets a pre-market society move *forward*. Surplus frees a few livelihoods
into knowledge-keepers (elder -> scribe -> scholar: occupations with `knowledge_output > 0`),
and under pressure the whole population intensifies as well. Together they accumulate one
civilization-level stock, `GlobalTechnologyState.knowledge_level`, which (a) raises the
subsistence carrying ceiling — the escape from the Malthusian trap — and (b) advances the era
once it clears that era's data-driven `knowledge_to_advance`. Progress is contingent: a
society with no knowledge-keepers and no pressure never accumulates and stays put.

Sequential and global (it aggregates a single figure), annual cadence, pre-market only — in
market eras the `technology` module owns advancement.

Design: docs/design/EconLife_World_Spectrum_and_Evolution_Plan.md (the knowledge engine);
adversity term per Boserup intensification + the Deathworlders premise (a hard world forges
capability).

## Cadence & gating
- **Annual.** Returns immediately unless `current_tick != 0 && current_tick % kTicksPerYear == 0`
  (`kTicksPerYear = 365`, the core constant in `shared_types.h`). The `t = 0` snapshot tick is
  skipped. Every config rate is per-YEAR.
- **Regime-gated** to `KnowledgeConfig.active_regimes` — by default the whole pre-market arc
  (`subsistence`, `barter`, `coinage`, `money`, `feudal`, `mercantile`, `industrial`). In any
  other regime it emits nothing and leaves `knowledge_level` untouched.
- **No regime-exit reset is needed or wanted.** `knowledge_level` is a cumulative STOCK, not
  a transient per-tick signal: leaving it standing on regime exit is correct (contrast
  `warfare.war_death_fraction` and the reset `subsistence` owes on its surplus signal).
- Sequential: `is_province_parallel()` is the default `false`; all work is in `execute()`.
- `runs_after: ["subsistence", "technology"]`. Because the orchestrator applies each
  module's deltas immediately after that module runs, this module reads the SAME-tick
  livelihoods and surplus that `subsistence` just published — not last tick's.

## Inputs (from WorldState)
- `current_tick` — the annual cadence test
- `technology.current_era` + `era_catalog.by_index()` -> `EraDefinition.economic_regime`
  (regime gate) and `EraDefinition.knowledge_to_advance` (the advancement threshold);
  `era_catalog.max_era()` (the forward bound)
- `technology.knowledge_level` — the level the advancement test compares against and the
  base the annual decay is taken on
- `significant_npcs[].occupation` -> `occupation_catalog.by_index()->knowledge_output` —
  dedicated knowledge work (`occupation == 0` is skipped; only `knowledge_output > 0`
  contributes)
- `provinces[].cohort_stats->total_population` and `->subsistence_surplus_ratio` — the
  population-weighted average surplus, which becomes the scarcity pressure term (provinces
  with null `cohort_stats` are skipped)
- `hazard_settings` -> `hazard_mortality_from_settings()` — the world's hazard level, the
  other pressure term
- `tech_effects_for_era(current_era).knowledge_mult` — writing / printing / the scientific
  method compound the whole production term ("learning to learn")

## The production law (this is the contract)
```
specialist_term  = production_scalar * SUM over significant_npcs of occupation.knowledge_output
population_term  = population_innovation_rate * SUM over provinces of total_population
avg_surplus      = SUM(pop * subsistence_surplus_ratio) / SUM(pop)      (1.0 if no population)
scarcity         = clamp(1 - avg_surplus, 0, 1)
pressure         = clamp(adversity_base
                        + adversity_hazard_weight * max(0, world_hazard - adversity_garden_hazard)
                        + adversity_scarcity_weight * scarcity,
                        0, adversity_pressure_cap)
production       = (specialist_term + population_term) * pressure * knowledge_mult
decay            = decay_per_year * knowledge_level
net              = production - decay
```
Scarcity itself spurs the intensification that lifts the food ceiling — that is the grounded
escape from the Malthusian wall — and harsh worlds out-innovate comfortable gardens, which is
what makes the World-Class spectrum matter.

## Outputs (to DeltaBuffer)
At most ONE `TechnologyDelta` per year. No region deltas, no NPC deltas, no deferred work,
no serialized module state.
- `TechnologyDelta.knowledge_delta` (**additive**; `technology.knowledge_level`) — set to
  `net` only when `net != 0`. The apply layer floors the resulting level at 0.
- `TechnologyDelta.new_era` (**replacement**; `technology.current_era`) — set to
  `current_era + 1` when `era->knowledge_to_advance > 0 && knowledge_level >= era->knowledge_to_advance`
  and `current_era < era_catalog.max_era()`. The apply layer accepts it only if it is
  strictly greater than the current era (forward-only) and stamps `era_started_tick`.
- Nothing is pushed when both fields are empty (no scholars, no population, zero decay).

Note the advancement test reads the level BEFORE this year's `knowledge_delta` is applied, so
crossing a threshold advances the era on the following annual pass.

## Configuration (`KnowledgeConfig`, core/config/package_config.h)
- `active_regimes` — the regime gate
- `production_scalar` — knowledge/year per unit of scholar `knowledge_output`
- `decay_per_year` — annual attrition, **0.0 by default and deliberately so**:
  civilizational knowledge is cumulative (embodied in people, practices and later writing).
  A transient population dip must not erase technique, or a society that overshoots its food
  supply would lock into the Malthusian trap instead of recovering. Era thresholds, not
  decay, govern pace. Dark-age regression is out of V1 scope.
- Adversity term: `population_innovation_rate` (knowledge/yr per person at unit pressure),
  `adversity_base`, `adversity_hazard_weight`, `adversity_scarcity_weight`,
  `adversity_garden_hazard`, `adversity_pressure_cap`

## Preconditions
- `technology` has run this tick, so `current_era` is resolved.
- `era_catalog` is loaded (a null era definition is treated as inactive) and
  `occupation_catalog` is loaded (otherwise `by_index()` returns null and no specialist term
  accumulates).
- `subsistence` has run this tick, so occupations reflect the current food balance and
  `subsistence_surplus_ratio` is fresh.
- Provinces that carry population have non-null `cohort_stats`; those that do not are simply
  excluded from both sums.

## Postconditions
- On an active annual tick, `knowledge_level` has moved by exactly `net` (floored at 0).
- The era has advanced by at most one step, never backwards, never past
  `era_catalog.max_era()`.
- On a non-annual tick, a `t = 0` tick, or a market-era tick, `technology_deltas` is empty
  and both `knowledge_level` and `current_era` are untouched by this module.

## Invariants
- **No RNG.** The module makes no random draws at all; it is deterministic by construction.
- Accumulation order is canonical: NPCs in `significant_npcs` index order, provinces in
  province index order, both summed in `double` before the `float` narrowing — same result on
  any core count (it is sequential, so no parallel merge is involved).
- Knowledge is a single GLOBAL stock. There is no per-province knowledge; the module is
  deliberately not province-parallel.
- Era advancement is forward-only and monotone, one step per annual pass, bounded by the
  catalog.
- Contingency is preserved: with no knowledge-producing occupations and no population the
  production term is 0 and, with the default zero decay, nothing is emitted — the society
  genuinely stalls rather than drifting forward.
- The pressure multiplier is bounded by `adversity_pressure_cap`; `scarcity` is bounded to
  `[0, 1]` (a famine cannot produce unbounded invention).
- This module never writes province, NPC or cohort state — its only channel is
  `TechnologyDelta`.

## Known Issues (spec is the contract; implementation currently diverges)
- **Dead NPCs still produce knowledge.** The contract is that only ACTIVE NPCs produce
  knowledge: the specialist sum MUST skip any NPC whose `status != NPCStatus::active`.
  `significant_npcs` is append-only and retains dead/fled NPCs for forensic and memory
  references (see `core/world_state/npc.h`), so an unfiltered sum counts every scholar who
  ever lived. **Today the loop filters only on `occupation == 0`** and therefore includes
  `dead`, `fled` and `imprisoned` NPCs. The effect is a phantom scholar corps that grows
  monotonically with cumulative deaths, so knowledge production ratchets on the DEAD rather
  than tracking the living scholar population — inflating era pacing over long runs, the more
  so the higher mortality is. Fix: filter to `status == NPCStatus::active` before summing
  `knowledge_output`.
- **Cadence dial mismatch with `subsistence`.** This module hard-codes the core
  `kTicksPerYear` (365) while `subsistence` uses its own configurable
  `SubsistenceConfig.ticks_per_year`. A package that changes the subsistence year desynchronizes
  the two annual passes. The intended contract is one year length for the pre-market arc;
  until that is unified, treat `SubsistenceConfig.ticks_per_year = 365` as required.

## Failure Modes
- Null era definition, non-active regime, `current_tick == 0`, or a non-annual tick: return
  with an empty delta buffer (all no-ops, not errors).
- Empty `significant_npcs` or no knowledge-producing occupations: `specialist_term = 0`; the
  population term may still carry progress. Both zero -> nothing emitted.
- Zero total population across all provinces: `avg_surplus` defaults to 1.0 (no scarcity
  pressure) and `population_term = 0`.
- `occupation_catalog.by_index()` returning null for a stored occupation index: that NPC
  contributes nothing (no crash).
- `knowledge_delta` driving the level negative (only possible with a non-zero
  `decay_per_year`): the apply layer floors `knowledge_level` at 0.
- `new_era` beyond the catalog: prevented at source by the `max_era()` test and again by the
  apply layer's forward-only guard.
- Already at `max_era()` with the threshold cleared: no `new_era`, and if `net == 0` nothing
  is emitted at all.

## Performance Contract
- Sequential, main thread, once per 365 ticks.
- O(N) over significant NPCs + O(P) over provinces per annual pass; no allocation beyond the
  single delta push.
- Target: negligible (< 1ms at 2,000 significant NPCs), and it is a no-op on 364 of every 365
  ticks.

## Dependencies
- runs_after: ["subsistence", "technology"]
- runs_before: []
- Consumers of its output: `subsistence` (`knowledge_level` raises the carrying ceiling and
  wanes the predator food penalty via `predator_clearance_halfsat`), and — through
  `current_era` — every era-gated system (`era_catalog` regimes, `tech_effects_for_era`
  multipliers, occupation `min_era` unlocks, the specialist ceiling arc).

## Test Scenarios
Pinned in `simulation/tests/unit/knowledge_test.cpp`:
- `knowledge: regime gate is dawn-only` — active in `subsistence`/`barter`, inactive in
  `modern`.
- `knowledge: scholars produce knowledge at the dawn, none in market eras` — 4 scholars at
  era 1 on tick 365 emit exactly one delta with `knowledge_delta > 0`; the same world at
  era 8 emits nothing; a dawn world with no scholars emits nothing (progress is contingent).
- `knowledge: accumulated knowledge advances the era` — at era 1 with knowledge 4300 (just
  over the builtin `knowledge_to_advance`), the delta carries `new_era == 2`.

Not yet covered (should exist per this spec):
- `test_dead_scholars_do_not_produce`: two identical worlds, one where every scholar has
  `status = NPCStatus::dead`; verify the dead world emits no specialist contribution.
  This test currently FAILS by design of the known issue above.
- `test_non_annual_tick_is_noop`: any tick where `current_tick % 365 != 0` produces no
  technology deltas.
- `test_scarcity_raises_production`: identical worlds differing only in
  `subsistence_surplus_ratio` (0.6 vs 1.4); verify the scarce world accumulates more
  knowledge, bounded by `adversity_pressure_cap`.
- `test_era_advance_is_forward_only_and_capped`: at `era_catalog.max_era()` with the
  threshold cleared, verify no `new_era` is emitted.
