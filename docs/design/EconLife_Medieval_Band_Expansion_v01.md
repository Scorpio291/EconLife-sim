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
| Population checks: **food** (Malthus + drought/flood) | **active** (well grounded) |
| Population checks: **disease** (epidemics) and **war** (conflict mortality) | **missing** (curve grows monotonically; no plague/war dips — see §5.5) |

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

A second, *spatial* gate compounds it (§3.5): even earned surplus is useless to a
settlement it cannot reach before the draft animals eat it in transit. So the
sizing input is ultimately the **net feedable surplus** of a settlement's
catchment, not just its own province's surplus. Two gates — surplus in **time**
(the wall), surplus in **space** (the ox) — both inviolable.

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

## 3.5 The tyranny of the ox — grain logistics as the feudal generator

Premise (design insight, 2026-06-25): castles, lords, and manors are not
decoration — they are the **emergent solution to a logistics problem**, and the
problem reduces to one conserved fact: *a draft team eats the grain it hauls.*
Over distance the oxen consume the cargo, so grain has a hard economic haulage
radius; beyond it the load is entirely eaten in transit and delivers nothing.
This single matter-conservation limit *generates* feudalism. We make it a
first-class mechanic, not flavour. It is the **spatial** partner to §3's temporal
wall, and equally inviolable.

### The primitive (conserved)
For a load `L` hauled distance `d` over a link, the team consumes grain at rate
`k_eff` per unit distance:

```
delivered(d) = max(0, L − k_eff · d · trip_factor)
```

- `trip_factor` accounts for the empty return leg (the ox still eats going home).
- `k_eff = k_base · terrain(transit_terrain_cost) · road(1 − infrastructure_bonus)
  · mode(LinkType) · gravity(g)` — the world's gravity dial scales it too (§5.5):
  heavier g, the oxen carry less and tire faster, so the radius shrinks and the world
  fragments harder.
- The grain the team eats is **conserved** — deducted from the load and credited
  as draft sustenance, never vanished.
- Physical limit `r_max = L / (k_eff · trip_factor)`: beyond it, delivered = 0.
- Economic radius `r_econ < r_max`: where delivered falls below the surplus needed
  to justify the haul.

### Land vs water is the whole story
`mode(LinkType)` makes water an order of magnitude cheaper — a barge or coaster is
not eaten by its cargo the way an ox-team is:

- Land ≈ 1.0 (the tyranny); River ≈ 0.1; Maritime ≈ 0.05 (illustrative).

The engine already carries `LinkType {Land, River, Maritime}`,
`transit_terrain_cost`, and `infrastructure_bonus` on every `ProvinceLink` — so
`r_econ` is computable from existing structure. Roads (`infrastructure_bonus`)
extend the land radius: the grounded reason a society sinks surplus into roads.

### The emergent chain (why castles and lords exist)
1. **Catchment.** A settlement is fed only by the surplus within its `r_econ`, net
   of what the oxen eat bringing it in — a **spatial cap** on non-farm population,
   the complement to the temporal wall.
2. **Localism → decentralization.** Land grain cannot be centralized, so a large
   central court/army cannot be fed from a distant breadbasket. The solution that
   emerges: site a fortified surplus-store + a lord + a garrison **at each local
   surplus locus**, fed from that locus. Power fragments to the granularity of the
   haulage radius — that fragmentation *is* feudalism, emergent, not scripted.
3. **Protect the grain → the garrison loop.** A concentrated grain store is a
   target (raiders, rivals, bandits). The garrison defends it and is fed from the
   same local surplus. The lord's tithe is the conserved transfer that converts
   surplus → protection; the protectable surplus sets the garrison size, which sets
   local military power. Loop: surplus → garrison → protection of surplus.
4. **Water makes cities.** River/coastal provinces have a large `r_econ` → they
   aggregate distant surplus → support towns, markets, a merchant class; landlocked
   provinces stay manorial/castle-bound. The riverine-city / inland-manor spectrum
   falls straight out of geography × haulage.

### Architecture mapping
- New Tier-1/2 step **`grain_logistics`**: from each province's *earned* surplus
  (§2, subsistence), compute deliverable grain to neighbours across `ProvinceLink`s
  (`k_eff` per link), conserving ox-consumed grain. Output per province: **net
  feedable surplus** = local surplus + Σ(delivered-from-within-radius).
- Net feedable surplus **replaces** local surplus as the input to the freed-
  specialist / genesis gate (§2, §4): a town/castle's size is its catchment's
  earned, *deliverable* surplus.
- Castle/manor genesis (§4) sites a fortified node at a surplus locus; garrison =
  surplus-fed soldiers; tithe (§5) funds it; a province **security** value (rising
  with garrison, falling with predation) governs how much stored surplus survives.

### Respects the wall, doubly
The hauled grain is still the *earned* surplus of §2–§3 — no surplus, nothing to
haul, no castle. The ox-cart limit only *adds* a spatial constraint; it never
softens the temporal wall. A subsistence-locked or stranded-inland province
supports no garrison and no lord — correctly. **Two inviolable gates now bound the
medieval economy: can the society produce surplus (time), and can that surplus
reach here before the oxen eat it (space)?**

> The ox-cart is the **floor** of a cross-era law: the logistics/communication
> radius sets the maximum scale of centralized authority. The same mechanic, with a
> wider radius, gives kingdoms, then empires, then the global order — until it hits
> its permanent **ceiling**, the speed of light, and re-fragments humanity into
> sovereign, diverging star systems (era 17). Build `grain_logistics` (M2) as the
> general "link → deliverable fraction + latency" case so the space age extends it
> rather than duplicating it. See `EconLife_Logistics_and_Political_Scale_v01.md`.

---

## 4. Entity genesis for the band

> **Scale note (decided at M3):** history-gen runs at COHORT scale (~30M pop over a
> 12,000-yr climb), so the *aggregate* town economy is carried by
> `cohort_stats.urban_population` (M3, shipped) — NOT by per-firm entities. The
> individual workshop/guild-shop/manor/castle records below are the **player-entry
> (M7)** materialization: when a region is promoted to full LOD, its entities are spun
> up from the cohort aggregates (urban_population, net_feedable_surplus, proto-capital).
> The genesis *gating logic* below is what entry uses; it does not run per-firm during
> the climb.

Extend `business_lifecycle` genesis (today: flat `residents / denominator`,
founder-funded, modern-only) with a **pre-market genesis path** for the
`feudal`/`mercantile`/`industrial` regimes (at entry / full-LOD):

- **Target is freed specialists from the catchment, not headcount.** Supportable
  workshops in a province = `freed_specialists / workshop_labor_denominator`, where
  `freed_specialists` derives from the province's **net feedable surplus** (§3.5) —
  local surplus plus what neighbours can deliver before the oxen eat it.
  Subsistence-locked *or* surplus-stranded-inland province → 0 freed specialists →
  0 workshops. (Doubly wall-gated, time and space, by construction.)
- **Founder-funded from proto-capital.** A founding specialist commits their
  accumulated proto-capital as the workshop's seed cash (no money minted — the
  existing genesis invariant). Insufficient proto-capital → no formation.
- **Era-appropriate firm types.** Not corporations: **workshop** (artisan
  household producing a craft good), **guild-shop** (workshop admitted to a
  guild — quality/price privileges, entry cost), **manor** (agricultural estate;
  the feudal production unit binding land + peasant labor + lord), and **castle**
  (a fortified surplus-store + lord + garrison sited at a surplus locus per §3.5;
  garrison size bounded by the locally feedable surplus). Firm "profile" set from
  the era, reusing the existing `NPCBusiness` record where it fits.
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

- **Manorialism & the garrison loop.** Manors bind peasant labor to land; output
  is the subsistence harvest, but a **tithe/rent** fraction flows from peasant
  producers to the lord (a conserved transfer, not minted). The lord spends that
  surplus feeding a **garrison** that protects the local grain store (§3.5's
  "protect the grain"): surplus → garrison → security → less surplus lost to
  predation → more surplus. The tithe is thus not arbitrary extraction — it is the
  price of protection the haulage limit makes unavoidable, and the seed of the
  concentrated capital and inequality medieval politics runs on. Grounded in
  existing surplus + an ownership link + a province `security` value.
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

## 5.5 Limiting factors — food, the world-classification hazards, and war

Premise (analysis, 2026-06-25): real population history is *two steps forward, one
step back* — the Black Death erased ~40% of Europe and the curve plateaued
1300–1500; wars, quakes, and lean years carved repeated dips. The current model
grows **monotonically** to the food ceiling: the **food** check (Malthusian
births/deaths + drought/flood harvest hits) is well grounded, but every other
limiter is flat or absent. Bring in the full limiting-factor set so the curve gets
its real shape and a society can fail in more than one way (§0, "not all societies
survive").

### The seven world-classification hazards (bring each in distinctly)
The world classification (`world_class.h`, see
`EconLife_World_Spectrum_and_Evolution_Plan.md`) already defines seven hazard dials
that compute a world's Class (Earth ≈ 12). **Today all seven collapse into ONE
constant** cohort-mortality multiplier (`hazard_mortality_from_settings`), plus
seasonality acting on food — a flat background, the same every tick. Give each its
own grounded, often episodic, expression:

| Hazard | Today | Grounded expression to bring in | Kind | Conquerable? |
|---|---|---|---|---|
| **disease** | flat scalar | density-dependent **epidemics** (see below) | episodic shock | yes (medicine) |
| **geology** | flat scalar (+ drought/flood) | **disasters**: quakes/storms/wildfires/floods scaled by the dial — population + infrastructure + grain-store hits | episodic shock | partly (engineering) |
| **seasonality** | food penalty | keep the chronic food penalty **+ episodic harvest failures** (bad years → famine risk) | episodic shock | yes (storage/irrigation) |
| **gravity** | falls in the scalar | falls/accidents **+ haulage cost**: heavier g → oxen carry less, tire faster → larger `k_eff` → **smaller grain radius → more feudal fragmentation** (§3.5); higher construction cost | chronic + coupling | **no** (planetary) |
| **predators** | flat scalar | a check on **dispersed populations and livestock/herds — including the draft oxen** (predation = food + haulage cost); strongest early, **wanes** as density/tech clears them | chronic, waning + coupling | yes (clearance) |
| **radiation** | flat scalar | chronic mortality **+ fertility depression** (lowers `birth_surplus` → a permanent drag on the carrying ceiling) | chronic | **no** (planetary) |
| **atmosphere** | flat scalar | chronic habitability drag: ongoing mortality/health + a **cap on carrying capacity** (a toxic world supports fewer people / needs more tech) | chronic | **no** (planetary) |

Three structural groupings fall out:
- **Episodic shocks** (disease, geology, seasonality) — the population-curve *dips*,
  almost entirely missing today. This is what makes the curve stepped, not a ramp.
- **Feudal couplings** (gravity→haulage, predators→herds/oxen) — hazards that bind to
  the *economy* (the §3.5 ox-cart radius, the livestock that pull the carts), not just
  to a death rate. The novel, on-theme ones: a heavy/predator-ridden world is more
  fragmented and harder to provision.
- **Chronic drags** (radiation, atmosphere, gravity-falls) — the persistent ceiling
  on a world's population/health (roughly today's scalar, but split so each is legible
  and can interact).

And a key axis for the development story: **conquerable vs. persistent.** Disease,
predators, seasonality, and (partly) geology *wane with technique* — predator
clearance, storage/irrigation, medicine, engineering — so overcoming them is the arc
of a thriving world (and feeds the modern release, below). Gravity, radiation, and
atmosphere are *planetary* — you cannot tech them away, so they permanently shape a
world's ceiling and character. A Class-12 world earns its progress against the
conquerable hazards while living forever with the planetary ones.

### Disease — density-dependent epidemics (the disease dial × crowding)
- **Driver:** the world's `disease` dial sets baseline epidemic propensity; outbreak
  hazard then rises with **population concentration** — the freed-specialist/town
  density of §3.5–§5 and trade contact along the haulage network (rivers/ports are
  vectors). A dispersed countryside is relatively safe; a packed town or river hub is
  not. Disease becomes a **natural brake on urbanization**, emergent from the same
  concentration the ox-cart produces.
- **Conserved:** an outbreak removes people from affected cohorts, decays, and
  propagates to linked provinces (one-tick cross-province). Towns periodically cull
  back toward the countryside → urbanization advances in *waves*, not a ramp.

### War — rational polities, not scripted raids (the §3.5 loop, made political)
War is *endogenous* (not a world dial): the protect-the-grain garrison loop made
lethal, and made **political**. It is not random raiding — it is rational polities
competing for conserved grain under physical reach (the ox-cart) and the balance of
power, with **reputation** pricing every broken promise. A polity attacks when the
expected value of seizing a weak/rich/**reachable** neighbour beats peace; allies when
a hegemon threatens (a self-correcting balance of power); signs **treaties** that hold
while peace pays; and **backstabs** when the off-guard prize exceeds the relationship
plus the reputation hit — becoming a shunned pariah if it does so too often. Grain,
people, and territory only move or are consumed (casualties from cohorts, combatants
first; seized grain to the victor). Generalises the just-grounded criminal
territorial-conflict engine to the polity level. **Full model:
`EconLife_War_and_Diplomacy_v01.md`** (treaties, alliances, politics, backstabbing —
all emergent from EV decisions, none scripted).

### The modern release (why the hockey stick)
The *conquerable* limiters are the grounded cause of the post-industrial explosion:
medicine/sanitation collapse disease, the Green Revolution lifts food and tames
seasonality, engineering blunts geology, and relative great-power peace cuts war — so
the modern curve rockets (real 1B→7B, 1800→2010) because the brakes come off, not
because a growth constant was raised. (The persistent planetary hazards remain — an
off-Earth colony never escapes its gravity/atmosphere.) The dawn lab stops at the
modern era, so this release is verified in the modern population module — the analysis
flagged the sim's industrial→modern rate at ~10× below real modern peak.

### Invariants
Conserved (deaths from cohorts; seized grain transferred); deterministic (all shock
rolls via `DeterministicRNG`, canonical order); **wall-respecting** — these are
*reduce-only* (they cut population/surplus, never reserve or prop up a class), and the
chronic drags only lower the ceiling, never raise it. Gated by the behavioral suite
(they move demographics) and re-validated against the World-Class/Bounty spectrum.

---

## 6. Content to author (the data gap)

Data-driven via existing catalogs (`era_available` already supported); the bulk
of the work, lowest engine risk. A minimal but coherent medieval set:

- **Goods (`era_available ≤ 5`):** grain, flour, bread, ale, wool, cloth,
  timber, charcoal, iron ore, iron, tools, leather, pottery, salt, and **draft
  animals / fodder** (the ox of §3.5 — an economic actor whose grain/fodder
  consumption *is* the haulage cost). Plus **backfill** the handful of basics that
  logically predate modernity (grain, timber, ore) to earlier eras so the agrarian
  arc isn't empty.
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
- **Plague-scarred / war-torn** (§5.5): a world that climbed but was culled by an
  epidemic or overrun for its grain — depressed population, a setback or ruin amid
  the remains of a richer past. The non-food failure modes; legible at entry.

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

1. **M1 — Earned surplus (engine, D1). ✅ SHIPPED 2026-06-25.** food_mult cap 6→40
   (agricultural tech tree fully expressive); era thresholds re-calibrated. A
   productive society (earthlike) shows freed-specialist pulses after the era-5/era-7
   ag revolutions (medieval 26%, industrial 44%); marginal worlds stall before
   medieval and earn nothing. Spectrum preserved; modern ~yr 11,970. **No wall
   infringement** (only the ceiling moved). Gates green.
2. **M2 — Grain logistics (engine, §3.5, D6). ✅ SHIPPED 2026-06-25.** New
   `grain_logistics` module: subsistence publishes absolute `grain_surplus`; the
   module computes per-province `net_feedable_surplus` via a conserved one-pass
   allocation (each province distributes its surplus across self + neighbours weighted
   by the ox-cart delivered-fraction; the team eats `(1−df)` of each haul;
   `delivered + eaten == exported`). Water (river/maritime) out-delivers land ~10–20×;
   mountains and heavy gravity (§5.5 coupling) shrink the radius toward 0; roads widen
   it. Dawn-gated; **publish-only** (consumed by genesis in M3), so the M1 climb is
   unchanged. Distance is single-hop (multi-hop + centroid distance are the D6
   refinement). Unit-proven (water>land, conservation, stranded-keeps-local);
   society/emergence gates green.
3. **M3 — Catchment urban population (aggregate town economy). ✅ SHIPPED 2026-06-25.**
   Architectural correction: history-gen runs at COHORT scale (~30M pop), so spawning
   per-firm entities here is wrong — individual workshop/guild-shop/manor/castle
   entities materialize at **player entry (M7)** per the history-gen plan. M3 instead
   consumes `net_feedable_surplus` (M2) into a per-province `urban_population` (= the
   catchment surplus / per-capita food, capped at population) — the aggregate medieval
   town economy. River/coastal hubs grow towns, stranded inland stays rural
   (unit-proven). Conserved, dawn-gated, **publish+observe** (M1 climb unchanged).
   *Finding:* in the current food-uncapped calibration urbanization PULSES with
   surplus and peaks in the growth eras (classical ~17–23%), sitting Malthusian-pinned
   (~4%) at equilibrium — consistent with the inviolable wall (towns need real
   surplus). Tuning the medieval urban *level* (a bigger/longer ag-revolution pulse)
   is a wall-respecting calibration follow-up. Gates green.
4. **M4 — Medieval content. ✅ SHIPPED 2026-06-25.** Authored the era-≤5 content:
   6 backfilled basics (wheat/flour/lumber/iron_ore/salt/beer) + 10 new goods
   (bread, wool, wool_cloth, charcoal, wrought_iron, iron_tools, raw_hide, leather,
   draft_oxen, fodder); 8 recipes forming real chains (grain→flour→bread, grain→ale,
   wool→cloth, hide→leather, wood→charcoal, ore+charcoal→wrought_iron→tools); 9
   facility types (watermill/bakery/brewery/weaver/tannery/charcoal_kiln/bloomery/
   smithy/manor_farm). All labor-only (no anachronistic engine power). recipe↔goods
   cross-validation clean; regression test asserts the set is available at an era-5
   start and modern-only goods (steel) are not. The production economy that consumes
   this content is instantiated at player entry (M7); gates green.
5. **M5 — Feudal layer: manorialism. ✅ SHIPPED 2026-06-25.** The defining
   "behaves distinctly from the commons" mechanic: in the stratified regimes
   (feudal/mercantile/industrial) a tithe concentrates the new proto-capital toward a
   lord stratum instead of the commons' egalitarian even split — conserved (a skewed
   distribution of the SAME proto-capital), climb-preserving (only wealth distribution
   shifts). Emergent arc observed (capital gini): founding 0.42 → the commons flattens
   to ~0.26 → feudal/mercantile re-stratify to ~0.37–0.39. The lord's concentrated
   capital is the founder war-chest for entry genesis. Gini surfaced in the history
   observe. *Deferred:* the garrison/security loop folds into **M6** (where war/
   predation consume security); guild entry/quality/price and towns/fairs are the
   **M7** entry-materialization (firm/market layer). Gates green.
6. **M6 — Limiting factors: hazards + war (§5.5, D8/D9).** Bring the seven
   world-classification hazards in distinctly (split the flat scalar): the episodic
   shocks — disease epidemics (density × the disease dial), geology disasters,
   seasonality harvest failures; the feudal couplings — gravity→haulage radius,
   predators→herds/oxen; the chronic drags — radiation→fertility, atmosphere→ceiling.
   Plus endogenous **war & diplomacy** — NOT scripted raids but rational polities
   competing for conserved grain under reach (ox-cart) and the balance of power, with
   reputation pricing broken faith; treaties, alliances, politics, and backstabbing
   all emerge from EV decisions (full model:
   `EconLife_War_and_Diplomacy_v01.md`, decisions W1–W5). All conserved, reduce-only
   (wall-respecting). Sequenced here because the couplings need M2/M3 (haulage, towns)
   and war needs the M5 garrison/tithe. Split: **M6a** episodic shocks + chronic
   hazard split; **M6b** feudal couplings; **M6c** war & diplomacy (the largest — a
   polity-level diplomacy engine generalising the criminal conflict engine).
   **✅ M6a EPISODIC SHOCKS SHIPPED 2026-06-25:** the three episodic hazards, each an
   independent stochastic check gated to the pre-market arc (the modern era releases
   them via medicine/engineering/storage):
   - **disease** — mortality spike rising with the `disease` dial AND urban crowding
     (towns are vectors → brakes urbanization); in population_aging.
   - **geology** — quake/storm/wildfire mortality spike scaled by the `geology` dial
     (not density); in population_aging.
   - **seasonality** — episodic bad-harvest output cut scaled by the `seasonality`
     dial (seeded by YEAR so a failure is consistent across the year at any tick
     resolution); in subsistence, on top of the chronic penalty.
   All conserved (deaths via the cohort path / less food produced), deterministic,
   reduce-only. Cumulative calibration drift modest: modern 11,970→12,349 (+3.2%),
   shape intact (medieval 978 vs 950) — the hazards realistically slow the climb. A
   single threshold re-calibration to restore modern ~12,000 is **deferred until the
   full hazard suite + war land** (chasing it mid-suite is a moving target).
   **✅ M6a CHRONIC + COUPLINGS SHIPPED 2026-06-25:** predators→herd/food penalty
   (waning as knowledge clears them), atmosphere→carrying-cap (planetary),
   radiation→fertility (planetary, applies in all eras). gravity→haulage was already
   in M2's `delivered_fraction`. So all seven world-class hazards now act through their
   signature channels (additive to the background mortality scalar — radiation kills
   AND suppresses births, etc.). Earth-level penalties kept small (they compound over
   the 12,000-yr climb): earthlike modern 11,970→12,822 (+7% cumulative M6a drift),
   anchor holds (Thriving), spectrum intact (garden stagnates, fertile-deathworld
   fastest, barren-deathworld stalls, none extinct). **A single threshold
   re-calibration to restore modern ~12,000 is deferred until after M6c war** (and
   must follow war so earthlike doesn't stall under the combined load). M6a DONE
   except multi-year/spread epidemics (optional refinement). Next: M6c war.
   *Deliverable:* the curve gains realistic dips/plateaus; plague/quake/famine/war
   become spectrum failure modes; shifting alliances and the rise/balancing of powers
   read as real history; the World-Class spectrum bites harder. Behavioral gate
   re-validated.
7. **M7 — Entry & verbs.** Era selection surfaces the world's earned state;
   medieval player verbs. *Deliverable:* a playable medieval start.

M1 is the linchpin and the riskiest (spectrum rebalance); everything downstream
is inert without it, and it is the only milestone that touches the food/population
core — which it does *by raising the ceiling, never by softening the wall.*

---

## 11. Open decisions (need ratification before build)

- ~~**D1 (blocking):** food-productivity cap/curve so era food techs bite~~ —
  **RESOLVED & SHIPPED (2026-06-25, M1).** food_mult cap 6→40 (tech tree fully
  expressive); era thresholds re-calibrated. EARTHLIKE earns a 26% medieval urban
  class (surplus pulse from the heavy-plough/three-field revolution) and reaches
  Modern ~yr 11,970; marginal worlds stall before medieval and earn no urban
  economy. Wall intact (only the ceiling moved). See the session log
  (emergence_baseline_2026-06-10.md, 2026-06-25 M1 entry).
- **D2:** is `agricultural_capital` (land improvement as a located stock) in scope
  for M1, or is knowledge+food-tech enough to drive the first surplus pulses?
- **D3:** manorial ownership model — reuse `NPCBusiness` (manor as a firm with a
  land endowment) or a dedicated estate entity? (Reuse preferred.)
- **D4:** guild representation — a new lightweight per-craft/per-town record, or
  encode guild state on existing business/market fields? (Lean lightweight.)
- **D5:** does the mercantile band (era 6) reuse this genesis path wholesale
  (workshops→firms, guilds→early companies), confirming the design generalizes
  before we commit to it?
- **D6 (§3.5):** grain-logistics calibration — `k_base`, `trip_factor`, the
  `mode`/`terrain`/`road` factors (land vs river vs maritime), **and the gravity
  coupling** (heavier g → larger `k_eff` → smaller haulage radius, §5.5). Does
  haulage distance use `ProvinceLink.shared_border_km` (a border length, not a travel
  distance — a poor proxy) or a province-centroid distance that world-gen must
  expose? Centroid distance likely required.
- **D7 (§3.5/§5):** the "protect the grain" predation model — reuse `random_events`
  raiding plus a province `security` value fed by garrison size, or a dedicated
  raiding/banditry loop? Either way, lost grain must be conserved (it goes to the
  raider, not to nothing).
- **D8 (§5.5):** epidemic model — outbreak hazard as a function of which density
  signal (town/freed-specialist concentration, trade-link contact, both) × the
  `disease` dial? Add an `epidemic` template to `random_events` (era-gated) or a
  dedicated disease module? Propagation along `ProvinceLink`s (rivers/ports as
  vectors) — in-scope for v1 or single-province first?
- **D9 (§5.5):** war-mortality model — generalize the criminal territorial-conflict
  engine (now exposed per-province) to inter-polity grain-conflict casualties + grain
  seizure, or a separate agrarian-warfare loop? Casualty draw order (combatants
  first) and the security/military-power comparison that triggers it.
- **D10 (§5.5):** splitting the flat hazard scalar into the seven distinct dials —
  scope and ordering. Episodic shocks: geology→disasters (scale `random_events`
  frequency/severity by the `geology` dial) and seasonality→episodic harvest failure
  (on top of the existing chronic food penalty). Chronic split: radiation→`birth_surplus`
  (fertility) and atmosphere→carrying-capacity cap, peeled out of the single
  `hazard_mortality_from_settings` multiplier. Coupling: predators→herd/livestock
  (incl. the draft oxen) loss, waning with density/tech. Which land in M6a vs M6b, and
  re-validate the World-Class spectrum after each (this directly reshapes it). Keep
  reduce-only/wall-respecting.

---

## 12. One-paragraph summary

Medieval is thin because only the abstract commons loop runs there and 100% of the
goods/recipe/firm content is modern-anchored. The fix is to make the medieval
economy a *grounded, conditional* function of the surplus a society actually earns —
bounded by **two inviolable gates**. In **time**, the Malthusian wall: the commons
module already computes freed specialists and proto-capital, both zero without real
productivity gains, so gating workshop/guild/manor/castle genesis on them makes the
wall the gate for the entire medieval layer (no reserved fractions, no population
damping, no restoring force). In **space**, the tyranny of the ox: a draft team eats
the grain it hauls, so surplus has a hard economic radius — grain can't be
centralized over land, which is precisely why castles, lords, and garrisons emerge
to concentrate and protect surplus *locally* and feed the protectors from it. Water
(rivers/coast) breaks the radius and makes cities; landlocked surplus stays
manorial. Societies that break the wall upward through technique *and* can deliver
their grain get towns, guilds, and cities; those that can't stay subsistence-locked,
stranded, or stagnant. On top of the two gates sit the limiting factors that carve
the curve's real shape (§5.5): the seven world-classification hazards brought in
distinctly — episodic shocks (plague, quake, lean year) that dip the curve,
planetary drags (gravity, radiation, atmosphere) that cap it, and couplings
(gravity→haulage, predators→herds) that bind the world's nature to its economy — plus
endogenous war over the grain. A player entering in 500 CE meets whichever world this
one became: rich or stranded, thriving or plague-scarred, free or overrun.
