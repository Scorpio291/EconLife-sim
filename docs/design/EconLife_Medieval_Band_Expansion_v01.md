# EconLife — Medieval Band Expansion (v01)

Status: **proposed** (2026-06-25). Design only; no behavioral change.
Companion to `EconLife_Mechanical_History_Generation_Plan.md` (Band 3, the
pre-industrial/mercantile band — of which Medieval is the entry) and
`EconLife_Historical_Eras_and_Tech_Arc.md` (era 5 = Medieval, regime `feudal`).

This document specifies how the Medieval era (era 5, `feudal`) becomes a
*playable starting point* rather than the thin commons loop it is today — and,
crucially, the philosophy that governs the whole expansion.

---

## 0. Governing principle — the Malthusian wall is inviolable

> "The Malthusian wall shall not be infringed upon. Not all societies deserve or
> are capable of surviving."

This is the **first constraint**, not an afterthought. Every mechanic below is
designed *around* the wall, never *through* it:

- **The urban/specialist class is EARNED, never granted.** It exists only as the
  grounded residue of real agricultural surplus — labor that genuine productivity
  freed from the land. A society that cannot produce surplus has no townsfolk, no
  workshops, no guilds, no medieval economy. That is the correct, intended
  outcome.
- **Forbidden levers (would infringe the wall):** reserving a fixed non-farm
  fraction; damping population growth so a specialist margin "survives"; minting
  specialists from a constant; any restoring force toward a target urban share.
- **The only sanctioned urbanization lever is TECHNIQUE** — agricultural
  productivity (knowledge, era food technologies, land improvement) raising the
  land's *carrying capacity*. Population then grows into the higher ceiling and
  the wall reasserts at the new level. Specialists emerge in the **transient**
  between an advance and its reabsorption — exactly the historical pattern (each
  agricultural revolution funded a wave of town growth, much of it later
  reabsorbed). This is the wall *moving up with technique* (Boserup), not the wall
  being bypassed.
- **Stagnation and collapse are first-class outcomes.** A comfortable garden that
  drowns in fertility-driven population and never urbanizes; a barren world that
  stalls at subsistence; a society that overshoots and crashes — these are
  features of the World-Class/Bounty spectrum, not failures of the model. The
  player may enter a world whose medieval economy is rich, thin, or absent, and
  the difference must be legible.

Consequence for scope: **we do not "fix" the spec→0% finding from the
2026-06-25 era calibration.** We make the medieval *economy* a grounded function
of whatever surplus the society actually earned — which may be none.

---

## 1. Current state — why medieval is thin today

Verified 2026-06-25 against the live catalogs and module gates:

| System at a medieval (era 5) start | State |
|---|---|
| Subsistence (abstract food balance), knowledge climb, commons demographics | **active** |
| Livelihood occupations (farmer/herder/artisan/builder/healer/trader/scribe/scholar) | **assigned as tags** |
| Goods available (`goods_available_at(5)`) | **0** (243 of 251 tagged `era_available=8`, rest 9) |
| Recipes available | **0** (all `era_available>=8`) |
| Functioning facilities | **0** (created only from recipes → none pre-modern) |
| Firm/business genesis | **off** (`genesis_active_regimes = {modern, near_future, space_age}`) |
| Markets, production, labor wages, finance, R&D, criminal economy | **inert** (operate on entities that don't exist pre-modern) |

So a medieval start today is the *neolithic loop reached later*: watch food,
population, and a knowledge counter climb; NPCs carry occupation labels but there
is no economy to act in. The era **spine** (regimes, historically-grounded
thresholds, progression) is solid scaffolding; the era **flesh** (content + the
economic layer) is unbuilt. Per the history-gen plan this is expected — only
Band 5 (modern) is a completed content band.

---

## 2. The grounded chain that makes the wall gate everything

The commons economy already computes, per province, the exact quantities the
medieval layer needs — and both are zero without real surplus:

- **Freed specialists** (`subsistence_module`): `population − farmers_needed`,
  clamped to the regime's specialist ceiling. This is the non-farm labor the food
  balance genuinely liberated. At the wall it is ~0; after an agricultural advance
  it pulses positive.
- **Proto-capital** (`subsistence_module`): a share of surplus food banked to the
  resident founders — "the wealth that later funds the first firms." Zero when
  there is no surplus.

**Design keystone:** the medieval economic layer is sized by, and gated on, these
two grounded outputs. Workshops form only where freed specialists exist to staff
them *and* proto-capital exists to found them. No surplus → neither exists → no
medieval economy. **The wall gates the entire layer automatically, with no extra
restraining force.** Every mechanic below hangs off this chain.

---

## 3. The urbanization mechanism (wall-respecting)

The honest driver of every later section. Three grounded inputs, already on
`WorldState`, raise the carrying ceiling:

1. **Knowledge** (`technology.knowledge_level`) — saturating productivity factor
   (already wired in `subsistence_module`).
2. **Era food technologies** — heavy plough, three-field system, horse collar,
   watermill exist as era-5 tech nodes with `food_mult` values, **but are
   currently inert**: the cumulative `food_mult` hits its 6.0 cap by era ~3, so
   the medieval agricultural revolution has no effect (the same class of bug the
   era calibration found for `knowledge_mult`). To let medieval societies *earn*
   urbanization, the food-productivity stack must be allowed to bite.
3. **Land improvement** (proposed) — a slow, conserved per-province
   `agricultural_capital` that rises with sustained investment of surplus labor
   (clearing, drainage, terracing) and decays without upkeep. A located stock, not
   a modifier.

**Decision required (D1):** raise/retune the `food_mult` cap so era food techs
take effect. This is the single change that lets a productive society break the
wall *upward*. It is wall-respecting (population grows into the new ceiling), but
it is a **spectrum-wide rebalance** — an earlier experiment (cap 6→12) freed
specialists transiently AND pushed the fertile garden into a population trap
(stall at era 3, no specialists → no knowledge). Under §0 that garden trap is an
*acceptable, even desirable* outcome (abundance breeds stagnation). Recommend:
adopt it, re-validate the dawn spectrum, and treat divergent world fates as
intended. To be ratified before any build.

There is **no** population-damping or reserved-fraction counterpart. The wall
stands; only the ceiling moves, and only by earned technique.

---

## 4. Entity genesis for the band

Extend `business_lifecycle` genesis (today: flat `residents / denominator`,
founder-funded, modern-only) with a **pre-market genesis path** for the
`feudal`/`mercantile`/`industrial` regimes:

- **Target is freed specialists, not headcount.** Supportable workshops in a
  province = `freed_specialists / workshop_labor_denominator`. Subsistence-locked
  province → 0 freed specialists → 0 workshops. (Wall-gated by construction.)
- **Founder-funded from proto-capital.** A founding specialist commits their
  accumulated proto-capital as the workshop's seed cash (no money minted — the
  existing genesis invariant). Insufficient proto-capital → no formation.
- **Era-appropriate firm types.** Not corporations: **workshop** (artisan
  household producing a craft good), **guild-shop** (workshop admitted to a
  guild — quality/price privileges, entry cost), **manor** (agricultural estate;
  the feudal production unit binding land + peasant labor + lord). Firm "profile"
  set from the era, reusing the existing `NPCBusiness` record.
- **Self-limiting** at the surplus-set target (the existing saturation deadband),
  so a society at its ceiling stops forming firms — the wall again.

Net: genesis becomes a thermometer of earned surplus. The same code that bootstraps
a modern economy bootstraps a feudal one, but sized by freed labor instead of raw
population.

---

## 5. The feudal economic regime layer

Today `feudal` is behaviorally identical to the commons (only the specialist
ceiling differs). The band adds the regime's defining mechanics, each tied to a
real signal and conserved:

- **Manorialism.** Manors bind peasant labor to land; output is the subsistence
  harvest, but a **tithe/rent** fraction flows from peasant producers to the
  lord (a conserved transfer, not minted), seeding the first concentrated capital
  and the inequality that medieval politics runs on. Grounded in existing
  surplus + a new ownership link.
- **Guilds.** A guild per craft per town controls **entry** (cap on guild-shops,
  an admission cost paid from proto-capital), **quality** (guild-shops gain a
  quality floor via the existing tech-tier/maturation machinery), and **price**
  (a local price floor on the guild's good). All expressed through existing
  market/price and business fields; no new price engine.
- **Towns & local markets.** A town emerges where freed specialists concentrate;
  it hosts a **local market** in the era's goods (the existing `RegionalMarket`
  per good, finally non-empty because §6 content exists) and periodic **fairs**
  (a demand/turnover pulse). Long-distance trade stays thin (mercantile band's
  job).

Each of these is a thin layer over conserved primitives — consistent with the
project's anti-"fake-rail" stance.

---

## 6. Content to author (the data gap)

Data-driven via existing catalogs (`era_available` already supported); the bulk
of the work, lowest engine risk. A minimal but coherent medieval set:

- **Goods (`era_available ≤ 5`):** grain, flour, bread, ale, wool, cloth,
  timber, charcoal, iron ore, iron, tools, leather, pottery, salt. Plus
  **backfill** the handful of basics that logically predate modernity (grain,
  timber, ore) to earlier eras so the agrarian arc isn't empty.
- **Recipes (`era_available ≤ 5`):** milling (grain→flour), baking
  (flour→bread), brewing (grain→ale), weaving (wool→cloth), smithing
  (iron→tools), smelting (ore→iron), tanning (hide→leather), charcoal burning.
  Honor the existing input→output conservation and `key_technology_node` gating.
- **Facility types:** watermill, bakery, brewery, weaver's workshop, smithy,
  tannery, manor farm. (Facility types are not era-gated today; they become
  usable purely because era-5 recipes now exist.)
- **Occupations:** the layer-2 livelihoods already exist (artisan, builder,
  trader, etc.). Add craft-specific occupations only if guild mechanics need
  per-craft identity.

Authoring rule (per CLAUDE.md): all in CSV, keyed by string ids, no enum/code
changes; cross-validated by the existing world-gen recipe↔goods check.

---

## 7. The conditional outcome spectrum (the payoff of §0)

A player choosing a medieval start should meet a world whose feudal economy was
*earned*, and the variance must be legible:

- **Thriving feudal world** (Class-12-ish earthlike, sustained productivity):
  real towns, guilds, a manorial surplus, trade goods — a rich start.
- **Subsistence-locked world** (barren/marginal): few or no freed specialists →
  near-empty medieval layer; the player inherits a hard, agrarian struggle. Valid
  and intended.
- **Stagnant garden** (comfortable, abundance-driven population trap): large
  population, little urbanization, slow knowledge — a "soft" world that never
  modernized. Intended.
- **Collapsed/extinct**: a world that overshot and crashed; not offered as a
  start (or offered as ruins). Intended.

The selection screen should surface this (e.g., a one-line world-state read at
entry) so "start in 500 CE" honestly means "start in *this* society's 500 CE."

---

## 8. Player entry & verbs

Per the history-gen plan, entry = "pick a place (and time); promote that region to
full LOD." Medieval-appropriate verbs (a subset of the modern verb set, era-gated):

- found / run a **workshop** in a craft the town's surplus supports;
- seek **guild** admission (cost, quality/price privileges) or operate informally;
- work a **trade** (livelihood) when capital is too thin to found;
- as landed gentry, manage a **manor** (labor allocation, tithe rate);
- **trade at market / fair** in the era's goods.

Modern-only verbs (corporate R&D, zoning, securities) stay gated off. The player's
agency scales with the society's surplus — in a subsistence-locked world the
honest verb set is small, by design.

---

## 9. Cross-cutting requirements (project invariants)

- **Conservation:** every transfer (tithe, wages, founder capital, trade) is
  drawn from a located stock and paid to another; nothing minted. Production
  consumes located inputs.
- **Determinism:** all draws via `DeterministicRNG`; canonical sort order on
  accumulations; per-province parallelism merges in index order.
- **Spec-first:** every new field gets an `INTERFACE.md`; the dependency rule
  (sim never imports ui) is untouched.
- **Test gates:** fast gate (`ctest -LE emergence`) per change; behavioral gate
  (`ctest -L emergence`) for anything touching food/population/knowledge/genesis,
  plus the `[.society-*]` dawn observes to confirm the spectrum (thriving /
  stalled / garden-trap / collapse) still differentiates after D1.
- **Performance:** medieval entities are fewer than modern; the <200ms tick target
  is not at risk, but history-gen LOD (plan P3) governs deep-time runs.

---

## 10. Phasing (shippable milestones, each gated)

1. **M1 — Earned surplus (engine, D1).** Let the agricultural-productivity stack
   raise the carrying ceiling (food-tech cap revisit + optional land-improvement
   stock). Re-validate the dawn spectrum; ratify the garden trap as intended.
   *Deliverable:* a productive society shows freed-specialist pulses after era-5
   ag advances; a marginal one does not. **No wall infringement.**
2. **M2 — Pre-market genesis.** Feudal genesis gated on freed specialists +
   proto-capital; workshop/guild-shop/manor firm types. *Deliverable:* workshops
   appear only where M1 produced surplus, founder-funded.
3. **M3 — Medieval content.** Author the §6 goods/recipes/facilities + backfill.
   *Deliverable:* non-empty local markets in era-5 goods; real production chains.
4. **M4 — Feudal layer.** Manorial tithe, guild entry/quality/price, towns &
   fairs. *Deliverable:* the regime behaves distinctly from the commons.
5. **M5 — Entry & verbs.** Era selection surfaces the world's earned state;
   medieval player verbs. *Deliverable:* a playable medieval start.

M1 is the linchpin and the riskiest (spectrum rebalance); everything downstream
is inert without it, and it is the only milestone that touches the food/population
core — which it does *by raising the ceiling, never by softening the wall.*

---

## 11. Open decisions (need ratification before build)

- **D1 (blocking):** food-productivity cap/curve so era food techs bite — adopt
  the spectrum shift (incl. the garden population trap) as intended? (§3, §10/M1)
- **D2:** is `agricultural_capital` (land improvement as a located stock) in scope
  for M1, or is knowledge+food-tech enough to drive the first surplus pulses?
- **D3:** manorial ownership model — reuse `NPCBusiness` (manor as a firm with a
  land endowment) or a dedicated estate entity? (Reuse preferred.)
- **D4:** guild representation — a new lightweight per-craft/per-town record, or
  encode guild state on existing business/market fields? (Lean lightweight.)
- **D5:** does the mercantile band (era 6) reuse this genesis path wholesale
  (workshops→firms, guilds→early companies), confirming the design generalizes
  before we commit to it?

---

## 12. One-paragraph summary

Medieval is thin because only the abstract commons loop runs there and 100% of the
goods/recipe/firm content is modern-anchored. The fix is to make the medieval
economy a *grounded, conditional* function of the surplus a society actually earns:
the commons module already computes freed specialists and proto-capital, both zero
without real productivity gains, so gating workshop/guild/manor genesis on them
makes the **Malthusian wall the gate for the entire medieval layer** — no reserved
fractions, no population damping, no restoring force. Societies that break the wall
*upward* through technique get towns and guilds; societies that don't stay
subsistence-locked or stagnate — and a player entering in 500 CE meets whichever
world this one became.
