# Production-Grounding Genesis Firms: Energy as Era-Appropriate Mechanism

**Status:** PROPOSAL — for review. No code changed.
**Date:** 2026-06-17
**Scope context:** Integration & behavioral validation phase. Follows the firm-genesis
work (abstract endowment-grounded genesis, committed) and the decision to *production-ground*
genesis firms (real facilities producing real catalog goods from conserved, located resources).

---

## 1. Why this document exists

We set out to production-ground genesis firms: instead of a firm earning an abstract
`revenue_per_tick` number, it should run a real facility that produces real catalog goods
from finite, located resources — the same conserved-resource grounding the project is
applying everywhere else (extraction→production→energy→waste). The purest first slice was
**genesis extraction firms** (mines/wells), placed only where the matching `ResourceDeposit`
exists — the Norway case ("an economy depends on its seeded endowment").

The infrastructure worked end-to-end (facilities spawned, synced into the production
registry, computed real output). But the slice surfaced **two genuine blockers** that are
bigger than a slice and must be fixed *as mechanism, not papered over*:

1. **A latent correctness bug** — the first-loaded good (currently `iron_ore`) gets
   `numeric_id == 0`, which production treats as "unknown good — skip". So that good can
   never book production revenue or supply, anywhere in the sim.
2. **An anachronistic energy model** — production gates every facility's output on an
   *electricity* brownout ratio, in *every era*. Pre-industrial extraction was never
   electric. A power-gate workaround was tried and correctly rejected as a fake rail.

A third issue is structural and must be acknowledged up front:

3. **Demand-side grounding** — raw-commodity output is only worth what a downstream
   consumer will pay. Grounding extraction without grounding what consumes the ore just
   relocates the abstraction one stage downstream.

This proposal covers all three as one coherent subsystem, because production-grounding is
not honest until all three hold.

---

## 2. Part A — The good-id-0 sentinel bug (correctness prerequisite)

### 2.1 The bug

- `GoodsCatalog` assigns `numeric_id` sequentially from **0**
  (`goods_catalog.h:58`, `goods_catalog.cpp:95`).
- `lookup_good_id()` returns **0** to mean "good not found"
  (`apply_deltas.cpp:1041-1053`).
- These collide. The first good loaded — `iron_ore`, alphabetically first across the goods
  CSVs — gets `numeric_id == 0`, indistinguishable from "not found".
- Consumers treat `0` as the invalid sentinel, e.g. `production_module.cpp:594`
  `if (output_gid == 0) continue;` (the comment there even documents *why* 0 must be
  treated as invalid). Result: **iron ore is silently unsellable for every producer**,
  world-gen mines included. It went unnoticed because it is exactly one good and most
  firms output others.

This is a real bug independent of genesis or grounding. It should be fixed on its own.

### 2.2 Recommended fix: 1-based numeric ids, 0 reserved as "invalid"

Make `numeric_id` start at **1**; keep `0` meaning "no good / unknown". This is the
*least invasive* option: every existing `== 0` / `!= 0` guard remains **semantically
correct** (0 still means unknown) and simply stops colliding with a real good.

> An alternative (sentinel = `UINT32_MAX`, keep iron_ore at 0) was considered. It requires
> editing every guard site across production, drug_economy, seasonal_agriculture,
> regional_conditions, npc_spending — far more churn and risk for no benefit. Rejected.

### 2.3 Blast radius (from a full-tree audit)

**Must change (core, small):**
- `goods_catalog.h:58` — `next_numeric_id_ = 0` → `= 1`. (Assignment at `:95` follows
  automatically; `push_back_loaded` at `:156` already skips loaded ids — safe.)
- `world_generator.cpp:969` — catalog-absent fallback markets hardcode `good_id` 0–9;
  shift to 1–10 to stay consistent. (Catalog-present path at `:992` uses `numeric_id`
  directly — safe.)

**Safe, no change (these are the win of 1-based):**
- `good_id_hash` (FNV-1a) **cannot return 0** (non-zero offset basis) — the catalog-absent
  unit-test path is unaffected (`good_id_hash.h:27-36`).
- All composite `(good_id<<32 | province_id)` market keys/indices work for any id
  (`apply_deltas.cpp:340-350, 1220-1228`).
- Every runtime `gid == 0` / `gid != 0` guard (production `:134,139,345,362,483,594,632`;
  drug_economy `:203,275`; seasonal_agriculture `:248,386,446`; regional_conditions `:301`;
  npc_spending) stays correct — 0 still means unknown.
- Tests asserting `lookup_good_id(..., "nonexistent") == 0u` stay correct
  (`npc_province_index_test.cpp:379`, `persistence_scenarios.cpp:152`).
- Tests asserting emitted `md.good_id != 0` stay correct (production should never emit an
  unknown good): `production_test.cpp:763`, `seasonal_agriculture_test.cpp:625`.

**Persistence (decide migration policy):**
- `persistence_module.cpp:1728` writes `numeric_id`; `:2106` reads it;
  `CURRENT_SCHEMA_VERSION = 18` (`persistence_module.h:156`).
- Bump schema to 19. Pre-game/early dev: simplest is to **reject** pre-19 saves (no live
  worlds to preserve). If any saved worlds must survive, add a load-time migration that
  shifts persisted ids +1. *Open question for review — see §6.*

**Tests that hardcode a specific id (mechanical updates):**
- `world_generator_test.cpp:139-141` (`numeric_id == 0,1,2` → `1,2,3`).
- `production_test.cpp:717,730` and `seasonal_agriculture_test.cpp:603` and
  `persistence_scenarios.cpp:126` (`wheat.numeric_id = 0` → `1`, and matching market id).
- `emergence_harness.h:198` (`if (m.good_id == 0)` selects "first good" for a metric →
  select `== 1`). `political_social_scenarios.cpp:515,524` (food/luxury indices).
- `npc_province_index_test.cpp:366,370` already use non-zero ids — safe.

### 2.4 Validation
Fast gate (`ctest -LE emergence`) + emergence suite (`ctest -L emergence`). A new unit test
asserts: a recipe whose output is the first catalog good books non-zero supply **and**
revenue (the exact case that was silently broken).

---

## 3. Part B — Energy as era-appropriate mechanism (the grounding)

### 3.1 Why the current model is a fake rail in early eras

Current model (production INTERFACE.md invariant "Energy"): a per-province pre-pass sums
`energy_per_tick` over facilities as **electricity** demand, generates electricity from
renewables + burned fossil fuel, and scales *every* facility's output by the brownout
ratio `clamp(generation/demand, 0, 1)` (`production_module.cpp:273-371, 561`).

This is correct for the industrial era onward. It is anachronistic before it: a medieval
mine or mill ran on **muscle, water/wind mechanical power, and charcoal/wood** — never
electricity. Gating early extraction on an electricity ratio (and then *gating placement*
on renewable presence, as the rejected workaround did) models nothing; it just hides the
mismatch. Per project principle: **no hacks / fake rails.** The fix is to model the actual
motive power of the era.

Note: `recipe.labor_per_tick` currently does **nothing** to output — it is only a cost
coefficient (`recipe_catalog.cpp:208`: `base_cost = energy*10 + labor*5`). Muscle, the
dominant pre-industrial prime mover, is therefore unmodeled as power today.

### 3.2 Proposed model: motive power as a first-class, era-bound, conserved input

Generalize the electricity pre-pass into a **motive-power** pre-pass. A facility needs a
quantity of *work* per tick; that work can be supplied by era-available power **forms**,
each grounded in a real, conserved or free-flow source:

| Power form | Source (already in WorldState) | Conservation | Era availability |
|---|---|---|---|
| **Muscle** | `worker_count` / `labor_per_tick`; animal draught | labor is a real input; consumes worker-time | all eras (dominant early) |
| **Water / wind (mechanical)** | `river_flow_regime`, `WindPotential` deposits | free flow (not depleted) | water early; European wind ~high-medieval |
| **Biomass fuel** | charcoal/wood/peat goods (consumable) | matter burned → work (like fossil top-up) | all eras until displaced |
| **Fossil → electricity** | `crude_oil`/`natural_gas`/`thermal_coal` → `electricity` | matter → electricity → work | industrial era onward |

Key properties:
- **Output is bounded by available work, not by electricity specifically.** Early eras: the
  binding budget is muscle + local mechanical (water/wheel, wind/mill) + biomass. Industrial
  era: electricity becomes the binding form, as today.
- **Mechanical renewables do mechanical work directly** — the *same* `river_flow_regime` /
  `WindPotential` endowment, but a water-wheel mill is not "generate electricity then
  consume it." Only the industrial path routes through the `electricity` good.
- **Where genuinely no power exists, low/zero output is the correct emergent answer** — not
  a bug to gate around. A firm founded where it cannot be powered should struggle; that is
  signal, not breakage.
- **Conservation holds throughout:** labor-time consumed, biomass matter burned, fossil
  matter burned; free flows (water/wind/solar) are not depleted. No work is minted.

### 3.3 Concrete shape (for review — not yet final)
- Rename/generalize `generate_province_energy` → a power pre-pass that returns an
  **availability ratio per power form** (or a single effective work ratio per facility),
  era-scaled. The electricity path becomes one form inside it.
- Recipes express a **work requirement** with an era-appropriate dominant form. Two options
  to debate in §6: (a) keep one `energy_per_tick` column but reinterpret its *form* by era;
  (b) add explicit columns (`labor_per_tick` already exists; add `mechanical_per_tick`,
  `fuel_per_tick`). Option (b) is data-driven and modder-legible, consistent with the CSV
  philosophy, at the cost of a recipe-schema migration.
- Update production INTERFACE.md "Energy" invariant to the motive-power model (spec wins;
  change through review per the Module Interface Contract).

---

## 4. Part C — Demand-side grounding (no relocated abstraction)

Extraction output is only meaningful if something consumes it. In a founding economy with
no downstream processing, raw ore has no buyers; `price_engine` drives its price toward the
floor and the "revenue" is fictitious either way. Grounding extraction while leaving its
consumer abstract just moves the fake rail one stage downstream.

Two honest options (decide in review):
- **Ground the short chain:** seed/allow genesis of the immediate downstream consumer too
  (ore → smelter → metal → basic goods), so value circulates through real recipes. Larger,
  but it is the version with no abstraction.
- **Scope grounding to where real demand already exists:** production-ground only those
  sectors whose output already has a modeled consumer in the founding economy
  (e.g. food/agriculture → households), and leave commodity extraction abstract until its
  downstream is grounded. Smaller, honest about its own boundary, and avoids dead firms.

Recommendation: start with the **second** (food/agriculture, which has a real consumer:
people eat), since it grounds production *and* demand simultaneously, then extend to
extraction once at least one downstream processing consumer is grounded.

---

## 5. Sequencing

Each step lands independently and leaves all gates green.

1. **Part A — good-id-0 fix.** Pure correctness; unblocks any facility outputting the first
   good. Smallest, highest-confidence change. Ship first.
2. **Part B — motive-power model.** Generalize the energy pre-pass to era-bound power forms;
   make muscle/mechanical/biomass real; electricity becomes the industrial-era form. Update
   INTERFACE.md. Validate output is sane across eras (early mill runs on water/muscle;
   industrial plant on electricity).
3. **Part C(2) — ground a sector with real demand (food/agriculture).** Genesis firms with
   real facilities + real consumers; verify the founding economy bootstraps on real
   production, not abstract revenue.
4. **Part C(1) — extend to extraction + one downstream consumer.** Only after a downstream
   exists, so commodity prices mean something.

Determinism, perf (< 200 ms / 2k NPCs target), and conservation must hold at every step;
the emergence suite is the behavioral backstop.

---

## 6. Decisions & open questions

Resolved in review:
1. **Save migration (A):** RESOLVED — no change needed. Ids round-trip as stored and there
   are no committed pre-fix binary saves; a schema bump is deferred until Parts B/C touch
   the format. (Implemented in commit "number goods from 1…".)
2. **Recipe schema (B):** RESOLVED — explicit columns. `mechanical_per_tick` and
   `fuel_per_tick` added alongside `labor_per_tick` (muscle) and `energy_per_tick`
   (electricity). Data-driven and modder-legible. (Implemented in B1.)
3. **Muscle accounting (B):** RESOLVED (lean adopted) — `labor_per_tick` stays the
   staffing/cost signal it is today; muscle-as-power is treated as "met" when the facility
   is staffed, so we do not add a second labor drain and do not double-count the
   labor-market allocation. The physically-supplied forms B2 grounds are mechanical, fuel,
   and electricity.
5. **Power gating / era boundaries (B):** RESOLVED — fluid start, no calendar anchor, and
   **emergent supply-gating** (no hardcoded era cutoffs). Electricity exists only where
   generation capacity exists; a province with no generators simply has no electricity, so
   electricity-only recipes can't run there while water/wind/biomass-powered ones can. Era
   affects only recipe *availability* (`era_available`).

Still open:
4. **Grounding order (C):** food/agriculture-first (real demand: people eat) vs
   extraction-first (purest endowment but no downstream consumer). (Lean: food/agriculture-
   first.) Decide when Part C begins.
6. **Fuel good (B2/B-data):** burn the existing wood goods (softwood/hardwood logs,
   wood_chips) as biomass, or add a dedicated `charcoal` good (historically the
   metallurgical fuel)? (Lean: start with existing wood goods; revisit charcoal with the
   B-data pass.)
