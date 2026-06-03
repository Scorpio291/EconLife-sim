# EconLife Module Implementation Audit — 2026-06-02

> **Progress update (2026-06-03):** Working the priority list from §6.
> - **#1 npc_business tick bug — FIXED** (`next_decision_tick_update` delta;
>   businesses no longer re-decide every tick).
> - **#2 population_aging STUB — BUILT** (4 slices): cohort data model on
>   RegionCohortStats (12 DemographicGroups), monthly income/employment
>   convergence + annual education drift + births/deaths + aggregate
>   (total_population/mean_income/gini) recompute via a new CohortStatsDelta,
>   world-gen cohort seeding, persistence v16 (cohorts + NPC age_years), and
>   significant-NPC annual aging + natural death. **Deferred (flagged):**
>   retirement role transitions (no `retired` NPCRole exists); per-cohort
>   skill_supply / aggregate_skill_supply. Spec-vs-impl gaps worked around:
>   labor wage market + HealthcareProfile + NPC.health are not on WorldState
>   (proxied by regional_wage_anchor, sick_rate, age respectively).
> - **#3 political_cycle STUB — BUILT** (3 slices): self-seeded governor
>   offices, government-type gating (Autocracy/FailedState skip), real
>   coalition-weighted election resolution from province cohorts with
>   incumbent turnover (3a); campaign auto-activation + endorsement
>   application (3b); full legislative pipeline — stage progression,
>   NPC-legislator polling via compute_legislator_support, vote resolution,
>   enacted→consequence, dead-sponsor→failed (3c). **Flagged (cross-module
>   gaps, not internal):** no producer yet creates proposals/endorsements;
>   NationPoliticalCycleState not delta-updated (no NationDelta); module
>   state not persisted; real enacted-policy effects await the consequence
>   system (#4).
> - **#4 consequence system — ENGINE BUILT** (GDD §21): ConsequenceCategory
>   (8 types) + ConsequenceEntry + WorldState.consequence_queue; delay formula
>   (BASE_DELAY × (1+variance) × awareness); ConsequenceDelta schedule/cancel
>   routed by apply_deltas; firing in drain_deferred_work (investigation/legal
>   → LegalCaseSeedDelta, media/political → trust, social → grievance,
>   rival → dominance); persistence v17; fires after source death; cancellable.
>   **Follow-up 4b (not done):** migrate the ~19 legacy consequence_deltas
>   emitters (calendar, antitrust, npc_business, political_cycle, …) from the
>   no-op new_entry_id to typed ConsequenceEntries with real
>   source/target/awareness.
> - #5 regional_conditions, #6 module finishes — not yet started.


**Scope:** All ~49 modules with an `INTERFACE.md` spec, comparing spec ↔ shipped
code. **Method:** six parallel auditors (one per module batch), each comparing
the INTERFACE to the `.cpp`/`.h` with file:line evidence, **followed by a
manual verification pass** on every high-impact and excerpt-limited claim. The
verification pass corrected several auditor false negatives (noted inline);
statuses below are post-verification.

**Confidence key:** ✅ verified by hand · ◻︎ auditor-reported (spot-checked, not
line-by-line re-verified).

---

## 1. Master status table

| Module | Status | Conf | One-line |
|---|---|---|---|
| rng | COMPLETE | ✅ | SplitMix64 + fork + distributions. |
| world_state | COMPLETE | ✅ | Struct + delta semantics; a few supporting types are stubs (see §3). |
| tick_orchestrator | COMPLETE | ◻︎ | Kahn topo-sort, cycle detection, per-module delta apply. |
| price_engine | COMPLETE | ◻︎ | 3-step sticky equilibrium; equilibrium override omitted *by design*. |
| npc_spending | COMPLETE | ◻︎ | 4-factor demand; quality factor a documented V1 simplification. |
| production | COMPLETE | ◻︎ | Recipe + tech-tier + quality ceiling + criminal informal pricing. |
| commodity_trading | COMPLETE | ◻︎ | Position open/close, P&L, market impact (no margin = V1 by design). |
| business_lifecycle | COMPLETE | ◻︎ | Era transition, stranded-asset penalties, era-entrant spawning. |
| banking | COMPLETE | ◻︎ | Repayment, default, credit scoring, collateral seizure, foreclosure. |
| currency_exchange | COMPLETE | ◻︎ | Weekly rate model, peg break, crisis triggers. |
| financial_distribution | COMPLETE | ◻︎ | Salary (FIFO deferred), draw, dividends, tax withholding, wage-theft. |
| trade_infrastructure | COMPLETE | ◻︎ | Transit arrival, perishable decay, interception, route formula. |
| real_estate | COMPLETE (Ph 1–6) | ◻︎ | Rent/value/buy/sell/mortgage/foreclosure/auction; Ph 7+ = post-V1. |
| supply_chain | COMPLETE | ✅ | Local match + transit + interception + LOD1 imports (all defined/called). |
| investigator_engine | COMPLETE | ◻︎ | Signal aggregation, meter, status, evidence/consequence, case seeding. |
| legal_process | COMPLETE | ✅ | Full charge→trial→convict→imprison→parole state machine. |
| evidence | COMPLETE | ◻︎ | Decay batches, holder credibility, actionability floor, token creation. |
| criminal_operations | COMPLETE | ✅ | Org formation + quarterly decisions + conflict + seeds (this session). |
| protection_rackets | COMPLETE | ✅ | Seed drain, demand, escalation, grievance (this session). |
| informant_system | COMPLETE* | ✅ | Self-seed + flip + testimonial evidence; *countermeasures planned (§3). |
| addiction | COMPLETE | ◻︎ | 7-stage state machine, withdrawal, recovery, relapse, terminal. |
| healthcare | COMPLETE | ◻︎ | Passive recovery, treatment, capacity, sick-leave. |
| community_response | COMPLETE | ◻︎ | EMA metrics, stage machine, opposition formation. |
| influence_network | COMPLETE | ◻︎ | Relationship classes, obligation erosion, recovery ceiling, health. |
| obligation_network | COMPLETE | ◻︎ | Demand growth, escalation, hostile-action gating, trust erosion. |
| npc_behavior | COMPLETE | ◻︎ | Daily EV decision engine, memory/knowledge decay, 8 action candidates. |
| player_actions | COMPLETE | ◻︎ | Queue drain + validate + dispatch to 20+ action handlers. |
| trust_updates | COMPLETE | ✅ | Memory→trust conversion + recovery ceiling (consumer pattern; see §4). |
| seasonal_agriculture | COMPLETE | ◻︎ | Annual crop machine, soil, monoculture, climate mods (seed-track deferred). |
| random_events | COMPLETE | ✅ | Poisson trigger (`1-exp(-rate/month)`), templates, effects (auditor under-read). |
| persistence | COMPLETE | ✅ | LZ4 + schema v15 + migrations + restoration/disruption tiers (auditor under-read). |
| deferred_work_queue | PARTIAL | ✅ | Min-heap + handlers, but `consequence` handler is a **no-op** (§3). |
| money_laundering | PARTIAL | ✅ | Transfer + structuring + shell-chain + FIU wired; crypto/commingling helpers **unused** (§2). |
| government_budget | PARTIAL | ◻︎ | Quarterly taxes/spending, but income & property tax use **placeholder estimates**. |
| antitrust | PARTIAL | ◻︎ | Monthly HHI + meter, but evidence/legislative-proposal output uses placeholder id. |
| weapons_trafficking | PARTIAL | ◻︎ | Diversion + pricing + evidence, but **embargo consequence / meter spike / chain-of-custody missing**. |
| drug_economy | PARTIAL | ◻︎ | Production/demand/seeding, but **recipe registry + market layers proxied; no cross-province shipments**. |
| designer_drug | PARTIAL | ◻︎ | Detection + scheduling stages, but **SchedulingProcess record not created**; political delay unused. |
| media_system | PARTIAL | ◻︎ | Story creation/pickup/amplification, but **exposure→reputation is a placeholder** (not written). |
| calendar | PARTIAL | ◻︎ | Deadline machine + missed-deadline consequence, but **fast-forward suppression not written to state**; consequence id placeholder. |
| scene_cards | PARTIAL | ◻︎ | Calendar trigger + choice resolution, but **authored-priority / max-per-tick / procedural fallback** unconfirmed. |
| facility_signals | PARTIAL | ✅ | Computes signals, but spec's EvidenceDelta/consequence emission lives in investigator_engine (ownership drift, §4). |
| alternative_identity | PARTIAL | ◻︎ | Decay/burn/maintenance cost, but **witness tracking, identity-link discovery, compromised status missing**; attribution is retroactive not at-emission. |
| lod_system | PARTIAL | ◻︎ | Monthly LOD1 supply + annual LOD2 price index, but **trade-offer objects, tier advancement, era/political events missing**. |
| regional_conditions | PARTIAL | ✅ | Stability/crime/dominance/trust, but **formal_employment_rate & regulatory_compliance not computed**. |
| npc_business | PARTIAL | ✅ | Decision matrix works, but **`strategic_decision_tick` never advanced** → re-decides every tick (BUG, §2). |
| political_cycle | STUB | ◻︎ | Election resolution + vote pass/fail only; **campaign/coalition/endorsement/obligation logic absent**. |
| population_aging | STUB | ✅ | 115 lines; writes province stress deltas only — **no cohort convergence, births/deaths, NPC retirement, education drift, Gini**. |

**Tally:** ~31 COMPLETE · ~14 PARTIAL · 2 STUB.

---

## 2. Confirmed correctness bugs (recommend fixing)

1. **`npc_business.strategic_decision_tick` is never advanced.** ✅
   `is_decision_tick()` returns `current_tick >= biz.strategic_decision_tick`
   (`npc_business_module.cpp:300`); nothing in the module, `apply_deltas`, or
   anywhere else writes/advances that field for an `NPCBusiness` (only
   persistence serializes it). Once a business's first decision tick passes, it
   re-runs its full quarterly strategic decision **every tick** thereafter,
   instead of every 90 ticks per `npc_business/INTERFACE.md` §Postconditions.
   Contrast: `criminal_operations` *does* advance its analogous field
   (`criminal_operations_module.cpp:253`). **Low-risk fix**: emit/advance
   `strategic_decision_tick += TICKS_PER_QUARTER` when a decision fires.

2. **`money_laundering` crypto-mixing & cash-commingling evidence paths are
   dead code.** ✅ `compute_crypto_evidence_probability` and
   `compute_commingling_capacity` are defined (`money_laundering_module.cpp:46,55`)
   but never called from `execute()`. (FIU structuring detection *is* wired,
   :259–276 — the auditor's "FIU missing" was wrong.) Because seeded ops only
   ever use `shell_company_chain`, the crypto/commingling methods are never
   exercised. Not a crash, but two specced evidence channels are inert until
   laundering-method diversity is seeded.

3. *(Already fixed this session)* `money_laundering` launder-rate was a fraction
   misused as an absolute amount — corrected to `dirty_amount / ticks_per_quarter`.

---

## 3. Cross-cutting / systemic gaps

- **Generic consequence system is stubbed.** `ConsequenceDelta` carries only a
  placeholder `new_entry_id` (`delta_buffer.h:87`); the deferred-work
  `handle_consequence()` is a **no-op** (`drain_deferred_work.cpp:47`, comment
  "no-op until Session 16"). Modules that should schedule generic consequences
  (`calendar` deadlines `:147`, `antitrust`) encode `new_entry_id = actor_id`
  as a stand-in. **Note:** this is the *generic scheduled-consequence*
  mechanism — the real `investigator_engine → legal_process → imprisonment`
  pipeline works independently and is **not** affected.
- **`InvestigatorMeter` as a first-class NPC field doesn't exist.** Several
  specs (facility_signals §19, antitrust, informant) describe writing
  `NPCDelta.investigator_meter.fill_rate`; in practice `investigator_engine`
  keeps its meter internally and upstream modules feed it via signals /
  `motivation_delta`. `legal_process` notes the same ("InvestigatorMeter records
  are not on WorldState yet"). Pervasive spec-vs-impl drift, not a functional
  hole.
- **Supporting `shared_types.h` stubs**: `ObligationNode`,
  `InfluenceNetworkHealth`, `DialogueLine`/`PlayerChoice`, and `ConsequenceType`
  are minimal skeletons (`shared_types.h:3-10`), so the data models behind
  scene-card dialogue and formal consequences are thinner than their specs.
- **Informant countermeasures** (pay/threaten/relocate/eliminate),
  InvestigatorMeter fill on disclosure, and knowledge-type→token mapping are
  specced but unbuilt — already documented under "Planned / not yet
  implemented" in `informant_system/INTERFACE.md` (reconciled earlier this
  session).

---

## 4. Spec drift to reconcile (capability exists, just not where/how the spec says)

- **`facility_signals`** ✅ computes `net_signal` and feeds LE/regulator
  motivation, but the `EvidenceDelta`/consequence emission its spec assigns to
  it (§25–26) is actually performed by `investigator_engine`. Gameplay works;
  the spec names the wrong owner.
- **`trust_updates`** ✅ converts this-tick `MemoryEntry`s into trust deltas
  with a recovery ceiling. The spec lists "business outcomes, criminal exposure,
  community events" as sources — those are **upstream modules emitting
  MemoryEntries**, which this module consumes. It is complete as a *consumer*;
  the auditor's "~25%" mistook the producer/consumer split for missing logic.
- **`price_engine`** intentionally never recomputes `equilibrium_price`
  per-tick (documented anti-decay-spiral rationale) — looks "missing", is by
  design.

---

## 5. Intentionally deferred to post-V1 (NOT gaps — per Feature Tier List `[EX]`)

Multi-nation play (V1 = 6 provinces, one nation); human trafficking ("first
major expansion"); cybercrime; full counterfeiting suite; advanced career paths
(infiltrator/undercover, union leader, hostile-takeover/short-selling); advanced
finance (hedge funds, insurance, "too big to fail"); education chain
(schools/university); advanced manufacturing (power plants, aerospace,
photovoltaics, pipelines); personal security/bodyguards; full prison-exit
options; multi-generational heir play; NPC↔NPC obligation graph (implicit via
motivations in V1); dynamic LOD promotion. Source markers: `npc.h`
`bodyguard`/`appointed_official [EX]`, `player.h` `UndercoverInfiltration [EX]`,
`geography.h` `diplomatic_relations [EX]`.

---

## 6. Recommended priority order

1. **`npc_business` tick-advance bug** (§2.1) — small, high-impact correctness fix.
2. **`population_aging`** (§1 STUB) — the largest functional hole; demographics
   are static (no aging/mortality/income convergence).
3. **`political_cycle`** (§1 STUB) — elections resolve but campaigns/coalitions absent.
4. **Generic consequence system** (§3) — unblocks calendar/antitrust deferred consequences.
5. **`regional_conditions`** missing `formal_employment_rate`/`regulatory_compliance` (§1) — other modules read these.
6. Module finishes: weapons_trafficking embargo path, drug_economy recipe/market layers, designer_drug SchedulingProcess, media exposure→reputation, money_laundering method diversity.

> Confidence note: ✅ items were re-verified by hand. ◻︎ items reflect the
> auditors' reading (spot-checked); a few PARTIAL ◻︎ statuses for large files
> (scene_cards, lod_system, government_budget) may understate completeness, as
> the persistence/supply_chain/random_events corrections above showed excerpt
> reading can produce false negatives. Treat ◻︎ PARTIALs as "worth a closer
> look", not confirmed holes.
