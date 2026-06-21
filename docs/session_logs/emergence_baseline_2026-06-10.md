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

**Resource-driven economy — the foundation (Phase 1 of 4: extraction binds to finite,
located deposits).** Pulling on "everything is downstream of world gen and the seeded
resources" exposed that the rich geological model (tectonic-seeded `ResourceDeposit`
with grade/quantity/accessibility/depletion, `FisheriesProfile` Schaefer model, soil/
climate) was almost entirely *unwired* from the running economy: extraction recipes
had **no input and conjured raw materials from nothing** (`iron_mining → iron_ore 10`),
production never read or depleted deposits, and `facility_generator` assigned mining
recipes at **random** with no reference to a province's actual deposits — so geology
did not constrain anything and comparative advantage could not emerge. The architecture
turns on **endowment ≠ exploitation**: a deposit must *exist* (seeded) before it can be
exploited, but existence does not force exploitation. Phase 1 implements the existence
precondition and finiteness: an extraction-bound recipe (new `extracted_resource`
column → `Recipe::extracted_resource`) produces **only** where the province holds a
matching deposit that is era-unlocked, accessible, and not exhausted; output scales with
deposit grade and is capped by `quantity_remaining`; extraction **depletes** it (new
`DepositDelta`). No deposit → no output, no cost. Scope is confirmed V1 (Feature Tier
List: extraction facilities, geological/biological resource layers, *deposit depletion
over time*, environmental consequences). Remaining arc: **P2** production debits
intermediate inputs from located stock (conservation; the production INTERFACE already
requires it); **P3** endowment-driven facility placement at world gen (mines matched to
deposits, energy-intensive industry where power is cheap, fishing where coastal access
allows) — note P1's gating + P3's placement are complementary (gating without placement
leaves randomly-placed mines that don't match local deposits idle); **P4** primary
sector reads its endowment (energy_cost_baseline from hydro/geothermal/fossil, a
fisheries module running the Schaefer dynamics, agriculture yield from soil/arable/
climate). Fast gate green (1630/1630, +3 extraction tests).

**Resource economy Phase 2: matter conservation — bind it to physics.** With
extraction made finite (P1), the next physical law: matter is neither created nor
destroyed, only moved or transformed. Audited every `supply_delta` source. Two
legit-chain leaks closed: (1) **production now debits its inputs from the located
stock** (`supply_delta = -consumed`, not merely a demand signal) — the spec
postcondition ("input supply decreases, output supply increases") that the impl had
never honored, so the stock used to gain outputs while never losing inputs (matter
created every tick); (2) **`npc_business` no longer conjures supply from cash** — the
old code added `cash_spent * 0.001` of "supply" to a market keyed by the *sector enum
cast to a good_id*, both creating matter from money and mis-attributing it; capacity
growth already flows through compounding `revenue_per_tick`. Net: located goods stock
is now mass-conserved — sources are extraction (from finite deposits) + production
output + agriculture/transit; sinks are consumption + surplus decay + production input
use. Known remaining proxy: the criminal/drug chain still uses a placeholder precursor
(`good_id 9999`, demand-only) — separate stubbed-proxy plumbing, out of P2 scope.
**Energy is still pure-cost (not physical):** `energy_per_tick` is only a money term
in `base_cost`, `energy_cost_baseline` is unused, and fuels (coal/oil/gas) are
extracted but never consumed for energy. Binding energy to physics — regional energy
price from the hydro/geothermal/fossil endowment, with facility energy demand
consuming regional fuel stock (renewables matter-free) — honors V1's "energy is a
regional cost, no buildable power plants" while conserving energy + fuel-matter; it
folds into P4. Fast gate 1631/1631 (+1 conservation contract test); emergence 27/27.

**Resource economy Phase 3: endowment-driven facility placement.** The direct
complement to P1 — without it, `facility_generator` assigned extraction recipes at
random, so under P1's gating most mines sat on the wrong deposit and produced nothing.
P3 restricts extraction-category facilities (mine/oil_well/quarry/logging_camp) to
recipes whose `extracted_resource` matches a deposit the province actually holds: you
build a mine where the ore is. If the local geology matches no extraction recipe, no
extraction facility is created there, leaving deposits the province lacks the industry
for as unexploited potential (existence ≠ exploitation, again). This seeds comparative
advantage structurally — a province's primary sector is now shaped by what is in its
ground — and makes P1+P2 productive rather than idling randomly-placed mines. World-gen
output for a given seed changes (deterministically); macro top-line metrics are not
where specialization shows (that lives in per-province production mix), so the
behavioral gates stay green. Remaining: P4 — primary sector reads its endowment in full
(energy as a real good from hydro/geothermal/fossil with fuel consumed to meet demand;
fisheries Schaefer module; agriculture yield from soil/arable/climate).

**Resource economy Phase 4a: energy as a generated, consumed good — bind energy to
physics.** Energy was pure-cost (`energy_per_tick` only fed `base_cost`; fuels were
extracted but never burned). The architect chose the *full electricity good* over
V1's abstracted-cost model. Implemented inside ProductionModule (it already runs first,
is province-parallel, and reads markets/facilities/deposits — avoids new-module
plumbing): an `electricity` good (added to the catalog) is generated per province each
tick in an energy pre-pass before facilities run. Renewables (solar/wind/geothermal
deposit capacity + hydro from `river_flow_regime`) generate matter-free; the shortfall
to meet the province's installed energy demand BURNS the province's fossil-fuel stock
(crude_oil/natural_gas/thermal_coal), consuming that matter (`supply_delta = -burned`).
The brownout ratio = clamp(generation/demand, 0, 1) throttles every facility's output
that tick. So energy is now conserved physics: fuel matter → electricity → work, with
renewables as free flows. Comparative-advantage signal: river/fuel-rich provinces run
at full output; provinces poor in both brown out and are output-limited; fossil-
dependent ones draw down finite reserves. Recipes with `energy_per_tick == 0` (and the
existing hand-built test recipes) see ratio 1.0 — no behavior change — so the gates
that don't model energy are unaffected. Fast gate (+3 energy tests); emergence 27/27.
Calibration is deliberately generous (hydro/renewable yields sized so brownouts hit
only genuinely energy-poor provinces) to avoid an economy-wide output collapse; the
magnitude can be sharpened later. Remaining P4: fisheries Schaefer module; agriculture
yield from soil/arable/climate.

**Resource economy waste pass: waste-as-conserved-matter.** Processes do not vanish
matter — the share not embodied in the product leaves as waste that must be handled.
Three new
`waste`-category goods (`industrial_waste`, `hazardous_waste`, `municipal_waste`).
Production emits waste proportional to each output's throughput, typed by the output
good's **category** (a pass that covers EVERY product without per-recipe authoring):
petroleum/chemicals/pharma + electronics → hazardous; heavy industry/metals/mining/
vehicles/construction + food/textiles/timber → industrial; services/financial/energy →
none. `npc_spending` emits `municipal_waste` from civilian consumption (and skips waste
goods as consumer purchases). Waste accumulates per province and disperses via the
standard surplus decay until handled. Conservation now spans the whole chain incl.
consumption. World-gen robustness: waste markets are seeded RNG-FREE (fixed values) —
`create_markets` runs before facilities/population/tech, so consuming RNG for added
goods would silently reshape the whole world downstream (a first attempt cratered
national legitimacy exactly this way, via the shifted facility/population world). With
waste goods RNG-neutral the world is identical to the prior baseline regardless of how
many waste/utility goods exist. Adds a production-waste test.

**Waste handling + pollution consequence — unhandled waste now bites.** `regional_conditions`
(Tier 11, after production/spending) now reads each province's accumulated waste and
(1) HANDLES it — removes a fraction each tick scaling with `infrastructure_rating`
(waste-management capacity; hazardous is harder to handle), so waste only builds where
generation out-paces handling; and (2) the hazardous-weighted residual POLLUTES — a
saturating function (robust to absolute waste scale) raises `sick_rate`, which drives
mortality via `population_aging`. Routed to **health, not grievance** (grievance has a
single owner, `community_response` — the lesson from the earlier multi-writer pin).
Constants conservative (strong handling, gentle pollution) to stay clear of the
emergence guards.

**Resource economy: fisheries (Schaefer surplus-production).** The seeded
`FisheriesProfile` was never fished. `seasonal_agriculture` (biological primary sector,
province-parallel) now runs the Schaefer model per coastal/freshwater province each
tick: logistic growth `r·N·(1−N/K)` replenishes the stock; fishing effort harvests a
fraction, landed as `fish_wild` supply; a seasonal closure pauses harvest (not growth).
Effort below the intrinsic growth rate is sustainable; raising it (config/mods)
overfishes toward collapse — the shared-access problem the struct documents. Stock is
updated via a new `FisheriesDelta` (mirrors P1's `DepositDelta`; applied clamped to
[0,K]). Landlocked provinces (`NoAccess`) don't fish. Adds fisheries tests + merge-
coverage.

**Resource economy P4b: agriculture endowment (redone, calibrated).** Re-landed the
soil/arable yield binding that was reverted earlier. Post-mortem: the original revert
was misattributed — the legitimacy crater that run was the WASTE goods' world-gen RNG
shift (fixed by RNG-neutral waste markets), not agriculture; agriculture had only
broken the daily_growth unit tests (they assert exact values). This pass: crop
`daily_growth` is scaled by `soil_fertility(soil_type)` (Mollisol/Alluvial 1.30×,
Andisol 1.25×, moderate soils 1.0×, poor 0.8×, Cryosol 0.6×) and arable land
(`0.7 + 0.4·arable_land_fraction`) — deliberately gentle and centered near 1.0 so it
redistributes output by geology (the agricultural comparative-advantage signal)
without crashing total food. The daily_growth unit tests get a neutral test province
(Vertisol + arable 0.75 → endowment exactly 1.0) so their assertions are unaffected.
This completes the resource-economy arc P1–P4. Fast gate; emergence 27/27.

**Criminal economy — grounding drug production in conserved precursors (slice 1).** A
three-agent deep dive mapped the criminal/grey-zone subsystems. Findings: the
enforcement loop (detection→prosecution→imprisonment) is CLOSED (fast-gate test) — the
CLAUDE.md "broken loop" note was stale and is now corrected. The grey-zone/concealment
scaffolding (money_laundering, regulatory_violation_severity + scrutiny meter,
visibility scopes, facility_signals, alternative_identity) is comparatively mature.
The weak point is the criminal *production* economy: it runs on proxies that violate
the conservation the legit economy now obeys. Specifically `drug_economy` produced in a
parallel proxy economy — output = `revenue_per_tick × 0.1`, a fake precursor
(`good_id 9999`, demand-only), drug supply keyed by the `DrugType` enum value cast to a
good_id (separate from the real cocaine/heroin catalog goods). (coca/poppy→cocaine/
heroin are EX-reserved in DrugType and already run conserved through the production
module's synthesis recipes when seeded.) Slice 1: synthetic drugs (meth/synthetic/
designer) now CONSUME real located `drug_precursors` — availability-bottlenecked and
debited from province stock (`supply_delta = -consumed`) — and the `9999` placeholder
is gone. Cannabis (the fallback majority) stays on the proxy for now: it has no
cultivation recipe, so gating it would collapse it — adding a cannabis cultivation
chain is the next slice. Remaining criminal-grounding work: cannabis cultivation;
unify drug supply onto real good-ids / route through production and retire the parallel
enum-keyed economy; weapons-diversion conservation (debit formal stock); designer_drug
revenue+inputs; wire alternative_identity discovery into the investigator engine.

**Criminal economy unification — Phase 1: real drug good-ids (fix the collision).** Wrote
the phased plan (docs/design/EconLife_Criminal_Economy_Unification_Plan.md) and executed
Phase 1. `drug_economy` had cast the `DrugType` enum value (0–3) straight to a good_id
for drug supply, price lookup, and addiction demand (`good_id 0`) — colliding with the
first catalog goods (iron_ore=0, copper_ore=1, bauxite=2…), so the drug economy was
silently reading from and writing into the iron-ore/copper/bauxite markets. Fixed: a
`DrugType → real catalog good` map (cannabis→cannabis_processed, meth→methamphetamine,
etc.); supply, price, and addiction demand are now keyed by the real good id via
`lookup_good_id`. This moves drug pricing off iron-ore's ~12 onto the real drug goods'
300–2000 band (a deliberate revenue recalibration) — validated: fast gate 1637/1637
(criminal-justice loop green), emergence 27/27; the bounded-wealth machinery (ceiling +
regime tax/seizure) held the surge. Next: Phase 2 (route drug production through the
production module, retire the revenue×0.1 proxy).

**Criminal economy — the balancing keystone: enforcement bite with organizational
resilience.** Phase 1's realistic drug prices made the long-horizon diagnostic run away,
which exposed the real gap: crime has NO negative feedback. The detection→prosecution→
imprisonment loop jails *individuals* but never touches the *enterprise* — production and
drug modules don't check operator status, and lucrative crime never goes bankrupt. So
crime grows unchecked everywhere and the corruption-modulated detection variance is
toothless. Per the "don't build fake rails; some places are crime-infested, others
well-managed" steer, the fix is to close the loop, not cap revenue. Keystone: a criminal
business whose operator (`owner_id`) is imprisoned runs at a reduced **resilience floor**
(`DrugEconomyConfig::operator_imprisoned_output`, 0.5) — a deputy keeps it going (gangs/
mafias survive decapitation) and it recovers fully on release. Crime suppression is thus
proportional to how often operators are jailed = enforcement strength, throttled by
corruption = governance. Well-policed, low-corruption provinces trend crime-light; weak/
corrupt ones stay crime-infested — emergent divergence, and organized crime is never
fully eliminated. Implemented for `drug_economy` (dominant criminal output + the
hyperactivity source); fast gate 1638/1638 (criminal-justice loop green), emergence
27/27. Extend the same operator-status gate to weapons/rackets/criminal-production next.

**Perf: prune the append-only evidence pool (latent O(n²)).** Chasing the long-horizon
diagnostic slowdown (which the keystone did NOT resolve) led to the real cause: it was
never crime *volume* — `evidence_pool` is append-only and scanned in full every tick by
~6 modules (investigator_engine, legal_process, media_system, evidence, designer_drug,
alternative_identity). Decayed tokens are retired (`is_active=false`, decay handler
`actionability < 0.01`) but never removed, so the pool grows without bound and per-tick
cost balloons over a multi-year run — a pre-existing O(n²) that Phase 1's more-active
drug economy merely amplified. Fix: the tick orchestrator prunes `is_active==false`
tokens each tick (stable erase-remove, deterministic). Every consumer already skips
inactive tokens, so it's behavior-neutral — fast gate 1638/1638 (incl. determinism
tests), emergence 27/27 unchanged. (A real latent perf fix — but it did NOT resolve the
diagnostic slowdown; see Phase 2 below for the actual cause.)

**Criminal unification Phase 2: break the revenue-feedback explosion.** The diagnostic
data finally pinpointed it: `drug_economy` set `production_output = revenue_per_tick *
0.1` and then wrote that drug revenue back as the business's `revenue_per_tick` — a
positive-feedback loop (`output→revenue→output`). It looked stable only because drug
prices were accidentally tiny (the iron-ore good_id collision, ~12); once Phase 1 gave
drugs their real prices (300–2000), the loop ran at ×30–200/tick and **pinned criminal
wealth at the 1e9 safety ceiling within ~1 year — even in Federation (strong, clean
governance)** (inequality 0.81, grievance crossing 0.5). Fix: output is now a fixed
per-business production capacity (`DrugEconomyConfig::base_drug_output`, a real
throughput limit), bottlenecked by real precursors and scaled by the enforcement
factor — never derived from the business's own revenue. Result: Federation criminal
wealth at yr5 fell from **1.00e9 → 8.88e5** (~1000×), bounded and growing in line with
legit (~7.4e5); the runaway is gone. Keystone (enforcement bite) + governance now
operate on a stable base. NOTE: this did NOT speed up the long-horizon diagnostic
(still ~16 min/regime) — that slowdown is the **cumulative per-tick cost of the whole
resource-economy arc** (energy/waste/fisheries/agriculture/conservation), a separate
profiling/benchmark concern, not the criminal explosion. Remaining criminal grounding:
route synthesis through the production module's real recipes + cannabis cultivation
chain; weapons real goods; designer_drug revenue. Per-tick perf needs a profiling pass.

**Removed the arbitrary wealth caps (crash-sentinel only now).** The `npc_capital_ceiling`
(1e9) and `business_cash_ceiling` (1e10) were band-aids added while runaway *bugs*
existed (banking double-credit; the drug revenue-feedback loop, now fixed in P2) — never
a statement that wealth is capped. With the runaways fixed they were vestigial, and an
arbitrary number capping wealth is itself a fake rail (real fortunes have no ceiling —
there is now a trillionaire). `apply_deltas` no longer clamps capital/cash to a magnitude
cap; it floors capital at 0 and uses the ceilings ONLY as crash sentinels for non-finite
(inf/NaN) values. Wealth is now bounded by what the economy actually produces. Kept:
`business_revenue_ceiling` (a per-tick *rate* limiter that catches explosions without
capping total wealth) and the market supply/price sentinels. Behavior-neutral in current
play (post-P2 nothing approaches the caps): fast gate + emergence unchanged. If a latent
runaway remains it will now surface as large-but-finite wealth (debuggable) rather than a
silent peg at an arbitrary number.

**Decoupled the goods/production layer from the world-gen RNG stream.** Architectural
directive: the dependency must be strictly `world gen → base resources` (deposits/
fisheries/soil/climate), and the goods + production catalog (markets, recipes, facility
assignment) must NOT advance the world-gen RNG — so editing/adding goods or recipes can
never reshape resources, population, or nations (the fragility that cratered legitimacy
in the waste pass and blocked further content work). Fix: `create_markets` and
`FacilityGenerator::create_facilities` now draw from independent forked streams
(`rng.fork(kGoodsLayerRngSalt / kProductionLayerRngSalt)`); `fork()` is const and does
not advance the parent, so the shared stream used by resources/population/nations is
untouched by the catalog. One-time re-baseline of the world for a given seed (markets/
facilities no longer consume the shared stream); determinism holds (forks are
deterministic — same seed → same world), and emergence + world-gen integration are
unchanged: fast gate 1652/1652, emergence 27/27. This unblocks the remaining content
work (cannabis cultivation, weapons goods) — adding recipes/goods is now world-neutral —
and is a foundational step toward a Dwarf-Fortress-style history-generation phase
(generate physical world + resources, then run the orchestrator to grow the economy/
society emergently): physical substrate fixed, economy a decoupled layer on top.

**Mechanical history generation adopted (design decision) + P0 (founding-seed mode).**
Architect chose forward-simulated history over backward-narrated flavor: the starting
world becomes the *true outcome* of running the orchestrator for decades/centuries from
a minimal founding seed, scoped to the **whole planet at LOD 2** (the player's region
materialized to LOD 0 at entry). Recorded in the WorldGen spec + Feature Tier List
(an adopted EXTENSION beyond documented V1); full architecture in
docs/design/EconLife_Mechanical_History_Generation_Plan.md (key enabler: `run_world_years`
already runs the orchestrator headless for N years; gaps are founding seed, entity
genesis, deep-time LOD, player entry). P0 landed: `WorldGeneratorConfig::founding_seed_mode`
(default false → unchanged full-seed "instant world" default) emits the physical
substrate + founding population but **skips the pre-built economy** (no seeded
businesses/facilities) — firms must emerge through history. New test confirms a founding
world has provinces/deposits/population/markets but zero businesses/facilities; default
path byte-identical (additive, guarded skip). Next: P1 history-gen driver (run the
orchestrator forward from the founding seed) + P2 entity genesis (firms/settlements
forming from near-zero) + P3 deep-time LOD.

**History-gen P1: the forward-run driver.** Productionized the headless multi-year run
(previously only the emergence test harness) into a first-class engine capability:
`generate_world_with_history(gen_config, pkg_config, history_years, threads)` (new
`simulation/modules/history_generator.{h,cpp}`, in the `econlife_modules` lib alongside
`register_base_game_modules`). It generates a world, then advances the full base-game
orchestrator for `history_years × 365` ticks before any player agency, returning the
evolved world — history is simply the *gameplay engine running before the player
arrives*. Deterministic from seed (single-threaded pool → reproducible). Integration
tests (`history_gen_integration_test.cpp`, in the fast gate): a founding-seed world runs
2 years forward — valid, finite, deterministic (same seed → identical evolved world); a
full-seed economy survives a 2-year run (businesses persist, capitals finite);
`history_years==0` returns the fresh world unchanged. Founding-seed firms don't bootstrap
yet (no entity genesis — that's P2); P1 proves the driver. Fast gate + emergence green.
Next: P2 entity genesis (settlements/firms forming from near-zero) → P3 deep-time LOD.

## Update 2026-06-16: History-gen P2 (firm genesis) — a founding world bootstraps an economy

**Firms are now born from local opportunity, not just hand-seeded at world gen.**
`BusinessLifecycleModule` gained a continuous, opportunity-driven genesis path
(`genesis_from_opportunity`) that runs on its own monthly cadence (independent of
era transitions). Each province has a supportable firm target = `residents /
firms_per_resident_denominator` (default 10) — the *same ~1-per-10 density world
gen seeds* — so a founding-seed world (zero firms) grows toward the equilibrium the
full seed starts at, while an already-seeded world sits at saturation and genesis
stays quiet (a 10% deadband). Firms are founded by **real local residents who commit
their own capital** (NPC `capital_delta` debit → firm `cash`): no money minted from
nothing, formation bound to accumulated local wealth.

**Two findings drove the design:**
1. *A founding world has no income floor.* With no firms there are no wages, so NPC
   capital drains to ~0 within a month. The first genesis cohort therefore fires at
   the **founding moment (tick 1)**, while founders' savings are intact — the earliest
   entrepreneurs form firms now, and those firms create the wages that sustain later
   formation. (Realistic: settlements bootstrap from whoever can first open a farm/shop.)
2. *Don't drop a modern firm into a vacuum.* Per design feedback, a solo owner-operator
   (farmer, baker, smith, shopkeeper) produces real value from **their own labour applied
   to local resources**, independent of employees. Genesis firm earning power is now
   grounded in *what the firm does with the province's endowment* — agriculture tracks
   `agricultural_productivity` (worked land), processing follows local farming, retail/
   services track the local customer base — not a generic demand number. A solo firm is
   viable on its own (operating profit flows to the owner via production →
   financial_distribution regardless of headcount; lower cost ratio reflects paying
   oneself rather than a payroll). This is the project's resource-endowment grounding
   applied to firm formation: fertile ground sustains farmers, a populace sustains shops.

**Result (seed 42, 6 provinces, 150 NPCs, single-thread history run):** the founding
economy goes from **0 firms → ~11**, median NPC capital 0 → ~1,150 (solvent), aggregate
population capital **growing** 240k → 1.07M over 5 years (was a flat ~77k subsistence
trap before this pass), ~98/149 NPCs solvent. It converges toward — but stays below — the
full-seed equilibrium (~20 firms), exactly the realistic *developing-economy* trajectory:
a founding world matures over its long history run rather than appearing fully-formed.
Genesis perturbs the full seed only marginally (18 → 20 firms; within the deadband) and
**emergence stays 27/27**. Determinism preserved (RNG forked from `world_seed + tick`,
deterministic province/resident iteration).

Validated by `history_gen_integration_test.cpp` (fast gate): a founding-seed world now
bootstraps from zero firms to ≥ provinces firms, all located/legitimate/revenue-positive,
with aggregate capital growing beyond the un-run world (the economy *functions*, not just
exists). Fast gate + emergence green.

Still abstract (facility-less) firms — full production-grounding (genesis firms owning
Facilities that produce real catalog goods from deposits/recipes) is the next genesis
slice, alongside settlement/state genesis. Next: P3 deep-time LOD.

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

---

## 2026-06-21 — Dawn-climb pace + completion (history-gen)

Context: the dawn→present knowledge climb was wildly too fast (era 6 reached in
~180 in-game years) and not believable. Recalibrated the whole commons knowledge
engine so a society climbs from the Neolithic to the modern era on an
accelerating, real-history-like timescale, and added the fast-forward stride so
those millennia simulate in seconds.

What changed (all data-driven / config, no new magic numbers in code):
- **Knowledge-keepers gate by era** (occupations.csv `min_era`): elder at the
  dawn, scribe with writing (era 2), scholar with formal scholarship (era 4) —
  and they LEAD the Layer-2 list so a thin surplus still funds one. The dawn
  knowledge output is now a trickle that accelerates as better keepers unlock.
- **Knowledge is cumulative**: `decay_per_year` 0.02 → 0.00001 (the old value
  imposed a ~50-yr decay-relaxation equilibrium that, not the thresholds, set the
  pace). Thresholds now govern the climb.
- **Comfort margin** (`commons_surplus_margin`): commons demographics chase a
  surplus offset down by the margin, so the population settles at a modest
  permanent surplus (~1.1–1.2) instead of the bare carrying capacity — leaving
  headroom that funds the specialist/knowledge class. Without it the Malthusian
  equilibrium sits at surplus 1.0 with zero specialists (the trap).
- **Saturating knowledge→food coupling** (`knowledge_productivity_max/_halfsat`
  replacing the linear `_coupling`): the carrying ceiling tends toward a realistic
  ~26× limit instead of exploding ~1000× and crashing the surplus in the upper
  eras.
- **Persistent elite floor** (`commons_min_specialists_per_province`): a few
  knowledge-keepers survive even famine/overshoot, so a stalled society keeps
  creeping and climbs back out instead of dying in a Malthusian crash.
- **Era thresholds** (eras.csv `knowledge_to_advance`) recalibrated to the new
  dynamics.

Result (fast-forward, [.society-history]): from the dawn, earthlike reaches the
modern era (era 8) at year ~12,960 — vs ~12,000 of real history — with an
accelerating arc (Neolithic ~5,900 yr; Bronze ~2,200; Iron ~770; Classical/
Medieval/Early-Modern ~800–900 each; Industrial ~1,200). Garden, earthlike,
barren-deathworld and fertile-deathworld ALL complete the climb and classify
Thriving — i.e. a high World-Class world is not doomed (Earth is Class-12).

Known rough edge (follow-up): the upper eras (6→8) run a thin/negative surplus —
the population overshoots the saturating ceiling and the climb completes through
demographic stress rather than comfortable development. Industrial (era 7) is
~1,200 yr vs real ~300 because the rate decelerates there. A firmer demographic
brake (target-seeking, not a fixed offset) would smooth this.

---

## 2026-06-21 (cont.) — Ground the commons economy: granaries, no rails

Per "every action and reaction should be grounded in something," tore out the
three fake rails from the pace work and rebuilt the commons food economy on
conserved, located resources.

REMOVED (fake rails): the persistent-elite floor (specialists conjured from
nothing in famine), knowledge-keeper min_surplus = 0 (an elite that ate no food),
and the commons "comfort margin" (a phantom offset on perceived food).

GROUNDED model (schema v24 adds cohort_stats.food_store):
- **Granary** — a conserved per-province food stock. Production surplus banks into
  it (capped at a few years of consumption); deficits draw it down; stored grain
  SPOILS. Maintaining reserves against spoilage is a permanent, real production
  demand — that, not a margin, is what frees a standing specialist class.
- **Specialists are food-balance-grounded**: the people the farmers don't need to
  feed everyone and keep the granary whole, rising with knowledge (fewer farmers
  needed per head). Capped at a realistic pre-industrial ~15% (most hands stay on
  the land).
- **Birth vs famine decoupled**: fertility tracks long-run productivity (output
  vs need + reserve upkeep); starvation fires only once the granary is empty.
- **Cumulative knowledge** (no decay): a population dip no longer erases technique.
- **Preventive-check demographics**: commons births/deaths use a fixed Malthusian
  balance point, not the modern political-stability proxy.

Emergent outcome (honest, accepted — NOT railed to a guaranteed climb): from the
dawn a society climbs through the agrarian eras at a believable, accelerating pace
(Neolithic ~3,500 yr, then faster) on a thin ~1.07 surplus with ~14% specialists,
then hits the MALTHUSIAN WALL around the Medieval era and STALLS (population at
carrying capacity, surplus gone, knowledge frozen) — classified "Stalled". All
four world archetypes (garden/earthlike/both deathworlds) follow this arc;
Bounty/Class shift the population scale and timing but not the stall, because the
knowledge rate is tied to the (sampled) specialist fraction, not the population.

This is the grounded Malthusian trap that held real agrarian civilizations for
millennia. Breaking past it to industrial/modern needs a grounded BREAKTHROUGH the
bare commons does not yet model — candidates: knowledge production that scales with
the whole population (more minds), market/trade dynamics, or a demographic
transition. Left as the next grounding step rather than faked. Gates green:
1534 unit cases, determinism, history/world_gen/subsistence/population/occupation/
persistence integration.
