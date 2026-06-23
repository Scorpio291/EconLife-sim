# Fake-Rail Remediation Plan

Status: proposed (2026-06-22). Companion to the audit in this session and to
`docs/session_logs/emergence_baseline_2026-06-10.md`.

A "fake rail" is any mechanism that substitutes a hardcoded value, a restoring
force toward a constant, a conservation violation, or a cross-module proxy for an
outcome that should EMERGE from grounded, conserved, located inputs. This plan
sequences the rails found in the audit into phases by leverage and risk, and for
each one names the concrete grounded signal that already exists (or must be
created) to replace it.

## Guiding rules (apply to every fix)
1. **Grounded inputs only** — replace a constant with a real signal already on
   `WorldState`/`Province`/`NPC`/markets, or create the missing signal explicitly.
2. **Conservation** — anything produced is drawn from a source and pays a cost;
   anything consumed is deducted from a located stock.
3. **Spec wins** — where code diverges from `docs/interfaces/.../INTERFACE.md`,
   update code to the spec (or amend the spec through review), never silently keep
   the divergence.
4. **Determinism** — no new entropy; canonical sort order on accumulations.
5. **Gate every change** — fast gate (`ctest -LE emergence`) per change; behavioral
   gate (`ctest -L emergence`) for anything touching demographics, knowledge,
   stability, or R&D, since those feed the emergence baseline we just calibrated.
6. **Data-driven** — new tunables go in CSV/JSON config, not literals in `.cpp`.

## Correction to the audit
- **Modern era progression is NOT calendar-forced.** Transition needs
  `score >= era_transition_threshold` (0.70, config). Calendar contributes at most
  0.40 per era (`technology_module.cpp:69-86`), so it cannot trigger a transition
  alone — node maturation / domain-knowledge triggers must supply the rest
  (`technology_module.cpp:88-194`, threshold `technology_types.h:210`). This is a
  pacing gate, not a rail. No fix needed; the related work is grounding R&D speed
  (Phase 3) so the tech contribution is itself real.

---

## Phase 0 — Corrections & quick wins (low risk)

### 0.1 Production quality floor (spec divergence) — `production_module.cpp:628`
- **Now:** when the actor lacks the recipe's `key_technology_node`, quality is
  floored at `0.1` and production proceeds — i.e. goods made from tech you don't
  have. The code comment admits the spec says quality should be 0.
- **Fix:** honor the spec — `maturation_of()==0` → quality ceiling 0 (no output),
  OR, if "informal/low-grade production without the formal node" is intended,
  amend `docs/interfaces/production/INTERFACE.md` to define it and make the floor a
  named config (`unteched_quality_floor`) rather than a literal.
- **Decision needed:** is no-tech production meant to be possible? Default: spec
  wins → 0.
- **Test:** unit — recipe requiring an absent node yields zero output.

### 0.2 Document/accept the non-rails
- `banking` `credit_score=0.5` starting prior, `real_estate` `[0.1,5.0]` value
  clamp, `community_response` stage thresholds, currency-exchange policy clamps:
  keep, but move bare literals to config and add a one-line justification comment
  so future audits don't re-flag them. No behavioral change.

---

## Phase 1 — Conservation fixes (clearest grounding wins)

### 1.1 Designer-drug supply from nothing — `designer_drug_module.cpp:55,149`
- **Now:** `supply_delta = 10.0 * market_margin_multiplier` conjured each tick; no
  precursor, no cost.
- **Grounded path (reuse existing chain):** `drug_economy_module.cpp:147-294`
  ALREADY models precursor→production→supply, bottlenecked on real precursor market
  stock (`regional_markets[idx].supply`, consumed via negative `MarketDelta`).
  Designer compounds already resolve a creator business
  (`compound.creator_actor_id` → `npc_businesses`, used for R&D cost deduction at
  `designer_drug_module.cpp:70-84`).
- **Fix:** drive compound supply through the same precursor mechanism:
  output = min(business production capacity, `precursor_supply / ratio`); emit the
  drug `MarketDelta` for output AND a negative `MarketDelta` for precursor consumed.
  Add a compound→precursor ratio (extend `precursor_for_drug()` to cover designer
  compounds; default to `drug_precursors` at ratio 1.0, which already exists).
- **New/needed:** a per-compound precursor ratio (config or reuse the generic
  `designer_drug` mapping). No new entity required.
- **Risk:** low; isolated criminal subsystem. **Test:** unit — zero precursor stock
  → zero compound supply; positive stock → output bounded by stock and precursor
  market shows the matching drawdown (conservation check).

### 1.2 Addiction spend hardcoded — `addiction_module.cpp:90,199,331`
- **Now:** `substance_spend_for_stage()` returns 5/15/30/50/20; deducted from
  `npc.capital` if affordable, regardless of whether the drug is actually for sale.
- **Grounded path:** drugs are real goods with a `RegionalMarket.spot_price`.
  `npc.addiction_state.substance_key` (string) → `lookup_good_id` →
  `lookup_market(state, gid, npc.current_province_id)` → `spot_price`.
- **Fix:** `spend = consumption_quantity(stage) * spot_price`; deduct that, and only
  if `market.supply > 0` (you can't buy what isn't there — a supply gap should drive
  `supply_gap_ticks`/craving, which the state already tracks). Add
  `consumption_quantity` per stage to `AddictionConfig` (units/tick — the grounded
  primitive; price comes from the market, not the config).
- **New/needed:** per-stage `consumption_quantity` config; substance_key→good
  validation. **Risk:** medium — couples addiction to drug prices (a real feedback:
  scarcity raises price → addicts priced out → demand signal). **Test:** unit — spend
  tracks price; zero supply → no purchase + widening supply gap.

### 1.3 (Optional, larger) Cannabis precursor unbound — `drug_economy_module.cpp:121`
- **Now:** cannabis bypasses the precursor bottleneck (no cultivation chain).
- **Fix:** add a `cannabis_raw → cannabis_processed` cultivation/processing chain
  (goods already exist in `goods_tier4.csv`) so cannabis is grounded like the other
  drugs. Defer unless we want full drug-economy conservation now.

---

## Phase 2 — Close restoring-force loops — `regional_conditions_module.cpp`

### 2.1 Stability auto-heal — lines 25-31
- **Now:** `stability += rate*(1 - stability)` drifts to 1.0 with no cause.
- **Grounded path (all signals already computed in THIS module each tick):**
  crime_rate, criminal_dominance_index, formal_employment_rate, inequality_index,
  community.grievance_level, infrastructure_rating, institutional_trust, pollution.
  `government_budget` already emits stability deltas from public-services and
  infrastructure spending — so part of the loop exists; the rail is the *extra*
  blind drift on top.
- **Fix:** replace the flat recovery with a target stability computed from those
  signals (e.g. weighted: + employment, + infrastructure, + trust; − crime,
  − criminal dominance, − inequality, − grievance, − pollution), then move stability
  toward THAT target at a bounded rate (the rate stays; the target becomes grounded
  instead of a constant 1.0). Weights to config + INTERFACE.md.
- **Risk:** medium-high — stability feeds migration, mortality, approval, event
  probability. Must run the behavioral gate; expect the spectrum to shift. **Test:**
  unit — high crime/inequality drives stability DOWN even with no events; scenario —
  a province with collapsing employment destabilizes rather than self-healing.

### 2.2 Drought/flood drift — lines 41-45
- **Reframe:** recovery toward 1.0 is legitimate (land recovers after a drought).
  The real gap: `random_events` generates `drought_mild/severe/flood`
  (`random_events_module.cpp`) and applies a stability hit, but never DEPRESSES the
  drought/flood modifier, and its `active_events_` are module-private.
- **Fix (close the loop):** have `random_events` write the agricultural depression
  while an event is active — either via a new `RegionDelta` field
  (`drought_modifier`/`flood_modifier` override) or by exposing a per-province
  "active weather" flag on `Province` that `regional_conditions` reads. Then the
  existing recovery drift becomes the grounded post-event healing.
- **New/needed:** a cross-module signal (delta field or province flag). **Risk:**
  low-medium. **Test:** unit — active drought lowers the ag modifier; after it
  expires the modifier recovers; province ag yield reflects both.

---

## Phase 3 — Ground the R&D engine — `technology_module.cpp:247,251,278`

The maturation formula is
`researchers_assigned * researcher_quality * facility_quality * domain_bonus *
funding_adequacy * rate_coeff`. Three factors are flat stubs.

### 3.1 Facility quality `=1.0` → `Facility.tech_tier`
- `Facility.tech_tier` (`production_types.h:100`) exists and is the natural signal:
  a higher-tier lab matures tech faster. Normalize tier to a multiplier (config
  `tech_tier_quality_scale`). **Risk:** low. **Test:** higher-tier facility → faster
  maturation, deterministically.

### 3.2 Funding adequacy `=1.0` → `funding_per_tick` vs requirement, gated by cash
- `MaturationProject.funding_per_tick` is real (set from NPC business decisions,
  `BusinessDecisionResult.rd_investment_rate`). Compute
  `funding_adequacy = clamp(funding_per_tick / required, 0, 1)` where `required`
  scales with node difficulty/era; gate by `NPCBusiness.cash`.
- **Conservation check:** confirm `funding_per_tick` is actually DEDUCTED from the
  business each tick — if not, that is itself a rail (R&D for free). Wire the
  deduction if missing. **Risk:** medium. **Test:** underfunded project matures
  slower; cash-starved business can't fund; cash actually decreases.

### 3.3 Researcher quality `=0.7` — no grounded signal exists yet
- There is NO researcher NPC role and NO skill field (confirmed). Options, in order
  of effort:
  - (a) Derive competence from the actor's accumulated `domain_knowledge` / business
    `effective_tech_tier` — a firm that knows more researches better. Grounded in
    existing state, no new entity. **Recommended first step.**
  - (b) Introduce a researcher occupation + a skill field and sample real
    researchers per actor/province. Larger; a proper fix but a new subsystem.
- Until (b), make it (a) plus a documented config coefficient — not a bare literal.
  **Risk:** medium. **Test:** knowledge-rich actor researches faster than a novice.

### 3.4 (Maintainability, not a rail) Hardcoded era-specific bonuses — lines 133-194
- Smartphone/cloud/EV/GenAI bonuses are hardcoded in C++. Move to the
  `era_triggers` CSV/config path that already exists, so modern era content is
  data-driven like everything else. No behavioral change intended.

---

## Phase 4 — Remaining proxy signals (the known stubbed cross-module loops)

These match CLAUDE.md's "loops stubbed with proxies/stand-ins" — the open
integration work. Each replaces a proxy with the real producer's signal. Sequence
after Phases 1-3; several depend on signals other modules must first expose.

| Rail | File:line | Replace proxy with | Depends on |
|---|---|---|---|
| LE "heat" from social_capital | `criminal_operations_module.cpp:45` | investigator workload / open investigations | investigator/evidence signal exposed |
| Evidence credibility from social_capital | `evidence_module.cpp:79` | evidence weight + holder track record | evidence type/quality fields |
| Market share from revenue | `antitrust_module.cpp:112` | actual per-good output | production output per business/good |
| Conflict stage from dominance; output=revenue·0.01 | `weapons_trafficking_module.cpp:69,100` | criminal_operations conflict state; recipe output | criminal_operations conflict engine |
| Income tax from mean_income proxy | `government_budget_module.cpp:118` | summed wages actually paid | per-province wage flow |
| Wages = %revenue via scale proxy | `financial_distribution_module.cpp:67` | labor-market wage × headcount | labor_market wage + worker counts |
| LE skill config proxy | `money_laundering_module.cpp:242` | real LE capability signal | LE/enforcement model |
| base_price = equilibrium_price (self-ref) | `price_engine_module.cpp:93` | a tracked base/input cost | a real cost basis on the market |
| Working-age from 200-NPC sample | `healthcare_module.cpp:196` | `cohort_stats` working-age share | already acknowledged; low priority |

For each: define the producer module's output, add it to `WorldState`/delta as a
grounded field, then have the consumer read it. Update the relevant INTERFACE.md.

---

## Phase 5 — Adaptation clamps (review, likely keep with justification)
- `hazard_mortality` clamp `[0.15,3.0]` (`population_aging_module.cpp:246`) and
  `hardiness` clamp `[0.05,5.0]` (`apply_deltas.cpp:461`). These bound generational
  adaptation. Decide: are they physical bounds (keep, document, move to config) or
  do they mask real divergence on extreme worlds (widen/remove)? Given the new
  adversity dynamics depend on `world_hazard`, test whether widening changes the
  world spectrum before changing. **Risk:** medium (feeds the emergence baseline).

---

## Recommended order
1. **Phase 0.1** (spec-divergence quick win) and **Phase 1.1/1.2** (clean
   conservation fixes, isolated subsystem) — highest clarity, lowest blast radius.
2. **Phase 2** (restoring forces) — high value (kills the ratchet-maskers), but run
   the behavioral gate; it moves the spectrum.
3. **Phase 3** (R&D) — the modern-game sequel to the dawn-era grounding.
4. **Phase 4** (proxy loops) — the long tail of known integration work.
5. **Phase 5** (clamps) — review last.

Each phase is independently shippable behind the test gates.

---

# Execution log & later-session focus (updated 2026-06-23)

Phases 0-3 plus two follow-ups are committed. This section records what shipped and,
crucially, **every place that needs deeper focus in a later session** — most of the
Phase 4 long-tail turned out to be *feature-scope* (the grounded signal does not yet
exist or is not exposed), not *rail-scope* (swap a proxy for an existing signal).

## Shipped (rail-scope, grounded + tested)
- Phase 0.1 production quality (spec-honoring), 1.1 designer_drug supply (precursor
  conservation), 1.2 addiction spend (market price).
- Phase 2.1 regional stability target, 2.2 drought/flood depression loop.
- Follow-ups: natural-disaster infrastructure damage; live addiction pricing.
- Phase 3 R&D maturation formula (facility tier, cash-funded, researcher config).
- Phase 4: criminal_operations LE-heat now reads the real investigation pressure
  (peak investigator_meter across the org's members/leadership) instead of local
  police social_capital. (criminal_operations_module.cpp compute_le_heat.)

## NEEDS LATER FOCUS — feature-scope (signal missing or unexposed)

Each item lists the proxy, why it can't be grounded by a swap today, and what must
be built first.

1. **R&D advancement pipeline is unwired** (Phase 3 finding). Nothing creates
   `MaturationProject`s (`active_maturation_projects` is read-only; deferred
   `handle_maturation_advance` is an empty stub). The maturation formula is grounded
   but inert until project creation + researcher assignment + R&D budgeting are
   built. FEATURE.

2. **price_engine base_price self-reference** (`price_engine_module.cpp:~93`). No
   per-good cost basis exists on `RegionalMarket`. Need a production->market cost
   aggregation: a `weighted_producer_cost_per_unit` field + a Tier-1 step that
   publishes volume-weighted producer cost per good/province, merged before
   price_engine (Tier 3) runs. FEATURE.

3. **government_budget income tax from mean_income** (`government_budget_module.cpp:~118`).
   No per-province wage FLOW exists (only the `mean_income` stock). Need
   financial_distribution (Tier 4) to emit a per-province "wages paid this period"
   total that government_budget (Tier 5) consumes. FEATURE.

4. **financial_distribution wages = % of revenue** (`financial_distribution_module.cpp:~67`).
   The labor-market wage rate (`RegionalWageMap`) is PRIVATE module state on
   LaborMarketModule, not on WorldState, so financial_distribution can't read it.
   Worse: there are **two overlapping wage systems** — labor_market
   `process_wage_payments` AND financial_distribution salaries, both revenue-derived.
   Needs: expose the wage map on WorldState, add a per-business worker count (sum of
   facility worker_count) + a business skill domain, and RECONCILE the dual wage
   payers. Design needed. FEATURE (and an architecture cleanup).

5. **antitrust market share from revenue** (`antitrust_module.cpp:~112`). No
   per-business per-good output attribution flows through the delta system
   (`MarketDelta.supply` is anonymous/aggregated). Need producer-tagged supply (a
   `source_business_id` on the supply delta, or a per-business output field) so
   antitrust can compute true per-good shares. FEATURE.

6. **weapons_trafficking conflict stage from dominance** (`weapons_trafficking_module.cpp:~69`).
   The real signal (`CriminalOrganization.conflict_state`) EXISTS and is wired by
   criminal_operations, but the org list is not exposed on WorldState, so
   weapons_trafficking can't read it. Smallest feature here: expose criminal orgs
   (or a per-province conflict signal) on WorldState. FEATURE (small).

7. **weapons_trafficking weapon output = revenue*0.01** (`:~100`). No weapon
   production recipe exists. Ground via a real recipe/facility output (like other
   goods) or a `weapon_output_per_tick` field from production. FEATURE.

8. **weapons_trafficking diversion = regulatory_violation_severity*0.5** (`:~104`).
   Semantic mismatch (compliance != trafficking intent). Need an explicit
   `trafficking_diversion_fraction` field seeded by criminal_operations. FEATURE.

9. **money_laundering LE skill proxy** (`money_laundering_module.cpp:~242`). NPC
   skills are not modelled at all. Need a SkillDomain/skill-level model on NPCs,
   populated for law-enforcement, then read here (and reusable elsewhere). FEATURE
   (broad — unlocks several other groundings).

## NEEDS LATER FOCUS — acceptable-for-now (documented, low priority)
- **evidence credibility from social_capital** (`evidence_module.cpp:~79`).
  social_capital is the only reputation signal today; it is a defensible grounding.
  A track-record/conviction-history credibility field is the real upgrade. LOW.
- **healthcare working-age denominator** (`healthcare_module.cpp:~196`). The sick
  rate is a sample estimate (sick sample / processed sample) that converges — an
  acceptable approximation, not a conjured value. A cohort-grounded working-age
  denominator is a refinement. LOW.

## Cross-cutting theme for planning
The remaining work is dominated by **two missing capabilities** that, once built,
unlock many groundings at once:
  (a) an **NPC skill model** (unblocks money_laundering LE skill, R&D researcher
      quality, and more), and
  (b) **producer-attributed market flows** (per-business per-good output + producer
      cost), which unblock antitrust, price base-cost, and clean wage/tax flows.
Recommend scoping those two as features before resuming proxy-by-proxy work.
