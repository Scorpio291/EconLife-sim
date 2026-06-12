# Emergence Baseline & Road Ahead — 2026-06-10

## Update 2026-06-12: wealth runaway closed — regime-dependent restoring force, now visible as grievance

The "wealth runaway" follow-up (one actor hitting the 1e9 capital ceiling within
a year, noted below) is closed. It had two halves: the runaway itself, and the
fact that nothing in the social economy could *see* it.

**Root cause (the blow-up).** Two compounding bugs:
1. `banking` credited business_capital loan proceeds to BOTH the business cash
   AND the owner's personal capital — double-creating money every origination.
2. `business.revenue_per_tick` was unbounded (`output ≤ market_supply_ceiling`
   × `price ≤ market_price_ceiling` ≈ 1e14), so a revenue-scaled loan compounded:
   owner wealth → income-scaled consumption → prices → revenue → bigger loan.
Fixes: loan proceeds credit the business only (owner is borrower, not recipient);
added `SafetyCeilingsConfig::business_revenue_ceiling = 1e7` and clamp
`revenue_per_tick` to it. This un-pinned max capital from 1e9 to a bounded value.
(In practice `maxBizRev` settles ~1e3, so the revenue ceiling is a pure safety
net, never the operating point — the residual accumulation is criminal-economy
proceeds, not business profit.)

**No restoring force — but not a universal one (this is not a utopia).** Even
bounded, owner/criminal capital accumulated monotonically: profit/proceeds flow IN
every tick with no wealth-proportional outflow. The fix is NOT to assume wealth
auto-equalizes — it does not, in the model or the real world. Concentration is the
default; the only thing that reverses it is a state with the policy and capacity to
redistribute. Added a **regime-dependent progressive wealth tax**
(`government_budget`, quarterly) with TWO regime-scaled levers, because legit and
illicit wealth are constrained by different institutions:

1. **Legit wealth → progressive redistribution tax.** Non-criminal NPC capital
   above an exemption is taxed at a rate climbing with wealth (5%→75% annual),
   scaled per nation by `regime_redistribution_factor()` (Democracy/Federation 1.0 —
   accountable welfare states; Autocracy 0.25 — kleptocratic elite capture, the rich
   go largely untaxed; FailedState 0.0 — no fiscal apparatus).
2. **Illicit wealth → rule-of-law seizure.** Criminal proceeds are hidden from the
   revenue service, so the tax never reaches them — only ENFORCEMENT does. A
   separate quarterly seizure strips criminal-role NPC capital, scaled per nation by
   `regime_rule_of_law_factor()` (Democracy/Federation 1.0 — real rule of law seizes
   proceeds of crime; Autocracy 0.30 — the criminal-political elite are shielded;
   FailedState 0.0 — no enforcement at all). Criminal NPCs pay this INSTEAD of the
   tax.

Both factors are looked up per NPC through their province's `nation_id`, so the
restoring force varies by *place*; proceeds fund the national budget. This was the
correction the first cut got wrong: the tax alone left the dominant (criminal)
fortune untouched in every regime (a three-regime diagnostic showed maxCap
regime-invariant — illicit wealth evades taxation, realistically), so the visible
"some places better, some worse" only emerges once rule-of-law seizure governs the
criminal fortune. There is no automatic march toward equality — concentration is the
default, reversed only where institutions (fiscal capacity AND rule of law) enforce
it.

**...and the law does not bind equally.** A second correction: a flat levy (even a
progressive one) overstates how much the very top actually pays. A significant NPC
with a fortune is not one of the masses — they have teams (lawyers, accountants,
offshore structures, laundering networks) and political protection that shield part
of any levy. `levy_avoidance_fraction(npc, cfg)` computes that shielded fraction
INDIVIDUALLY, not as a flat function of wealth: it is composed from the NPC's own
`social_capital`, `contact_ids`, `risk_tolerance`, `financial_gain` motivation, role
(accountant/lawyer/banker/fixer and the politically protected dodge structurally
more; criminal operators launder), a modest wealth term, and a stable id-derived
innate aptitude — so two equally-rich actors with different connections/savvy dodge
by very different amounts. Both the redistribution tax and the rule-of-law seizure
are multiplied by `(1 - avoidance)`, capped so even the best-shielded pay something.
Effect: the effective rate flattens at the very top, so concentration is sticky even
in accountable states — exactly because the ultra-rich are individually modeled
actors who can buy their way around the rules.

**Inequality was blind to it.** `inequality_index` tracked only the cohort
*income* gini — which a single owner hoarding *capital* does not move — so the
concentration never reached `community_response`. Added
`RegionalConditionsModule::compute_wealth_concentration()` (normalized top-decile
capital share of the province's significant NPCs); the inequality target is now
`max(cohort_income_gini, 0.85 × wealth_concentration)`. So a boom that funnels
capital to a few owners registers as inequality → grievance. **A boom that
concentrates wealth now breeds resentment.** The 0.85 weight keeps a single rich
owner from alone pegging inequality (broad concentration is required), so the
healthy 200-NPC/3y baseline stays under the `grievance < 0.5` guard while the
longer 300-NPC/5y kingpin-accumulation scenario correctly escalates past it.

Diagnostic (`[.emergence-econ]`) now runs the SAME seed-42 economy under three
regimes (Federation / Autocracy / FailedState) and reports legit (tax-exposed) vs
illicit (enforcement-exposed) top fortunes separately, so each lever's effect is
visible: the accountable state strips the criminal fortune and taxes legit wealth
(lower concentration); the kleptocracy shields its elite; the failed state lets both
run. The general restoring force (banking double-credit + revenue cap, then the
tax/seizure) brought the dominant fortune off the 1e9 ceiling. Emergence suite stays
green (27 assertions, 10 cases); fast gate green.

**...but only crime made anyone rich — so the legit economy was fixed too.** The
three-regime diagnostic exposed a deeper gap: legit owners topped out at ~2e5 while
criminals reached ~3e8, so the redistribution tax had almost no legit target and the
regime divergence could not show in legit wealth. Two structural bugs: (1) production
only credits cash for businesses with registered FACILITIES; facility-less legit
firms (services/trade/light mfg, modelled abstractly via `revenue_per_tick`) got NO
cash inflow — they could only pay out wages/draws and never accumulate; (2) the
`expand` decision added zero capacity (a cosmetic market-supply nudge + a placeholder
consequence), so facility firms never grew either. Fixes: production now credits
facility-less legit firms their operating profit (`revenue - cost`); and a profitable
legit business that reinvests now COMPOUNDS its `revenue_per_tick`
(`NpcBusinessConfig::organic_growth_rate`, +10%/decision, clamped to the safety
ceiling) while loss-makers contract. Successful legit enterprise now builds a real,
dispersed upper class — giving the regime-scaled tax a legit target so "some places
better, some worse" can finally show in legit wealth, not just the (evading) criminal
fortune.

**Still open (the people-push axis).** Redistribution here is the *policy* lever
(a state chooses to tax). The complementary *people* lever — sustained grievance
forcing a regime to redistribute (democratic concession) or to suppress (autocratic
crackdown) — is the regime-response branch, which is unit-tested but does not yet
fire from the orchestrated economy: a production shock drives community response to
stage 4 (economic_resistance) via wage-theft memories, but national legitimacy
plateaus ~0.40 (never crossing the 0.30 crisis threshold) because measured
unemployment stays ~0 — the informal-wage floor is absolute, and this generated
world's economy is informal/criminal-dominated so a formal-production shock barely
moves the formal-employment / business-distress signals that drive grievance.
Closing that (a depression that produces real, bounded unemployment) is the next
target — it is what lets the people-push axis reach the regime-response tier.

## Milestone 2026-06-11: zero broken-loop ratchets remain

Starting point (2026-06-10 baseline): the simulation FLATLINED — ~96% of NPCs
idle, every province condition pinned at an extreme, criminal justice never
closing, six `[!shouldfail]` ratchets documenting broken feedback loops.

After this session, the emergence suite has **0 ratchets** — every feedback loop
it tracks is alive and asserted as a passing guard. The six broken loops, closed:

1. NPC activity collapse → decision-engine calibration (inaction gate to the
   bottom of the EV scale) + informal wage floor. Population stays active;
   unemployment is a real margin, not a pin.
2. Grievance pinned at 1.0 → single-owner, material-grounded model; killed a
   ~300/tick social_consequence firehose (npc_behavior per-action placeholder),
   two continuous writers (regional_conditions, population_aging) and a
   stage→grievance pump. Grievance now tracks conditions and relaxes.
3. Province stability collapse & grievance saturation → downstream of (2); both
   un-pin (stability recovers to ~0.8, grievance settles ~0.15).
4. National legitimacy crater → grounded roll-up reflects (now healthy)
   conditions; regime-differentiated response (suppression→collapse,
   concession+turnover, fragmentation) built & unit-tested.
5. Criminal justice loop → raids were seeded at `moderate` severity (below the
   custodial floor) so convictions only fined; raised the raid floor to
   `serious`. detection→raid→conviction→IMPRISONMENT→parole now closes
   (dedicated fast-gate integration test).
6. Crime-rate metric → wrong denominator (sample criminals / full population);
   now the criminal fraction of the tracked-actor sample (~0.10).

The crisis MECHANISMS (regime responses, community opposition formation) are
validated by deterministic controlled-world unit tests, since the baseline world
is now healthy by design (crises are player/event-driven, not spontaneous).

## Next target: the formal labor market (the keystone gap)

The single largest remaining integration gap. The formal labor market is
STILLBORN: `labor_market.job_postings_` is only ever populated by save-
deserialization — **no module creates JobPostings**, `npc_business` has no
labor-demand model, and there is no production channel. So no NPC is ever
formally hired; `formal_employment_rate` is propped up only by population_aging's
cohort convergence (disconnected from actual hires). Building it
(npc_business labor demand → a JobPosting seed-delta channel → labor_market
hiring → real formal employment) would:
  - make labor_market actually function;
  - give `formal_employment_rate` a real producer;
  - enable business failure → layoffs → unemployment → grievance → the unrest
    pipeline END TO END (the deprived-world crisis scenario that's currently
    untestable in the full sim);
  - close the loop from the production/business economy into the social economy.
This deserves an interface-first design pass (it spans npc_business + labor_market
+ a new delta channel).

### Concrete design (scoped this session, ready to implement)

A self-contained first slice keeps everything inside `labor_market` (it already
owns `job_postings_`, `applications_`, `employment_records_`, and *fills*
postings via `process_hiring_decisions` — it just never *creates* them). New
pre-parallel step `generate_job_postings(state)` in `init_for_tick` (single-
threaded, safe to push to the shared vectors), on the monthly cadence:
  - per business (deterministic by id): `target = clamp(revenue_per_tick /
    revenue_per_worker, 1, max_workers)`; `have = current_employees +
    open_unfilled_postings`; post `min(target-have, max_new_per_cycle)` jobs.
  - each posting: `offered_wage ≈ revenue_per_worker × 0.4` (keeps wage bill
    ≈ 40% of revenue → businesses stay solvent), `min_skill_level 0`,
    `channel public_board`, `expires = tick + duration`.
  - generate `applicants_per_posting` `WorkerApplication`s from the province's
    unemployed (employer==0) active NPCs, `salary_expectation ≤ offered` so they
    accept; advance a per-province cursor so applicants differ across postings.

Edge cases found (must handle):
  1. **Posting id counter** — add `next_posting_id_`, initialised past any
     deserialised posting id, to avoid collisions.
  2. **Garbage collection** — `close_expired_postings` currently only marks
     `filled=true` (it never GCs). With a live producer, `job_postings_` and
     `applications_` grow unboundedly; add a sweep that erases filled/expired
     postings and their `applications_` entries.
  3. **Solvency tuning** — validate via the econ diagnostic that business cash
     stays positive after wage bills (the `revenue_per_worker` / `0.4` wage
     fraction are the dials).
  4. **Re-validate the emergence suite** — formal employment rising changes
     `formal_employment_rate` (good) but must not destabilise grievance/stability
     (unemployment is the `waiting` margin, unaffected; but confirm).

New `LaborModuleConfig` dials: `revenue_per_worker`, `max_workers_per_business`,
`job_posting_duration_ticks`, `applicants_per_posting`,
`max_new_postings_per_business`.

Slice 2 (later): move labor demand into `npc_business` proper (target headcount
as a business-strategy output) via a `JobPostingSeedDelta` channel, so the
demand is a real business decision rather than a labor_market-internal heuristic;
then layoffs on business distress → unemployment → the crisis cascade.

Other smaller follow-ups: consequence-seeded legal cases stall below the 0.35
arrest threshold (evidence 0.30); ~~wealth runaway (one actor hits the 1e9 capital
ceiling within a year)~~ — RESOLVED 2026-06-12, see the update at the top of this
file (banking double-credit + uncapped revenue fixed; progressive wealth tax adds
a restoring force; wealth concentration now feeds inequality→grievance).


## Update 2026-06-11: grievance grounded → the world is healthy by default

Grievance had MANY uncoordinated writers (npc_behavior's ~300/tick
social_consequence firehose; regional_conditions + population_aging continuous
pumps; a stage→grievance feedback loop). Consolidated to a single owner
(community_response) with a material-deprivation-grounded model. Result: in the
calibrated/healthy economy, grievance now settles ~0.15 instead of pinning at
1.0, and the whole downstream un-pins — **stability recovers to ~0.8, legitimacy
to ~0.42, cohesion holds, NPCs stay active**.

Consequence: the **default baseline world is now healthy** — it does NOT enter
crisis on its own. That is correct (the GDD's world is prosperous at the Y2000
start; crises are player/event-driven), but it changes how we validate the
unrest pipeline. The emergence suite now asserts the **healthy-world positive
invariants** (grievance bounded, stability healthy, legitimacy reflects good
conditions, unemployment ≈ inaction margin). The crisis MECHANISMS — autocratic
suppression→collapse, democratic concession+turnover, failed-state fragmentation,
and community escalation→opposition formation (GDD §14.4) — are validated by
deterministic controlled-world UNIT tests ([political_cycle][unrest],
[community_response][opposition]) instead of waiting for the baseline to break.

**Open: an end-to-end "deprived/exploited world produces unrest" emergence
scenario.** There is no persistent economic-deprivation source yet (no JobPosting
producer → no real formal labor market; no mass-layoff/business-failure event;
historical_trauma_index is config-only, never implemented). Once one exists, an
emergence test can drive the full cascade economy→grievance→escalation→regime
response end to end. This is now the highest-value next integration target.


## Why this exists

The codebase is breadth-complete at the module level: ~50 V1 modules, 49
interface specs, ~1,600 fast tests. But the existing long-run integration tests
("runs 365 ticks", "prices stay positive") only assert *safety* invariants —
no NaN, values bounded — never that anything actually *happens*. So we had no
evidence about the question that matters for the 99%-experiment thesis: **does
the simulation produce emergent behavior, or do the modules just pass in
isolation while the orchestra fails to play?**

This pass built a behavioral harness to answer that empirically, then encoded
the answer as tests.

## What was built

- `simulation/tests/integration/emergence_harness.h` — boots a V1-scale
  generated world, runs it through the real base-game orchestrator for N years,
  captures an annual time series of behavioral aggregates from observable
  WorldState (module-private state like investigator cases is observed via its
  effects: NPC status, evidence pool, consequence queue, province conditions).
- `emergence_observe.cpp` (`[.emergence-observe]`, hidden) — dumps the 10-year
  series for a human to read. This is the diagnostic.
- `emergence_test.cpp` (`[emergence]`, opt-in label) — turns the baseline into
  assertions. Alive loops are locked in as regression guards; broken loops are
  Catch `[!shouldfail]` ratchets (suite stays green while they fail; the moment a
  loop is fixed the test passes, which Catch flags loudly → drop the tag).

Run: `ctest -L emergence` (behavioral suite) / `ctest -LE emergence` (fast gate).

## The baseline (seed 42, 500 NPCs, 6 provinces, 10 years)

```
yr | active wait imp dead | crim crImp | evid consq | crBiz sig | stab crime gini griev unemp | pop      | maxCap
 0 |    500    0   0    0 |   36     0  |    0     0 |    2    0 | 0.80 0.106 0.31 0.20 0.00 | 3,365,762 | 1.5e5
 1 |     10  490   0    0 |   36     0  | 1437   301 |    2    2 | 0.00 0.000 0.16 0.79 0.68 | 3,347,160 | 3.2e5
 2 |     12  ...   0    0 |   36     0  | 2549   351 |    2    2 | 0.00 0.000 0.08 0.99 0.68 | 3,273,487 | 3.4e5
 5 |     18        0    0 |   36     0  | 2050   534 |    2    2 | 0.00 0.000 0.01 1.00 1.00 | 3,066,783 | 5.8e5
10 |     19        0    0 |   36     0  | 2030   565 |    2    2 | 0.00 0.000 0.00 1.00 1.00 | 2,763,506 | 1.1e6
```

**Headline: the simulation flatlines after year 1.** Markets and capital
accumulation are alive (price spread moves; max capital grows 7× — wealth
concentrates). Almost everything else collapses to a boundary in the first year
and stays pinned for a decade.

## Findings (ranked)

### Alive loops (locked in as `[emergence]` regression guards)
- Markets move (cross-province price spread changes over time).
- Criminal activity generates evidence (pool → ~2000); the facility_signals→
  net_signal pipeline wired earlier this week reaches criminal businesses.
- Capital economy is active and bounded (no NaN, no runaway to ceiling).
- Determinism holds (identical seed → identical behavior).

### Broken / frozen loops (`[!shouldfail]` ratchets — the road ahead)

1. **Criminal justice loop never closes.** HIGH. Evidence accrues to ~2000 and
   consequences queue to ~565, but **zero imprisonments occur in 10 years**.
   `legal_process` has a working conviction→imprisonment path
   (`legal_process_module.cpp:315`), so the break is upstream: either cases are
   never seeded from the consequence/raid path, or seeded cases never progress
   to conviction. This is adjacent to (and the natural continuation of) the
   facility_signals→investigator_engine wiring just completed — the detection
   front end now works; the prosecution back end doesn't fire.

2. **Mass grievance produces no organized reaction; conditions pin with no
   resolution.** HIGH. This is the one to understand. Stability 0.80→0.00,
   grievance 0.20→1.00, unemployment →1.00, all pinned for a decade. The point
   isn't only "nothing pushes grievance down" — it's that a society under
   sustained mass unemployment should *break* (organize, strike, vote out the
   incumbent, form opposition), and the breaking is the emergent content. The
   machinery for this exists (GDD §14.2 seven-stage ladder in
   `community_response`), so the second observer pass measured the RESPONSE side:

   ```
   yr | unemp griev | response_stage(mean/max) cohesion inst_trust resource
    1 | 0.68 0.79  | 3.7 / 4   coh 0.27   trust 0.48   res 0.92
    2 | 0.68 0.99  | 4.0 / 5   coh 0.13   trust 0.45   res 0.76
    3 | 0.84 1.00  | 4.0 / 4   coh 0.00   trust 0.39   res 0.93
    4+| 1.00 1.00  | 4.0 / 4   coh 0.00   trust 0.36   res ~1.0   (frozen here for 7 yrs)
   ```

   Diagnosis: the population *does* start reacting — escalation climbs to stage
   4 (economic_resistance) and briefly stage 5 (direct_action/strikes) — but then
   **jams at stage 4 and never reaches stage 6 (sustained_opposition)**, so no
   OppositionOrganization ever forms despite grievance pinned at 1.0 and
   resource_access at ~1.0 for years. The block is **cohesion collapsing to
   0.00**: `direct_action` gates on cohesion ≥ 0.45 and `sustained_opposition`
   needs a qualifying leader, and cohesion is computed from NPC
   `social_capital × stability-motivation` averaged over the province. So the
   keystone chain is:

   > year-1 NPC-inactivity collapse (490/500 → `waiting`) → cohesion → 0 →
   > collective-action stages of the ladder are gated shut → population is
   > maximally aggrieved but cannot organize → escalation stuck at boycotts →
   > no opposition, no regime turnover, no relief → grievance/stability/
   > unemployment pinned at extremes forever.

   And even the stages it does reach have **no resolution mechanism**: economic
   resistance / brief strikes produce no concessions, no policy that addresses
   unemployment, no electoral turnover; opposition formation (if it occurred)
   only emits a `political_consequence` that *lowers* institutional trust,
   amplifying the downward spiral. The loop has no discharge — real uprisings end
   in suppression, concessions, or regime change; this one just floors.

   Two things to fix: (a) the upstream NPC-activity/cohesion collapse that gates
   the ladder shut, and (b) a resolution path so reaching the top of the ladder
   *changes the political/economic order* (electoral turnover, policy response,
   or regime change) instead of only amplifying decline. (Nation-level revolution
   / "war as failure mode" is EX scope per the Feature Tier List; the V1-
   appropriate discharge is organized opposition + electoral turnover via
   `political_cycle`.)

3. **Regional crime metric disconnected from reality.** MEDIUM. `crime_rate`
   →0.00 even though 36 criminals and 2 criminal businesses persist. The
   cohort-stats crime aggregation isn't tracking the criminal population it is
   meant to measure. Same disconnect smell as `gini`→0 while capital concentrates.

4. **Business & era dynamics inert.** MEDIUM/LOW. Business count frozen at 50
   (2 criminal) for a decade — `business_lifecycle` produces no failures/entries
   and no criminal-sector growth. Era frozen at 1 for 10 years (may be intended
   V1 pacing — verify against the R&D era-trigger design).

5. **Population monotonic decline** ~2%/yr (3.37M→2.76M). LOW. Possibly intended
   demographic drift; flag to confirm births/deaths balance is by design.

## Update 2026-06-10 (later session): year-1 collapse root-caused

Design steer: **unemployment can never approach 100% — there is always some kind
of work for willing bodies** (even the worst real collapses top out ~25-30%
formal unemployment; informal/subsistence work absorbs the rest).

Diagnosis of the year-1 NPC-inactivity collapse, traced end to end:

1. **No module creates JobPostings** — labor_market only consumes them (its only
   producer reference is its own save-deserialization). The formal hiring market
   is stillborn: no NPC is ever hired; all employment records stay employer=0.
   (Cohort `formal_employment_rate` still reads ~0.1-0.6 because
   population_aging's demographic convergence keeps the *stat* alive — the stat
   is disconnected from actual hires.)
2. **npc_behavior coupled work's value to the employment rate with no floor**:
   `wage = base_wage × employment_rate`, `work_prob = 0.5 + rate × 0.4` — a
   death spiral (low employment → worthless work → inaction → lower employment).
   FIXED: informal/subsistence wage floor (`NpcBehaviorConfig.informal_wage_floor
   = 0.30`) — formal scarcity degrades pay toward subsistence, never zero.
3. **The unemployment metric violated its own spec** (RegionCohortStats:
   unemployed = neither formal NOR INFORMAL): it counted every active NPC
   without a formal employer as unemployed and EXCLUDED `waiting` NPCs from the
   sample. FIXED: labor force = active + waiting; active-without-employer =
   informal worker; waiting-without-employer = unemployed.
4. **The real ignition (still open): the decision engine is born at the inaction
   margin.** EV = motivation_weight × probability × magnitude; for work that is
   0.25 × ~0.78 × 0.5 ≈ **0.0975 < inaction_threshold (0.10) on day one**, even
   at healthy 70% employment. ~96% of NPCs fall to `waiting` within months
   regardless of wages. This is a GDD §3 utility/threshold calibration pass on
   npc_behavior's candidate magnitudes/probabilities vs `inaction_threshold` —
   design-sensitive, deserves its own session, and is now THE keystone: fixing
   it un-pins unemployment (→ grievance → cohesion → the opposition ladder →
   regime-response timing all become meaningful).

New ratchet: "unemployment never approaches 100 percent" (< 0.60 at horizon) —
fails at ~0.96 until the §3 calibration lands. The missing JobPosting producer
(npc_business labor demand) is a separate follow-up; the cohort stat is not a
valid observable for it (population_aging keeps it nonzero), so it is tracked
here rather than as a ratchet.

## Update 2026-06-11: decision-engine calibration landed — activity restored

inaction_threshold 0.10 → 0.03 (gate at the bottom of the achievable EV scale,
per GDD §3). Observer results (seed 42, 500 NPCs, 10y, all three regimes):

- **Activity collapse FIXED**: 500/500 NPCs active for a decade, zero `waiting`.
- **Unemployment 0.96 → 0.00** (the inaction margin; frictional unemployment >0
  will emerge as motivation profiles diversify). Ratchet "unemployment never
  approaches 100 percent" flipped → promoted to a regression guard.
- **New emergent behavior**: NPCs now FLEE deteriorating provinces (19/500 fled
  by year 10) — migration responds to conditions.

**Next root exposed — grievance generation is disconnected from material
conditions.** With full informal employment, zero unemployment, and a fully
active population, grievance STILL pins at ~1.0 by year 1 (and cohesion still
zeroes, dragging stability to 0 and legitimacy to 0; the earlier "stability does
not fully collapse" pass turned out to be an artifact of the mostly-waiting
population and was re-demoted to a ratchet). Grievance is sampled from NPC
memory-log negativity (community_response::compute_grievance_contribution), so
a fully active population generating interaction memories saturates it
regardless of economic reality. The fix target: tie grievance generation/decay
to actual conditions (employment, income adequacy, addressed-vs-ignored
grievances per GDD §14.2 intervention points), so a materially-okay world
relaxes toward contentment and a deprived one escalates.

Open ratchets after this pass (5): grievance disconnect (the new keystone),
stability collapse (downstream of it), criminal justice loop, organized
opposition (cohesion gate, partly downstream), crime-rate metric, legitimacy
crisis response (downstream of grievance).

## Recommended road ahead

1. **Close the criminal justice loop (#1).** Highest leverage and continues the
   detection-pipeline thread. Trace consequence-fire → `LegalCaseSeedDelta` →
   `legal_process` case progression and find where it stalls. Flips the first
   ratchet green.
2. **Make the world react to mass unemployment (#2).** Two parts: (a) fix the
   year-1 NPC-activity/cohesion collapse that gates the escalation ladder shut,
   so a maximally-aggrieved population can actually organize (reach
   sustained_opposition); (b) give the top of the ladder a resolution path —
   electoral turnover / policy response via `political_cycle` — so the reaction
   changes the order instead of only amplifying decline. Flips the
   "organized opposition", "stability", and "grievance relaxes" ratchets together.
3. **Reconnect the crime metric (#3).** Smaller, isolated.
4. Then revisit business lifecycle / era pacing / population (verify intended).

Each fix is validated by a ratchet flipping from "failed as expected" to
"passed" — which is exactly the human-light validation loop the 99% experiment
is built around.
