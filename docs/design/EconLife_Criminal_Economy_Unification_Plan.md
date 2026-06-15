# Criminal Economy Unification Plan

**Status:** Proposed (planning). Authored 2026-06-14.
**Goal:** Ground the criminal *production* economy in the same conserved, located,
recipe-driven physical chain the legitimate economy now obeys (resource-economy arc
P1–P4) — retiring the revenue-scalar proxies and enum-keyed proxy markets.

This plan is the source of truth for the effort; update it as phases land.

> **Update 2026-06-15 — reprioritized around the crime *balancing loop*.**
> Phase 1 (real drug good-ids; commit `fe35ed6`) landed and is gate-green, but the
> long-horizon diagnostic ran away: realistic drug prices exposed that **crime has no
> negative feedback**. The detection→prosecution→imprisonment loop jails *individuals*
> but never touches the *enterprise* — production/drug modules don't check operator
> status, and a lucrative criminal business never goes bankrupt. So crime grows
> unchecked everywhere, and the corruption-modulated detection variance is toothless.
> The fix is NOT a revenue cap (a fake rail) but **closing the balancing loop**:
> enforcement must suppress the criminal economy, with strength set by governance/
> corruption, so crime-infested vs well-managed provinces emerge (Juárez vs Singapore).
> **Keystone (this update):** an imprisoned operator drops their criminal enterprise to
> a resilience floor (`DrugEconomyConfig::operator_imprisoned_output`, default 0.5) —
> the org keeps running on a deputy and recovers on release (organized crime is hard to
> dismantle by decapitation). Crime suppression then = imprisonment rate = enforcement
> strength = governance. Implemented for `drug_economy` first; extend to weapons/
> rackets/criminal-production next. This balancing work now leads; the conservation
> phases below continue on top of it.

---

## 1. Principle

Same as the legit economy: **matter is neither created nor destroyed, only moved or
transformed**, and **everything is downstream of located resources**. A criminal good
(cocaine, heroin, meth, cannabis, weapons) must be *produced from real inputs via a
recipe in a facility*, drawing those inputs down from located stock — not conjured from
a scalar multiple of the business's revenue.

The enforcement loop (detection→prosecution→imprisonment) and the grey-zone/concealment
systems (laundering, regulatory scrutiny, visibility/signals, alternative-identity) are
already mature and are **out of scope** here — this is purely about grounding criminal
*production*.

---

## 2. Current state (grounded findings)

### 2.1 The drug economy is a parallel proxy economy
`drug_economy_module.cpp`:
- Output is a proxy: `production_output = revenue_per_tick * 0.1f` (not recipe/facility
  driven).
- Drug supply is keyed by the **`DrugType` enum value cast to a good_id**
  (`supply_delta.good_id = static_cast<uint32_t>(drug_type)`), values 0–3.
- Addiction demand is written to **`good_id = 0`** ("aggregate drug demand").
- Drug "price" is read from `market_index_by_good_province[(drug_type<<32)|prov]`.

**BUG (collision):** real catalog good numeric_ids start at 0 (`iron_ore=0`,
`copper_ore=1`, `bauxite=2`, …). So the drug economy **writes drug supply into, reads
"drug price" from, and writes addiction demand into the iron_ore / copper_ore / bauxite
markets.** The drug economy silently corrupts the first real goods' markets. (Real drug
goods — `cocaine`, `heroin`, `methamphetamine`, `cannabis_processed` — exist in
`goods_tier4.csv` with proper catalog ids and get markets at world gen, but the proxy
never uses them.)

### 2.2 The real chain already exists as data (unused)
`recipes_*.csv` already define the full conserved chain:
- `coca_cultivation` (plantation) → `coca_leaf`; `poppy_cultivation` → `poppy`.
- `cocaine_synthesis` (chemical_plant): `coca_leaf ×4 + sulfuric_acid ×0.5 → cocaine`.
- `heroin_synthesis`: `poppy ×3 + sulfuric_acid ×0.3 → heroin`.
- `methamphetamine_synthesis`: `drug_precursors ×2 → methamphetamine`.
- `drug_precursor_synthesis`: `naphtha ×2 + sulfuric_acid ×0.5 → drug_precursors`.
- `cannabis_processing`: `cannabis_raw ×3 → cannabis_processed ×2`.
- **Missing:** a `cannabis_cultivation` recipe (so `cannabis_raw` is never produced).

These run *conserved* through the production module when a criminal business owns a
facility of the right type with the recipe assigned. `coca/poppy → cocaine/heroin` are
**EX-reserved in `DrugType`** so `drug_economy` never touches them; they already flow
through production when seeded. The production module already prices criminal businesses
on the informal layer (`get_price_for_business` → `informal_price_discount`).

### 2.3 Already landed (slice 1)
Synthetic drugs (meth/synthetic/designer) in `drug_economy` now consume **real
`drug_precursors`** (availability-bottlenecked, debited) — the `9999` placeholder is
gone (commit `946b2af`). Cannabis still uses the proxy.

### 2.4 Weapons are entirely fictional
No `small_arms`/`firearms`/`ammunition` good exists in the catalog; no weapons
manufacturing recipe exists. `weapons_trafficking` diverts `revenue×0.01` into an
**enum-keyed** informal market (`WeaponType` cast to good_id) and never debits any
formal stock. The "double-count" is really pure conjuring; grounding requires
*creating* the goods + recipes first.

### 2.5 designer_drug
`designer_drug_module` deducts R&D cost but **credits no revenue** and injects a
hardcoded `BASE_SUPPLY_PER_TICK` — magic supply, net loss to the org.

---

## 3. Target architecture

```
located biological resource ──cultivation recipe──▶ raw precursor (coca_leaf / poppy /
   (climate/soil-suited)         (production)          cannabis_raw)  + chemical precursors
                                                            │
                                            synthesis recipe (production, conserved:
                                            consumes precursors + acid, informal-priced)
                                                            │
                                                            ▼
                                              drug good (real catalog id: cocaine /
                                              heroin / methamphetamine / cannabis_processed)
                                                            │
                              drug_economy = DISTRIBUTION ONLY: quality/purity degradation,
                              wholesale→retail tiers, addiction demand & seeding, legalization,
                              evidence — all keyed by REAL good ids.
```

`drug_economy` stops *producing*; production becomes the sole, conserved producer.
Weapons get the same treatment once real goods/recipes exist.

**Key invariant to preserve:** criminal businesses must reliably own the right facility
chain (cultivation + synthesis) so the conserved chain actually produces — replacing the
"every criminal business makes revenue×0.1 of drugs unconditionally" proxy.

---

## 4. Cross-cutting risks & invariants (apply to every phase)

1. **World-gen RNG stream is fragile.** `create_markets` runs *before* facilities/
   population/tech and consumes RNG per good; `facility_generator` consumes RNG per
   random recipe pick. **Adding a good or recipe shifts the whole downstream world**
   (this cratered legitimacy in the waste pass). Mitigations: append new goods at the
   end of the last goods file; seed new markets **RNG-neutral** (as waste/utility goods
   are); for new recipes, prefer deterministic/assigned selection over random picks, or
   accept the world shift and revalidate emergence as the gate.
2. **Collapse risk.** Gating a drug on a precursor that isn't produced zeroes its output.
   Cannabis is the fallback majority — gating it without seeded cultivation collapses the
   criminal economy. Every gating step must be paired with assured input supply and
   validated against the **criminal-justice loop integration test**
   (`criminal_subsystem_integration_test.cpp`, fast gate) + the emergence suite.
3. **Price/revenue recalibration.** Fixing the good_id collision moves drug pricing from
   iron_ore's ~12 to real drug goods' ~800–2000 → criminal revenue surges. Expect to
   tune (informal discount, wealth ceiling already exist) so criminal wealth doesn't
   re-trigger the runaway we retired earlier.
4. **Determinism.** All new draws via `DeterministicRNG`; canonical sort order; province-
   parallel safety (no shared mutable module state).
5. **Persistence.** New `RegionalMarket`s for new goods are auto-persisted; any new
   per-entity field needs serialize/deserialize + the `delta_buffer_merge` coverage test
   if a new delta type is added.
6. **Gate every phase:** fast gate (1637+), emergence 27/27, criminal-justice loop test.
   Commit per phase; revert cleanly if a phase regresses.

---

## 5. Phased plan

### Phase 1 — Real drug good-ids (fix the collision)  ⟵ start here
**Why first:** it's a real bug (market corruption), it's the foundation for everything
else (the proxy must speak real good-ids), and it's the most contained.

**Changes (`drug_economy_module.cpp`):**
- Add `DrugType → real good string` map (`cannabis→cannabis_processed`,
  `methamphetamine→methamphetamine`, `synthetic_opioid→synthetic_opioid`,
  `designer_drug→designer_drug`).
- Resolve via `lookup_good_id(state, good)`; key drug **supply**, **price lookup**, and
  **addiction demand** by the real good id (not the enum cast / not 0).
- Keep production proxy + distribution as-is otherwise.

**Risk:** drug prices jump to real values → revenue surge. **Validation:** fast gate +
emergence + criminal loop; watch criminal capital. **Tune** informal discount / confirm
the wealth ceiling holds. **Rollback:** revert the keying.

### Phase 2 — Route drug production through the production module
**Why:** make production conserved & recipe-driven; retire `revenue×0.1`.
**Changes:**
- Ensure criminal drug businesses deterministically own the chain facilities
  (plantation+`coca_cultivation` / chemical_plant+`cocaine_synthesis` / etc.) — likely a
  `facility_generator` change that *assigns* drug recipes to criminal businesses by role
  rather than random pick (handles the RNG-shift concern by being deterministic).
- Remove `drug_economy`'s production loop (the proxy output + supply emission). Production
  now emits the real drug-good supply (conserved, informal-priced, consuming precursors).
- `drug_economy` reads the production-produced drug supply for distribution/quality/
  addiction.
**Risk:** high — drug supply becomes fully chain-dependent; if the chain is thin, supply
drops. **Validation:** criminal loop + emergence; verify drug supply > 0 and addiction
sustained. **Rollback:** restore the production loop.

### Phase 3 — Cannabis cultivation + gate cannabis
**Changes:** add `cannabis_cultivation` recipe (plantation → `cannabis_raw`); ensure
criminal cannabis growers exist (Phase-2 assignment); gate cannabis output on
`cannabis_raw` (now produced). All drugs conserved.
**Risk:** collapse of the majority drug if cultivation under-supplies. **Validation:**
criminal loop + emergence; tune cultivation yield / grower count.

### Phase 4 — Weapons: real goods + recipes + diversion conservation
**Changes:** add `small_arms` (and optionally `ammunition`, `heavy_weapons`) goods
(append, RNG-neutral or revalidated) + a `small_arms_manufacturing` recipe (steel/
machined parts → small_arms). `weapons_trafficking` diversion debits the manufacturer's
**real** small_arms output (capped by availability) and adds to the informal layer keyed
by the real good id; retire the `WeaponType`-cast key.
**Risk:** world-gen shift from new goods/recipe; weapons economy behavior change.
**Validation:** emergence + criminal loop.

### Phase 5 — designer_drug revenue + inputs
**Changes:** credit sales revenue to the producing business; source supply from
`drug_precursors` consumption (conserved) instead of the hardcoded injection.
**Risk:** low–medium. **Validation:** emergence + criminal loop.

---

## 6. Open design decisions (resolve before/within each phase)

1. **Producer of record:** confirm production module is the sole drug producer and
   `drug_economy` becomes distribution-only (recommended), vs. keeping a thin producer in
   `drug_economy`. Affects Phase 2 scope.
2. **Facility seeding:** deterministic role-based assignment of the drug chain to criminal
   businesses (recommended, RNG-safe) vs. relying on random recipe picks.
3. **Informal market model:** keep the `spot × informal_discount` approximation, or build
   a real informal market layer (separate supply/price). The latter is larger and may be
   EX scope.
4. **Cross-province precursor flow:** do synthesis hubs import coca_leaf/poppy via
   `supply_chain`, or must cultivation be co-located? Affects whether the chain is viable
   per province.
5. **Calibration targets:** acceptable criminal wealth ceiling / drug price band so
   Phase 1's revenue surge doesn't re-trigger the wealth runaway.

---

## 7. Out of scope
- Enforcement loop and legal process (already closed).
- Grey-zone/concealment systems (laundering, scrutiny, visibility, alt-identity) — except
  where Phase work happens to touch evidence emission keyed by real goods.
- A full informal-market layer (decision #3) unless explicitly pulled in.
