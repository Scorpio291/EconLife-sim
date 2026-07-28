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

---

## 2026-06-21 (cont.) — Adversity drives invention (Boserup + Deathworlders)

Per "a harsher world promotes adaptation, like the tech leaps from WW2": knowledge
is no longer produced only by a thin sampled elite. Added a grounded PRESSURE term
to the knowledge engine (KnowledgeConfig.population_innovation_rate + adversity_*):

  production = (dedicated_specialists + population_innovation) * pressure * tech_mult
  pressure   = base + hazard_weight*(world_hazard - gentle) + scarcity_weight*scarcity

- Boserup: a population pressing on its food supply (scarcity) intensifies —
  necessity is the mother of invention. This gives a GROUNDED escape from the
  Malthusian wall (the scarcity at the wall spurs the innovation that lifts the
  ceiling) instead of a hand-placed floor.
- Deathworlders: a hard world (high World-Class hazard) forges capability — higher
  baseline drive.
- Scales with the WHOLE population (more minds), not the 200-NPC sample — so the
  World-Class/Bounty spectrum finally MATTERS.

Era thresholds rescaled (~1.8x) to anchor Class-12 earthlike (real Earth) near the
real ~12,000-yr dawn->modern span. New classification: reaching a market era =
Thriving (climb complete); commons progress = Developing; climbed-then-flat =
Stalled.

Emergent spectrum ([.society-history], fast-forward), and it now reads like the
premise intended:
- GARDEN (Class 2.5, comfortable): slowest — only Bronze Age after 13,000 yr.
  Low pressure -> complacency -> creeps. (Developing)
- EARTHLIKE (Class 12): reaches the modern era ~year 11,000 — ~ real Earth.
  (Thriving)
- FERTILE DEATHWORLD (Class 13 + rich): FASTEST to modern (~9,950) — adversity
  AND resources. (Thriving)
- BARREN DEATHWORLD (Class 13 + poor): driven hard but too few minds — develops
  then honestly STALLS at the Medieval wall. (Stalled)

So: comfortable worlds stagnate, hard worlds are driven, and a hard world still
needs the population/resources to carry the breakthrough — Earth (a Class-12
deathworld) thrives. Honest stalls remain for marginal worlds; nothing is railed.
Gates green: 1534 unit, determinism, history/world_gen/subsistence/population/
knowledge/occupation/persistence integration.

---

## 2026-06-24 — Pre-industrial era band (feudal / mercantile / industrial)

Continued the era build forward from the agrarian+money band. Eras 5–7, which were
placeholder `barter`, now carry their canonical economic regimes per
docs/design/EconLife_Historical_Eras_and_Tech_Arc.md:
- 5 Medieval    -> `feudal`     (guild / manor / town)
- 6 Early Modern -> `mercantile` (workshops, shipping, early finance)
- 7 Industrial   -> `industrial` (factory system, wage labour)

eras.csv + era_catalog builtin; the dawn-climb modules (subsistence, knowledge,
population_aging) and the society-evolution harness now span these regimes by NAME
(they spanned them as "barter" before — the rename makes the placeholder removable
without switching the climb off mid-arc). Specialist (non-farming) ceiling extended:
money 0.22 -> feudal 0.27 -> mercantile 0.35 -> industrial 0.45. Unit suite green
(1544); society suite green; emergence gate green (1 failed-as-expected = the
deferred national_legitimacy invariant).

### Finding: the late-dawn Malthusian wall pins specialists at ~0 (next lever)
A/B verified the new ceilings are BEHAVIOR-NEUTRAL in the current calibration —
[.society-history] is byte-for-byte identical before/after the rename, with spec
reading **0% across eras 5–8** on every archetype. The ceiling is not the binding
constraint there: by era ~3 the cumulative `food_mult` hits its **6.0 cap**
(technology_catalog.cpp aggregate_effects) and `knowledge_factor` has saturated, so
the carrying ceiling plateaus while population keeps growing into it. At that
Malthusian wall `farmers_needed ≈ population`, so the food balance frees ~0
specialists regardless of the regime ceiling. The era-5 agricultural revolution
(heavy plough, three-field, watermill) and era-7 mechanized agriculture are
authored but **inert** — capped out.

Experiment (cap 6 -> 12, reverted): specialists DO reappear (earthlike era 6 -> 26%)
but only in TRANSIENT bursts right after each agricultural advance, absorbed by
population catch-up within one era; and it is a spectrum-wide rebalance — earthlike
reaches modern at 9727 (was 10917), populations ~2x, and the fertile GARDEN world
regresses into a population trap (stalls at era 3: abundant food -> max population
-> no specialists -> no knowledge). So making late-dawn specialization emergent is
a dedicated calibration milestone (raise/retune the food ceiling AND damp the
population catch-up so a standing specialist class survives the wall), not a safe
rider on the regime rename. Tracked here; the ceilings are staged for when it lands.

---

## 2026-06-25 — Grounding the historical climb in real era timing + research acceleration

Goal: make the dawn->modern climb and the research curve track real history, not just
end near the right total span. Two grounded changes, validated against the real
per-era calendar (eras.csv start_year is the historical anchor).

### 1. The "great acceleration" was capped away (research progress)
`aggregate_effects` capped `knowledge_mult` at 8.0, and the cumulative tech-tree
product hit that cap by the MEDIEVAL era. So the two most consequential meta-
inventions in history — the **printing press (2.0x)** and the **scientific method
(2.0x)** at era 6, plus calculus/newspaper — had ZERO effect. The model could not
reproduce the post-1450 explosion of knowledge. Raised the cap to 200 so the
"learning to learn" stack (writing -> alphabet -> university -> printing ->
scientific method) fully compounds. Safe by construction: the subsistence
`knowledge_factor` (food carrying ceiling) is already saturated in the late dawn, so
uncapping only accelerates ERA ADVANCEMENT, not food/population.

### 2. Era thresholds recalibrated to the real calendar (the climb)
`knowledge_to_advance` retuned (4-iteration empirical fit via [.society-knowledge-
trace] / [.society-history]) so the Class-12 EARTHLIKE anchor (real Earth) reaches
each era within ~150 yr of its true year-since-founding. New thresholds:
  neolithic 4200, bronze 13000, iron 20000, classical 39000, medieval 90000,
  early_modern 240000, industrial 445000.

EARTHLIKE climb vs real history (year since founding / era duration):
| Era         | real start | sim year | real dur | sim dur |
|-------------|-----------:|---------:|---------:|--------:|
| Bronze      | 6700       | 6851     | 6700     | 6851    |
| Iron        | 8800       | 8811     | 2100     | 1960    |
| Classical   | 9450       | 9534     | 650      | 723     |
| Medieval    | 10500      | 10576    | 1050     | 1042    |
| Early Modern| 11450      | 11481    | 950      | 905     |
| Industrial  | 11750      | 11757    | 300      | 276     |
| Modern      | 12000      | 11999    | 250      | 242     |

Modern reached at year 11,999 (real ~12,000). Every era duration within ~140 yr of
its historical value, and the SHAPE is now faithful: millennia-long ancient eras,
then the sharp scientific/industrial acceleration (era 7 knowledge rate 545/yr,
era 8 850/yr) the uncapped meta-inventions produce — instead of the old flat
~530 yr/era.

Spectrum preserved/sharpened (13,000-yr window): GARDEN (comfortable) crawls to
Bronze (Developing); EARTHLIKE reaches Modern ~12,000 (Thriving); FERTILE DEATHWORLD
(adversity+resources) is fastest to Modern ~10,840 (Thriving); BARREN DEATHWORLD
(too few minds/resources) honestly Stalls at Classical. Comfortable worlds stagnate,
hard worlds are driven, breakthroughs still need the population/resources to carry
them — nothing railed.

Gates: 1544 unit, society suite, emergence (1 failed-as-expected = deferred
national_legitimacy). The Malthusian-wall finding from the prior band still stands
(spec ~0% in the late dawn — population grows into the fixed food ceiling); grounding
late-dawn URBANIZATION remains the separate food/population calibration milestone.

---

## 2026-06-25 — M1: earned medieval urbanization (food tech tree expressive; wall intact)

First build milestone of the Medieval Band (see EconLife_Medieval_Band_Expansion_v01.md).
Goal: let a productive society *earn* a medieval urban/specialist class from real
agricultural surplus — WITHOUT infringing the Malthusian wall (no population damping,
no reserved non-farm fraction, no restoring force). Resolves design decision **D1**.

### The bug it fixes
`aggregate_effects` capped cumulative `food_mult` at 6.0 — which bit at era ~2, so the
ENTIRE agricultural arc (incl. the medieval revolution: heavy plough, three-field,
horse collar, watermill) was flat and invisible. Technique could not raise the
carrying ceiling, so no society could ever break the wall upward → spec pinned ~0 in
the late dawn (the long-standing finding). Raised the cap to 40 so the food tech tree
is fully expressive across V1 eras — mirroring the knowledge_mult=200 decision (tech
tree drives the curve; thresholds set the pace). food_mult is dawn-only (subsistence
module), so the modern economy / emergence gate are unaffected (verified).

### The mechanism (wall-respecting)
Each era's food techs raise that era's carrying ceiling; population then grows into it
and the wall reasserts. Specialists are the TRANSIENT surplus pulse between an
agricultural advance and its reabsorption — exactly the historical pattern. Verified
on EARTHLIKE (fine trace):
  - classical end (~yr 10000): surplus 0.91, spec 0%  (Malthusian deficit at the wall)
  - MEDIEVAL (~yr 10500): surplus 1.21, **spec 26%**  (heavy-plough revolution frees a
    quarter of the population into specialists — the earned urban class)
  - industrial (~yr 11000): **spec 44%** (mechanized-agriculture pulse)
The pulses land on the real agricultural revolutions (era 5, era 7); era 6
(early-modern) has no ag tech and no ag pulse — historically right (its urbanization
is trade-driven, the mercantile band's job).

### Re-calibration (the food curve shifted population -> knowledge -> pace)
Higher ceiling -> larger population -> larger knowledge population_term -> faster
climb, so era thresholds were re-tuned (3 iterations) to restore the grounded timing.
New `knowledge_to_advance`: 4200 / 13000 / 20000 / 52000 / 280000 / 950000 / 1900000.
EARTHLIKE durations vs real: Neolithic 6851/6700, Bronze 1944/2100, Iron 612/650,
Classical 1098/1050, **Medieval 956/950**, Early-modern 293/300, Industrial 216/250;
Modern reached **year 11,970** (real ~12,000). Calibration feasible because population
is bounded per-era by that era's food ceiling, so within-era rate stabilizes.

### Spectrum — the wall gates who urbanizes (intended, per "not all societies survive")
GARDEN (comfortable, bounty 1.8): stalls at Bronze (Developing) — abundance breeds
stagnation. EARTHLIKE: Modern ~11,970 (Thriving) with the 26% medieval class. BARREN
DEATHWORLD: Stalled at Classical — never reaches medieval, EARNS NO urban economy.
FERTILE DEATHWORLD: fastest to Modern ~10,818 (Thriving). Marginal worlds simply don't
reach medieval, so they get no medieval economy — not granted, not forced.

Gates: 1547 unit, society suite, emergence (1 failed-as-expected = deferred
national_legitimacy). Next: M2 grain logistics (the tyranny of the ox), then M3
pre-market genesis gated on the catchment's freed specialists + proto-capital.

---

## 2026-06-25 — M2: grain logistics (the tyranny of the ox)

Second build milestone of the Medieval Band. New `grain_logistics` module grounds the
spatial limit on surplus: a draft team eats the grain it hauls, so surplus has a hard
economic radius and water transport is far cheaper than land.

- subsistence now publishes absolute `grain_surplus` (output − need) per province.
- `grain_logistics` (global, dawn-gated, runs_after subsistence) computes per-province
  `net_feedable_surplus`: each province allocates its surplus across {self + neighbours}
  weighted by the ox-cart delivered-fraction; the team eats (1−df) of each haul.
  CONSERVED: delivered + eaten == exported (a source allocates its surplus exactly
  once). delivered_fraction is pure: water (river ~0.95 / maritime ~0.975) vs land
  (~0.5); mountains → 0 (impassable); roads raise it; heavier gravity lowers it (the
  §5.5 gravity→haulage coupling, brought in here). Single-hop neighbours for now
  (multi-hop + real centroid distance = the D6 refinement).
- New cohort_stats fields grain_surplus / net_feedable_surplus (transient, not
  persisted) + RegionDelta replacement fields + apply routing.
- Publish-only: nothing consumes net_feedable_surplus yet (M3 genesis), so the M1
  climb/spectrum is unchanged — verified (society + emergence gates green, in-suite
  determinism holds).

Built as the general "link → deliverable fraction" case so the space age (lightspeed,
EconLife_Logistics_and_Political_Scale_v01.md) extends it with latency rather than
duplicating it.

Gates: 1553 unit (+6 grain_logistics), society suite, emergence (1 failed-as-expected
= deferred national_legitimacy). Next: M3 — pre-market genesis gated on the catchment's
net_feedable_surplus + proto-capital (workshop/guild-shop/manor/castle).

---

## 2026-06-25 — M3: catchment urban population (aggregate medieval town economy)

Third build milestone. Consumes M2's net_feedable_surplus into a per-province
`urban_population` (cohort_stats) = catchment surplus / per-capita food, capped at
population — the aggregate medieval town economy. River/coastal hubs grow towns,
stranded inland stays rural (unit-proven: a river-fed hub out-towns a land-fed one).

Architectural decision: history-gen is COHORT scale (~30M pop), so the town economy is
an AGGREGATE (urban_population), not per-firm entities. Individual workshop/guild-shop/
manor/castle entities materialize at PLAYER ENTRY (M7) from the cohort aggregates, per
the history-gen plan. Design doc §4 reframed accordingly.

Conserved, dawn-gated, publish+observe (M1 climb unchanged — verified, society +
emergence gates green). grain_logistics publishes urban_population; the society
harness/observe capture it (new urban% column in [.society-history] and
[.society-knowledge-trace]).

Honest calibration finding (surfaced by the new observability): in the current
food-uncapped calibration urbanization PULSES with surplus and peaks in the GROWTH
eras (classical ~17-23% urban), then sits Malthusian-pinned (~4%) through the late
dawn — population grows fast enough to eat each ceiling-rise before a town can sustain.
This is consistent with the inviolable wall (towns need real surplus; at equilibrium
there is little), but it means the medieval urban LEVEL is currently modest. Tuning it
up (a bigger/longer medieval ag-revolution pulse, or slower late-dawn population
catch-up WITHOUT damping the wall) is a wall-respecting calibration follow-up — NOT a
mechanic change. The catchment->urban mechanic itself is correct and conserved.

Gates: 1555 unit (+8 grain_logistics), society suite, emergence (1 failed-as-expected
= deferred national_legitimacy). Next: M4 medieval content (goods/recipes/facilities),
then M5 feudal layer, M6 hazards+war, M7 entry (entity materialization).

---

## 2026-06-25 — M4: medieval content (goods / recipes / facilities)

Fourth build milestone — pure data (CSV), the era flesh for the medieval band. The
catalogs were 100% modern-anchored (0 goods/recipes before era 8); now era-≤5 content
exists so a medieval economy has something to make and trade.

- Goods: backfilled era_available on 6 basics (wheat→1, flour→2, lumber→1, iron_ore→3,
  salt→1, beer→2) + 10 new (bread, wool, wool_cloth, charcoal, wrought_iron,
  iron_tools, raw_hide, leather, draft_oxen, fodder — the ox of §3.5 + its fodder).
- Recipes (8): grain_milling, bread_baking, ale_brewing, hide_tanning, charcoal_burning,
  iron_bloomery (ore+charcoal→wrought_iron), wool_weaving, tool_smithing. Real chains:
  grain→flour→bread, grain→ale, wool→cloth, hide→leather, wood→charcoal,
  ore+charcoal→iron→tools. Labor-only (energy/mechanical/fuel = 0; no anachronistic
  engine power — water/muscle abstracted into labor; charcoal/ore are input GOODS).
- Facilities (9): watermill, bakery, brewery, weaver, tannery, charcoal_kiln, bloomery,
  smithy, manor_farm.

recipe↔goods cross-validation clean (no unknown-good errors at world-gen). New
regression test ([medieval]) asserts the full set is available at an era-5 start and a
modern-only good (steel, era 8) is not. The era-≤5 goods are also available in the
modern era (era_available <= 8) — they belong there too (modern bakeries make bread) —
and the emergence baseline still passes (modern economy absorbs them; determinism
holds). The production economy that consumes this content is instantiated at player
ENTRY (M7); during history-gen the commons food economy still runs.

Gates: 1555 unit, world_gen integration (12 cases) + new [medieval] test, society
suite, emergence (1 failed-as-expected = deferred national_legitimacy). Next: M5 feudal
layer (manorial tithe + garrison/security loop, guilds, towns & fairs).

---

## 2026-06-25 — M5: feudal layer (manorialism — tithe -> inequality)

Fifth build milestone. The defining feudal mechanic that makes the regime behave
DISTINCTLY from the egalitarian commons: manorialism. In the stratified regimes
(feudal/mercantile/industrial) a tithe concentrates the new proto-capital toward a
lord stratum (the first manorial_lord_fraction of residents by index) instead of the
commons' even split. Conserved (the SAME proto-capital, skewed — summed over residents
== total; unit-proven), climb-preserving (only wealth distribution changes, not
knowledge/population). SubsistenceModule::proto_share_for (pure/static) +
manorial_regimes / manorial_tithe_rate / manorial_lord_fraction config.

Emergent narrative observed (earthlike capital gini at era reaches): founding 0.42 ->
the commons FLATTENS inequality to ~0.26 (everyone farms, even split) -> feudal/
mercantile RE-STRATIFY to ~0.37-0.39 (lords). The lord/peasant divide, earned and
conserved, and the concentrated capital is the founder war-chest for entry-time firm
genesis. Gini now surfaced in [.society-history].

Scope: the garrison/security loop folds into M6 (war/predation consume security);
guild entry/quality/price + towns/fairs are M7 entry materialization (the firm/market
layer). M5 ships the class-structure core.

Gates: 1557 unit (+2 manorialism), society suite, emergence (1 failed-as-expected =
deferred national_legitimacy). Next: M6 — hazards + war (the seven world-class hazards
brought in distinctly; war via the grain-protection garrison loop).

---

## 2026-06-25 — M6a (disease): the first world-classification hazard, brought in distinctly

Sixth-band milestone, first hazard. The seven world-class hazards previously collapsed
into one flat mortality scalar; disease is now an EPISODIC shock — the plague check.

- population_aging::epidemic_mortality_factor (pure/static): each pre-market year a
  province may suffer an outbreak — a mortality spike of 1 + severity*disease*(1+density).
  Outbreak probability = base * disease_dial * (1 + density_weight * urban_fraction),
  capped. So plaguier worlds (high disease dial) AND crowded provinces (M3
  urban_population fraction — towns are vectors) outbreak more → disease is a natural
  BRAKE on urbanization. Conserved (deaths via the existing cohort mortality path);
  deterministic (per-province/tick RNG). Folded into hazard_mortality; gated to the
  commons arc so medicine RELEASES disease-as-population-check in the modern era (a
  grounded hockey-stick cause).
- Config: PopulationAgingConfig epidemic_base_rate/density_weight/max_prob/severity.

Effect: bites in the urban growth eras (2-4, ~21-25% urban) where towns exist, barely
in the rural late dawn — the intended density brake. Calibration-safe: earthlike modern
11,970 -> 12,044 (+74yr, ~0.6%); every era duration still tracks history (medieval
967 vs 950). The plague dips are yearly (smoothed at 500-yr sampling); dramatic
Black-Death plateaus need the multi-year + spread + pandemic refinement (deferred).

Gates: 1558 unit (+1 epidemic), society suite, emergence (1 failed-as-expected =
deferred national_legitimacy). Next M6: geology disasters + seasonality harvest
failures (episodic), the chronic radiation/atmosphere split, the feudal couplings
(gravity->haulage, predators->herds), then M6c war & diplomacy.

---

## 2026-06-25 — M6a episodic shocks complete: geology disasters + seasonality harvest failures

Added the other two episodic hazards alongside disease (M6a now covers all three
episodic shocks; each is an independent stochastic, pre-market-gated, conserved check):
- GEOLOGY (population_aging::disaster_mortality_factor): quake/storm/wildfire mortality
  spike scaled by the geology dial, NOT density-dependent. Independent RNG.
- SEASONALITY (subsistence::harvest_failure_factor): episodic bad-harvest output cut
  scaled by the seasonality dial, folded into base_ceiling. Seeded by YEAR (not tick)
  so a failure is consistent across a year at any tick resolution (correct for both
  fast-forward and full-res) and varies year to year.

Both pure/static + unit-tested (dial=0 -> never fires; rate rises with the dial; the
spike/cut matches the deterministic formula). Disease/geology fold into hazard_mortality;
seasonality into the food ceiling -> famine via the existing loop.

Calibration: cumulative M6a drift modern 11,970 -> 12,349 (+3.2%); shape intact
(neolithic 7109, bronze 2004, iron 630, classical 1112, medieval 978, early-modern 297,
industrial 219). The episodic hazards realistically slow the climb (plague/quake/famine
set societies back). A single threshold re-calibration to restore modern ~12,000 is
DEFERRED until the full hazard suite (chronic split + couplings) + war land — chasing it
mid-suite is a moving target.

Note: a harvest failure briefly dropped an existing subsistence specialist-assignment
unit test below threshold (the test rolled a bad year). Fixed by isolating the
surplus/specialist/proto-capital unit tests from the harvest hazard (seasonality=0 in
those test worlds) — the new mechanic correctly couples surplus to harvest variance.

Gates: 1560 unit (+2: geology, seasonality), society suite, emergence (1
failed-as-expected = deferred national_legitimacy). Remaining M6: chronic radiation/
atmosphere split, feudal couplings (gravity->haulage, predators->herds), multi-year/
spread; then M6c war & diplomacy.

---

## 2026-06-25 — M6a complete: chronic hazard channels (radiation, atmosphere) + predator coupling

Added the remaining distinct hazard channels, completing M6a (all seven world-class
hazards now act through their signature channel, additive to the background mortality
scalar — a hazard kills via the scalar AND has its specific effect):
- PREDATORS -> herd/food penalty (subsistence), strongest early and WANING as
  accumulated knowledge clears them (predator_food_factor; the §5.5 coupling).
- ATMOSPHERE -> carrying-ceiling cap (subsistence), planetary, never wanes
  (atmosphere_ceiling_factor).
- RADIATION -> fertility depression (population_aging), planetary, applies in ALL eras
  (radiation_fertility_factor; new fertility_mult param on process_births_deaths).
- GRAVITY -> haulage was already in M2 (grain_logistics::delivered_fraction).

All pure/static + unit-tested. Earth-level penalties kept SMALL (they compound over
the 12,000-yr climb): predator 0.10, atmosphere 0.12, radiation 0.18 (reduced from
0.25/0.30/0.40 after the initial values over-slowed earthlike past the 13,000 window).

Calibration: cumulative M6a drift earthlike modern 11,970 -> 12,822 (+7%). Anchor
holds (Thriving). Spectrum intact and correctly ordered: GARDEN stagnates at Bronze;
EARTHLIKE -> Modern ~12,822; FERTILE DEATHWORLD fastest ~11,617; BARREN DEATHWORLD
Stalls at Classical (survives — not extinct). The conquerable/planetary split is live:
predators (and disease/geology/seasonality) wane or release; gravity/radiation/
atmosphere are permanent.

A single threshold re-calibration to restore modern ~12,000 is DEFERRED until after
M6c war (must follow war, else earthlike could stall under the combined load).

Gates: 1562 unit (+2: predator/atmosphere, radiation), society suite, emergence (1
failed-as-expected). M6a DONE (bar optional multi-year/spread epidemics). Next: M6c war
& diplomacy (the polity-level engine — rational war, treaties, alliances, backstabbing,
empires).

---

## 2026-06-25 — M6c-1: war foundation (power, EV decision, conserved casualties)

First slice of the war & diplomacy engine. New `warfare` module (global, dawn-gated,
runs_after subsistence): at the dawn each province is a proto-polity (chiefdom/lord).

- POWER (WarfareModule::military_power, pure/static): population × how well-fed it is
  (power_surplus_floor + (1-floor)×clamp(surplus_ratio)) — levy + surplus-fed soldiers.
- REACH: adjacency (the ox-cart reach) — you can only attack a neighbour you can march to.
- EV DECISION: a polity attacks a neighbour only when power_A >= aggression_ratio×power_B
  (strike where you can win), with a base annual probability. Directional, deterministic
  (per-(year,attacker,defender) RNG; seeded by YEAR for tick-resolution consistency).
- CONSERVED CASUALTIES: war publishes per-province war_mortality (>=1, defender worse
  than attacker), folded into mortality by population_aging — the population war-dips.
  Spoils deferred (war here only kills; no minting).

New cohort_stats.war_mortality + RegionDelta + apply; WarfareConfig (aggression_ratio
1.3, base_aggression 0.04, attacker_loss 0.02, defender_loss 0.06).

Tests (+4): power scales with population & feeding; a strong polity attacks a weak
reachable neighbour (both bleed, defender worse, formula-exact); evenly-matched
neighbours stay at peace; inert in market eras. Gates: 1566 unit, society suite,
emergence (1 failed-as-expected). Spectrum intact: earthlike modern 12,822 -> 12,826
(negligible — wars are occasional border conflicts among the similar founding provinces,
not constant), deathworlds survive, garden stagnates.

REMAINING M6c (per EconLife_War_and_Diplomacy_v01.md): grain/territory SPOILS (makes
starting a war rational), diplomatic relations + treaties + alliances (balance of
power) + backstabbing (reputation economy), the EMPIRE layer (reach/leadership/mobility
+ the hold problem — Alexander/Genghis/Rome). Then the single deferred threshold
re-calibration to restore modern ~12,000 under the full hazard+war load.

---

## 2026-06-25 — M6c-2: war spoils (conserved plunder + rich-target EV)

Second war layer, making STARTING a war rational (attack the weak AND rich, then loot):
- The attack probability now scales with the defender's WEALTH share (prize_weight) —
  a rich neighbour is a more tempting target (the EV: weak AND rich).
- A won war PLUNDERS plunder_fraction (0.2) of the loser's resident proto-capital to
  the victor. CONSERVED: sequential depletion across multiple attackers; the loser's
  residents debited proportional to their wealth (never below zero), the victor's
  credited equally; global sum nets to zero (no minting). Emits NPCDeltas (capital).

Tests (+1): a won war plunders the loser's wealth to the victor, conserved
(0.2 x 1000 -> loser 800, victor 400, total unchanged). Gates: 1567 unit, society
suite, emergence (1 failed-as-expected).

Environment note: mid-session the container re-cloned onto an unrelated ancestor (a
merged PR) and wiped build/; my work was intact on the remote and restored via
git reset --hard origin/<branch>. The org egress policy 403s the FetchContent deps
(Catch2/lz4/h3/nlohmann_json from github), so the build was recovered with SYSTEM
packages (apt: catch2 3.4, libh3 4.1, liblz4 1.9.4, nlohmann-json 3.11.3) via
-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS plus two local shim configs (lz4_static,
and a bare `h3` alias) on CMAKE_PREFIX_PATH — no committed build-file changes. Caveat:
system h3 is 4.1.0 vs the pinned 4.4.1, but all gates (incl. society/emergence, which
exercise world-gen) passed, so the behavioural baselines are robust to that skew.

---

## 2026-06-25 — M6c-3: diplomatic relations (war sours, peace warms, alliances deter)

Third war layer — the substrate for treaties/alliances. The warfare module now holds
per-province-pair relations (module state, [-1,1], starts at 0):
- A war DROPS the pair's relation (relation_war_hit 0.3); a peaceful adjacent year
  HEALS it (relation_peace_heal 0.02) — so sustained peace warms neighbours into a
  de-facto alliance while feuds fester.
- Warm relations DETER an attack: attack_prob *= (1 - relation_deter_weight *
  max(0, relation)). A full alliance (relation ~1) with deter_weight 1 fully prevents
  war — allies don't fight.

Deterministic (ordered pair sets). New: pair_key()/relation() helpers, relations_ map.
Note: relations_ is module state not yet serialized (save/load follow-up; not exercised
in the dawn lab, which runs a single climb).

Tests (+2): a war sours relations (goes negative); 60 peaceful years warm a pair to a
deterring alliance that spares a now-dominant neighbour from attack. Gates: 1569 unit,
society suite, emergence (1 failed-as-expected).

Remaining M6c (per EconLife_War_and_Diplomacy_v01.md): explicit TREATIES (formal peace
with terms), ALLIANCES (mutual defence / balance-of-power coalitions vs a hegemon),
BACKSTABBING (rational betrayal + the reputation economy), and the EMPIRE layer
(reach/leadership/mobility + the hold problem — Alexander/Genghis/Rome). Then the single
deferred threshold re-calibration, then M7 entry.

Build note: this session's build ran against apt system deps (Catch2 3.4/h3 4.1/lz4
1.9.4/json 3.11.3) + find_package mode + local shims, after the org egress policy
blocked the pinned FetchContent deps. All gates pass under that skew.

---

## 2026-06-25 — M6c-4: alliances (balance of power) + backstabbing (reputation economy)

Fourth war layer, both emergent from the M6c-3 relations substrate:
- COALITION DEFENCE (balance of power): a defender's ALLIES (warm-relation neighbours,
  relation >= ally_threshold 0.3) add their power to its defence, so an attacker must
  out-match the whole coalition. A rising hegemon is checked by the coalitions its
  strength provokes -> conquest is bounded by default.
- BACKSTABBING (reputation economy): attacking a warm-relation ally is a betrayal; on
  top of the normal war-hit, the betrayer's relations with ALL its neighbours sour
  (backstab_reputation_penalty 0.25). A known backstabber loses its alliances and gets
  ganged up on (self-limiting) -- so betrayal is occasional and costly, not constant.

Deterministic (ordered pair/betrayer sets). Tests (+2): a B+C coalition deters a
would-be conqueror A that could beat B alone; A betraying ally B also sours A's relation
with the uninvolved ally C (the pariah brand). Gates: 1571 unit, society suite,
emergence (1 failed-as-expected).

The rational-war core is now complete: power (economy) + reach (adjacency) + EV (attack
the weak AND rich) + conserved casualties & plunder + relations (sour/warm/deter) +
coalitions (balance of power) + backstabbing (reputation). Remaining M6c: the EMPIRE
layer (reach/leadership/mobility multipliers + the hold problem -> Alexander/Genghis/
Rome), then the deferred threshold re-calibration, then M7 entry.

---

## 2026-07-02 — adversarial review of the war engine (d600b7b..HEAD) + fixes

Ran a high-effort multi-angle review of the M6c commits (8 finder angles + verify
pass). Ten findings survived; the four correctness ones are FIXED in this commit:

1. **Missing annual gate (CONFIRMED, critical).** warfare ran every daily tick while
   every rate is per-year and the attack RNG is year-seeded — in full-resolution runs
   the same war re-fired all 365 ticks: plunder compounded (0.8^365 ~ total
   confiscation) and diplomacy drifted 365x too fast (alliances saturated in ~50 days,
   then suppressed nearly all war). Unit tests masked it (they step whole years); the
   spectrum baselines used fast_forward (1 execute/year) so they accidentally measured
   the intended semantics. FIX: one decision pass per year
   (current_tick % kTicksPerYear != 0 -> return).
2. **Stale war_mortality on regime exit (CONFIRMED).** The regime-gate early return
   published no reset, so the last war spike persisted forever if warfare's config
   regime list diverged from population_aging's hardcoded commons list. FIX: publisher
   owns the signal — war_state_dirty_ flag + one-time 1.0 reset on regime exit; the
   consumer now applies war_mortality unconditionally (it is 1.0-neutral at peace)
   instead of behind a second regime list.
3. **Plunder conservation leak (PLAUSIBLE).** A victor province with no valid
   significant-NPC residents was debiting the loser with no matching credit —
   destroying wealth. FIX: no transfer at all unless the victor can receive
   (victor_can_receive gate), and credit is split across VALID residents only.
4. **relations_ unserialized (CONFIRMED).** Save/load silently reset all diplomacy.
   FIX: serialize_state/deserialize_state v1 (relations_ + reset flag); persistence
   auto-discovers it. Round-trip unit test added.

Conventions/cleanup findings also addressed: population_aging INTERFACE.md updated
(war_mortality + the M6a inputs); NEW docs/interfaces/warfare/INTERFACE.md and
docs/interfaces/grain_logistics/INTERFACE.md; NEW warfare integration test
(control-vs-war, same seed, 366 daily ticks under the real orchestrator — proves ONE
annual plunder [ratio ~0.8, not ~0 compounded] and conservation; it also surfaced that
npc_indices_by_home_province is empty until the first apply_deltas rebuild, so tick
0's war has no prize — latent world-gen quirk, noted). Reuse: shared
build_h3_to_province_index (geography.h), shared regime_in (era_catalog.h), canonical
kTicksPerYear (shared_types.h) — warfare/grain_logistics/knowledge/subsistence/
population_aging/lod_system migrated off their local copies.

Noted, not fixed here: RegionDelta region_id fan-out is a latent codebase-wide 1:1
assumption (~10 modules); per-tick always-emit RegionDeltas and minor map-lookup
simplifications — deferred.

Gates: 1,575 unit (266,444 assertions; +2 warfare cadence/reset, +1 no-plunder-
without-receiver, +1 serialization round-trip), 37 integration (13,211; + the new
warfare orchestrator scenario), emergence (1 failed-as-expected). Behavioral note:
full-res daily-tick dawn dynamics CHANGE with the annual gate (they were 365x off);
fast-forward spectrum semantics are unchanged by construction.

---

## 2026-07-02 — M6c-5: emergent nesting polities (conquest absorbs, hold-failure secedes)

The empire substrate, per the §5.4 correction (ownership is not seeded — it emerges
from settlement and nests upward: settlement -> city -> province -> kingdom -> empire):
- EMERGENT: polity_of_ is sparse; a province never conquered is its own polity.
  Unsettled/unpopulated land is ownerless (power 0 -> never a war party).
- CONSOLIDATION BY CONQUEST: repeated decisive wins (absorb_after_wins=3 per directed
  pair) absorb the loser's WHOLE polity into the victor's — a beaten kingdom joins the
  empire with all its provinces (nesting, not per-province flipping).
- EMPIRE PEACE + POOLED POWER: members never war each other and field the polity's
  summed power for defence AND offence (the coalition mechanic, via membership). The
  emergent flip side showed up immediately in tests: a kingdom raids neighbours
  through its border members with pooled weight.
- THE HOLD PROBLEM: a member whose own power exceeds secession_power_ratio (0.8) x the
  rest of its polity SECEDES (the centre can no longer overawe it) — the ladder drops
  a level; successor states are the members below. This is the Alexander/Genghis/Rome
  fall mechanic in its simplest grounded form (the rise multipliers — leadership,
  mobility, roads — are the remaining W6/W7 layers).
- Serialization v2 (polities + win ledger; v1 blobs still load).

Tests (+3): 3 wins absorb -> empire peace inside; the kingdom deters what a lone
province could not (and raids C through the border member — asserted as intended);
centre collapse -> secession -> re-absorption -> save/load round-trips the political
map. Two prior tests updated where the new mechanics invalidated their assumptions
(coalition test now disables annexation to isolate relation-coalitions; pool test
asserts no DEFENDER loss on the member). Gates: 1,578 unit (266,459), 37 integration
(13,211), emergence (1 failed-as-expected).

Remaining M6c: W6 leadership (Alexander) + W7 military-type reach (Genghis) + roads/
cohesion (Rome) as power/reach/hold modifiers on this substrate; then the single
deferred threshold re-calibration; then M7 entry.

---

## 2026-07-02 — M6c-6: the conqueror multipliers (Alexander, Genghis, Rome)

The rise mechanics on the polity substrate — how a rare empire BREAKS the bounded-war
default, each archetype through a different gate (design §5.5, W6/W7/W8):
- ALEXANDER (leadership): rarely (leadership_rate 0.004/yr/seat, deterministic per
  (seat,year)), a polity seat produces a great commander whose tenure (30yr)
  multiplies the polity's power x2.5. His death removes the multiplier — and the hold
  problem fragments what institutions never caught up with. Unit test runs the whole
  arc: a 40k polity that could never attack a 60k neighbour conquers it in one
  campaign under a commander, holds it while he lives, and loses it to secession the
  year his tenure ends (the Diadochi).
- GENGHIS (mobility): a polity whose power is predominantly steppe-bred (arable <0.2,
  forest <0.3; >=50% of polity power) fights as CAVALRY — herd-fed, no grain line —
  and strikes 2-HOP targets, past the ox-cart adjacency. Test: farmland A cannot
  touch a rich target two hops away; the same A as steppe raids it.
- ROME (cohesion): integration grows with tenure — the secession threshold scales by
  1 + 0.02/yr held (capped 3x), so a fresh conquest is as fragile as the day it was
  taken while a province held for generations endures. Test: the same borderline
  member secedes when freshly absorbed and holds when 150 years integrated.

Also fixed a subtle coalition bug the new tests surfaced: an ally belonging to the
ATTACKER'S own polity no longer adds its power to the defender's coalition (a member
cannot defend an outsider against its own empire). Serialization v3 (leaders +
membership tenure; v1/v2 blobs still load). Conqueror state injected in tests via
hand-built v3 blobs (deterministic asymmetry).

Gates: 1,581 unit (266,473), 37 integration (13,211), emergence (1 failed-as-expected).
M6c COMPLETE: foundation -> spoils -> relations -> alliances/backstab -> nesting
polities -> conquerors. Remaining before M7: the single deferred threshold
re-calibration (modern ~12,000 under the full hazard+war load), ideally on a
pinned-deps environment.

---

## 2026-07-03 — G2: the grounded war economy (no rails; every constant in real units)

Full rewrite of the warfare mechanics per the grounding doctrine. War now runs on
PEOPLE and GRAIN, not proxy constants:

- ARMIES: the levy (levy_fraction 10% of population). Campaign RATIONS are real grain
  drawn from the granary (food_store): soldier_ration_mult 2x civilian for
  campaign_days 120 (attacker) / defense_days 60 (defender); forage covers
  forage_share 50% off the land; the rest comes from the polity's granaries
  (deterministic member-by-member draw) — an unprovisioned army fights at forage
  strength. Rations EATEN are an explicit conserved sink (soldiers' mouths).
- BATTLES: Lanchester. P(attacker wins) = Sa^2/(Sa^2+Sb^2) (square law — the auto-win
  rail is dead; defenders can and do win, and a repelled attacker paid rations and
  blood for nothing). Casualties are REAL PEOPLE proportional to enemy effective
  strength (battle_lethality 0.1 x S_enemy), published as war_death_fraction — the
  field was RENAMED from the war_mortality multiplier to an additive annual death
  fraction so the units are honest end-to-end (population_aging adds it to cohort
  mortality; deaths capped at cohort size — the physical bound).
- SACK: the victor plunders the loser's granary, CARRY-LIMITED (carry_per_soldier
  100 units) and paying the ox law home (path delivered-fraction); what is sacked but
  not delivered is BURNED — an explicit destruction sink. Coin plunder unchanged.
- SUPPLY-EMERGENT REACH (replaces the cavalry 2-hop boolean): ALL polities may strike
  2-hop targets, but infantry supply pays the path's delivered-fraction twice
  (strength x path, rations / path) — so farmland armies arrive starving unless
  roads/rivers carry the supply (Rome strikes far), while steppe cavalry is herd-fed
  and pays nothing (Genghis). Reach EMERGES from the logistics law; nothing is
  forbidden, distant wars just fail the strength gate naturally.
- ROME/HOLD: integration needs tenure AND a route — cohesion accrues as
  years_held x best link delivered-fraction to a fellow polity member ("no road to
  Rome, no Rome"), saturating on the assimilation timescale. And the centre overawes
  with STRENGTH: a living great commander holds what raw numbers could not — his
  death is what lets the members go (the Alexander fragmentation now flows through
  the same hold ledger).
- NEW RegionDelta.food_store_delta (additive, floor-0 physical) carries all war grain
  flows; WarfareModule takes GrainLogisticsConfig (one logistics law).

Test suite rewritten (20 cases): square-law math; forage/fed factors; casualties
formula-exact in real units; granary conservation through named sinks (rations +
burn); Alexander arc (conquer -> overawe -> die -> Diadochi); Genghis
(farmland cannot strike 2-hop, steppe can); Rome (fresh secedes / 150yr+route holds /
150yr without a route never integrated); alliances/coalitions/backstab/pool/secession
re-proven under the new units.

Gates: 1,582 unit (266,479), 37 integration (13,211, incl. the orchestrator war
scenario), emergence (1 failed-as-expected). Spectrum sanity under the grounded
economy + G1 de-caps: EARTHLIKE Modern @ 12,793 (Thriving — anchor holds), fertile
deathworld 11,591, garden stagnates; the BARREN deathworld slipped Classical->Bronze
(real drift from de-capped hazards + war costs; for the planned recalibration).

---

## 2026-07-03 — G3/G4: emergent lords + real grain flows

G3 — LORDSHIP EMERGES FROM WEALTH (kills the lords-by-array-index rail): under
manorialism the tithe now flows to the WEALTHIEST resident heads (capital rank, ties
by id) — wealth buys the retinue and hall that collect the tithe, and the tithe
compounds the wealth. An aristocracy that entrenches and can be displaced (a plundered
dynasty falls; a richer upstart rises — war plunder now feeds directly into WHO rules).
Unit test: the rich mid-list head, not resident[0], collects the tithe.

G4 — THE CATCHMENT BECOMES A FLOW (kills the parallel-bookkeeping rail): stored grain
now DIFFUSES down the scarcity gradient along links (grain_trade_rate_per_year 0.1 of
the granary-fullness gap closes per year — kin networks, tribute, trade-in-kind),
emitted as additive food_store_delta; every haul pays the ox law and the transit loss
is eaten by the teams (explicit conserved sink). grain_logistics takes SubsistenceConfig
for the granary targets (one source of truth). The net_feedable/urban signals remain
the genesis VIEW; the diffusion is the real flow on the granaries. Unit test:
full-beside-empty granaries equalize, formula-exact, conserved through the named
sink; rivers deliver far more than land.

Spectrum under the complete grounding pass (G1+G2+G3+G4): GARDEN era 2 Developing;
EARTHLIKE Modern @ 12,785 (Thriving — anchor holds); BARREN DEATHWORLD era 4 Stalled —
RECOVERED from the era-2 slip G2 alone caused, because grain diffusion is real famine
insurance between provinces (the marginal world's rivers carry it back to Classical);
FERTILE DEATHWORLD Modern @ 11,587. The grounded flow IMPROVED the marginal world's
viability — exactly what a real redistribution mechanism should do, and nothing a rail
would ever have revealed.

Gates: 1,584 unit (266,478), 37 integration (13,211), emergence (1 failed-as-expected).
The grounding pass (G1-G4) is complete: no behavior-shaping caps; war runs on levies,
rations, Lanchester battles, carry-limited sacks; lords emerge from wealth; grain
moves as a real conserved flow. Remaining from the fake-rails audit: none of the six.
Next: the single recalibration (restore earthlike modern ~12,000 and re-anchor the
barren world under the grounded economy), then M7 entry.

---

## 2026-07-04 — RC: the single recalibration under the grounded economy

With the grounding pass complete (G1-G4: de-capped tech/hazards, the full war economy,
emergent lords, real grain flows), the deferred one-shot re-tune of the era pacing
dials (knowledge_to_advance — the ratified calibration surface; mechanisms untouched).

Method: one measured iteration. Ran the earthlike baseline, took each era's observed
within-era knowledge rate (stable because population is bounded by that era's food
ceiling), and set each threshold gap = rate x real-history duration target.

New thresholds: 3830 / 12740 / 19650 / 49250 / 253000 / 892000 / 2117000 (builtin +
eras.csv in sync). Result vs real history:

  Neolithic 6784/6700, Bronze 2127/2100, Iron 652/650, Classical 1040/1050,
  Medieval 969/950, Early-modern 305/300, Industrial 237/250 — every era within ~4% —
  and MODERN AT YEAR 12,114 (real ~12,000; 0.95% off).

Full spectrum, all anchors restored under the grounded economy:
  GARDEN era 2 Developing (abundance breeds stagnation);
  EARTHLIKE Modern @ 12,114 Thriving;
  BARREN DEATHWORLD era 4 STALLED at Classical (the original intended anchor —
    recovered by the G4 grain-diffusion famine insurance, then held by this re-tune);
  FERTILE DEATHWORLD Modern @ 10,976 Thriving (fastest, as designed).

Gates: 1,584 unit (266,478), 37 integration (13,211), emergence (1 failed-as-expected).

The medieval band now stands: M1-M5 shipped, M6a all seven hazards, M6c the full war &
diplomacy engine (through conquerors), the G1-G4 grounding pass, and this calibration —
all on a fully grounded, conserved, deterministic economy with historical timing.
Remaining: M7 entry materialization (workshops/manors/castles/polities from the cohort
aggregates; the M4 content goes live).

---

## 2026-07-09 — M7: entry materialization (the band's finale)

A fresh pre-modern, non-founding start ("start in 500 CE") now opens with the town
economy already standing — spun up at world gen from the SAME laws the climb runs on,
not conjured. `PremarketGenesis::materialize` (core/world_gen/premarket_genesis.cpp),
called from `WorldGenerator::generate` on an independently forked stream
(kPremarketGenesisRngSalt) when `starting_era`'s regime is premarket-manorial
(feudal/mercantile/industrial), `founding_seed_mode` is off, and the production
catalogs are loaded:

1. SURPLUS (subsistence law): per province, `natural_capital_of` (newly exposed
   static) → `subsistence_output` × the era's tech food_mult, minus need — the same
   Malthusian arithmetic the climb runs.
2. CATCHMENT (the ox-cart law, M2): each source allocates its surplus across
   {self + neighbours} weighted by `GrainLogisticsModule::delivered_fraction`
   (terrain/roads/gravity; the teams eat the difference) → net feedable → urban heads
   at `urban_per_capita_food`.
3. ENTITIES: `floor(urban / 8)` workshops per province (8 workers each — a real
   medieval shop headcount), rotating over the era-available crafts (lowest-era
   recipe per facility type, era_available ≤ starting era — no anachronisms). Each
   is FOUNDER-GATED (owner must hold `capital >= base_construction_cost`; candidates
   wealth-ranked capital desc/id asc — the same emergent ordering that makes G3
   lords) and FOUNDER-FUNDED: endowment = 0.5 × the founder's capital, a conserved
   transfer into `biz.cash`. The lord stratum (`lord_count`) holds manors — new
   `manor_farming` recipe (era 2, wheat 8 + wool 1, labor 30, no fertilizer input)
   on the existing manor_farm facility type. D3 resolved: a manor IS an NPCBusiness
   + Facility (reuse, as preferred).

EARNED, both ways: a subsistence-locked province (no catchment surplus) gets no
workshops; a bronze/barter start gets nothing at all (livelihoods, not firms); a
founding-seed world skips it (the climb carries aggregates — entities appear at
entry/LOD promotion, the deferred follow-up that will also read warfare's polity map).

Gate: premarket_genesis_integration_test.cpp — era-consistency (all facility recipes
era ≤ start), manors present under feudal, endowment conservation proven against a
control world (same seed, no catalogs → identical NPC layer; total wealth equal to
1e-5), bronze/founding starts materialize nothing, determinism (two generations
identical). Config: premarket_workers_per_workshop 8, premarket_endowment_fraction 0.5.

Link note: core->module shared laws (natural_capital_of, subsistence_output,
lord_count, delivered_fraction) moved to header-inline definitions -- targets that
never pull the module objects (scenario tests) otherwise fail to resolve them from
libeconlife_core.a's premarket_genesis.o (static-archive ordering). One law, one
definition, no module object code needed by core.

Gates: 1,584 unit (266,478), 41 integration (13,647 incl. the 4 new M7), full fast
gate 1,750/1,750 (unit+integration+scenario+determinism+benchmarks), emergence
green (122.8s). THE MEDIEVAL BAND IS COMPLETE: M1 earned surplus → M2 ox-cart logistics →
M3 feudal genesis → M4 era content → M5 guild/specialist layer → M6a hazards →
M6c war & diplomacy → G1-G4 grounding → RC calibration → M7 entry. Deferred (logged,
not blocking): medieval player verbs/UI, entry-from-climb at LOD promotion, castles
at polity seats, D4 guild records.

---

## 2026-07-26 — Deep review, then F1-F3 of the remediation

A max-effort review of the whole branch (10 independent finder angles over the
178-commit diff, every candidate put through an adversarial verifier) found 22
confirmed defects and refuted 3 candidate ones. The refutations are worth
recording: the food_store apply-order "mint" is not real (the orchestrator applies
each module's deltas immediately after that module runs, and both consumers declare
runs_after subsistence, so they read the post-banking store and bound every draw
against a live mirror); warfare's leader_mult default-insert is provably latent;
and the deleted facility_signals regulator feed was inert code, deliberately
retired.

The single worst finding was that ERA PROGRESSION WAS FROZEN for the default
modern game — which silently invalidates any long-horizon result the emergence
suite produced on this branch. Fixed in F1 (see the commit): the calendar gate now
reads the target era's start_year from the era catalog instead of a hardcoded table
that was never renumbered, the tech-condition bonuses are keyed by era string key,
base_year is signed so BCE starts stop wrapping to ~4.29e9, and tech/domain-knowledge
seeding follows the starting era instead of a hardcoded era 1. The two advancement
paths are now split BY THE DATA: an era with knowledge_to_advance > 0 belongs to
knowledge_module (the pre-modern climb, paced by what each world earns — this is
what keeps the World-Class spectrum alive), and only the modern band, which has
real historical dates, advances on the calendar.

F2 closed three conservation leaks. Production could mint matter (N facilities each
capped extraction against the same pre-tick deposit remainder; now a per-province
per-tick draw ledger) and destroy it (inputs were debited at full ratio while output
was scaled by power/staffing/deposit caps; the achievable throughput is now computed
BEFORE anything is consumed). The fishery was integrating a year of Schaefer biology
every day — ~90x MSY landings that never depleted the stock because growth was
inflated identically; rates are now explicitly annual, converted per tick, and
fishing_effort is retuned to a defensible 15%/yr exploitation rate.

F3 fixed the war-death timing cluster with a one-line ordering edge (warfare
runs_before population_aging), which makes publication and consumption same-tick
and thereby retires three separate defects at once: the year-late application, the
loss of a whole year's dead across a save, and the regime-exit reset wiping the
final war year. Battle dead are now debited to the provinces that RAISED the levy in
proportion to their contribution, each bounded by the soldiers it actually fielded —
a physical bound, which is why the fraction can no longer exceed 1.0 and be silently
clamped (an empire fighting through a small frontier province used to annihilate it
in one year while the surplus dead vanished). Secession stores the remaining
headcount instead of a leader-multiplied strength, so cascading fragmentation works.
Knowledge production now counts only living scholars (the era clock was being paced
by the cumulative death toll, since significant_npcs is append-only). Subsistence
publishes a one-time surplus reset on regime exit, so a famine year's ratio stops
scaling modern births forever. M7's workshop genesis now picks the cheapest
affordable craft per founder instead of halting permanently at the first
unaffordable one.

Two warfare unit tests pinned the old casualty formula and were updated with the
reason recorded: one asserted a death fraction of 0.125 for a province whose entire
levy was 400 of 4000 people — i.e. more dead soldiers than soldiers.

Deferred with tasks tracked: F4 (save/load — fisheries, national_legitimacy, the
political_cycle offices/unrest ratchet, the self-healing election trigger, unbounded
restore allocations, schema v25 with a raised floor), F5 (the remaining doctrine
rails: the hazard-mortality band and the addiction price band), F6 (regime-predicate
and catchment duplication, ticks_per_year triple source, dead code, hot-path scans),
F7 (re-anchor the spectrum — F1-F3 all move behavior, so the pacing thresholds and
the four World-Class anchors must be re-measured).

---

## 2026-07-27 — F7 blocked: the dawn climb is real but ~20x too slow (diagnosis)

F1-F6 shipped (see the commits). F7, the recalibration, is deliberately NOT done,
because retuning thresholds now would produce a curve that reaches modernity on
schedule while the mechanism underneath is not yet understood — the exact magic rail
the doctrine forbids. Recording the measurements so the next pass starts from data.

FIRST, a regression I introduced and fixed: the F3 dead-NPC filter on knowledge
production tested `status != NPCStatus::active`, which excludes NPCStatus::waiting
("alive and present, chose inaction this tick") — where most of a dawn population
sits. Knowledge production fell to the diffuse population term alone and ALL FOUR
spectrum worlds sat at era 1 "Developing" for 13,000 years. A stall with no cause in
the world: nobody starved, no granary emptied, no war was lost. All three gates
(1,601 unit / 41 integration / 10 emergence) passed throughout — nothing outside the
hidden [.society-*] observe runs pins the climb. Fixed (dead/fled/imprisoned excluded,
active+waiting counted) and now guarded by [liveness] unit tests, including one that
asserts production is strictly increasing in the size of the scholar corps.

THE MECHANISM IS ALIVE — measured via the new [.society-knowledge-who] diagnostic
(earthlike dawn, 200 NPCs, seed 7):

  year | surplus | occupied | knowledge-keepers | knowledge
     0 |  1.000  |        0 |  0 (output 0.0)   |    0.0
    50 |  2.184  |      200 |  6 (output 1.2)   |   11.6
   150 |  1.574  |      200 |  7 (output 1.4)   |   15.0
   300 |  1.196  |      200 |  8 (output 1.6)   |   22.5

and over the long horizon ([.society-knowledge-trace], 14,000 years): population
21,127 -> 126,616, surplus settling ~1.12, specialists 14%, urban 25%, knowledge
1,312. Every link is traceable: surplus frees specialists, the layer-2 rotation makes
some of them elders (knowledge-keepers lead the list), elders produce knowledge. Only
6-8 of 200 NPCs are knowledge-keepers because the surplus funds many crafts and few
scholars — a real result, not a cap.

WHAT BLOCKS CALIBRATION: the observed rate is ~9x BELOW what the module's own formula
predicts, and that gap is unexplained. At year 300: keeper output 1.6 x
production_scalar 0.4 = 0.64, plus population term ~1.5e-6 x ~45,000 = 0.068, times
pressure (adversity_base 0.35 + 0.6 x (world_hazard 1.0 - garden 0.45) = 0.68) and
knowledge_mult 1.0 (no era-1 node carries a knowledge multiplier — verified) gives
~0.48/yr. Measured: (22.5 - 19.8) / 50 = ~0.054/yr. Note 6 provinces x 1.5 = 9, so a
per-province vs global accounting factor is the first thing to check.

Until that is resolved the thresholds must not move: at the measured rate era 1 alone
(threshold 3,830) needs ~88,000 years against a real-history target of ~6,700, so the
apparent gap is ~13-20x — but tuning against a rate that is itself 9x off its own
formula would bake the discrepancy into the pacing dials permanently.

NEXT: (1) find the ~9x (start with whether knowledge production is being applied once
globally or diluted per province); (2) re-measure; (3) THEN recalibrate
knowledge_to_advance (builtin + eras.csv in sync, drift guard now covers the field);
(4) confirm the four spectrum worlds diverge for mechanical reasons — food ceiling,
mortality, logistics — and not because a dial was tuned to separate them.

---

## 2026-07-27 (later) — FOUND IT: the climb was powered by dead scholars

The ~9x gap in the previous entry is resolved, and the answer changes what F7 has to
be. Measured with the fast mechanism audit ([.society-knowledge-quick], earthlike
dawn, 200 NPCs, seed 7):

  year | living NPCs | keepers counted | keepers LIVING | module output/yr
     5 |         200 |               6 |              6 | 0.348
    10 |         197 |               6 |              5 | 0.294
    15 |         189 |               6 |              5 | 0.294
    20 |         172 |               6 |              4 | 0.240

The module's published production tracks the LIVING keeper count exactly (0.348 for 6,
0.294 for 5, 0.240 for 4 — each matching (keepers x 0.2 x 0.4 + 1.5e-6 x pop) x 0.68 to
four decimals). There is no wiring loss and no calibration mystery: the formula is
right, and the input is shrinking.

THE CAUSE: the tracked NPC layer dies off (~1.4%/yr: 200 -> 172 living in twenty
years) and is NEVER REPLACED, while the cohort population grows 21,127 -> 126,616.
Occupations persist on dead records because significant_npcs is append-only, so the
corps LOOKS intact (6 keepers at every sample) while the living corps drains to zero.
By year ~300 knowledge production is the diffuse population term alone (~0.05/yr), and
era 1's 3,830 threshold becomes unreachable.

WHY THE OLD BASELINE "WORKED": before the F3 liveness filter the module summed every
NPC with a knowledge occupation regardless of status. Those six elder records kept
producing for 12,000 years after the men died. THAT is what reached Modern @ year
12,114 in the RC entry — the climb was powered by corpses. The filter is correct (the
dead do not do research); it exposed a gap that was hidden behind it.

So the stall now has a real cause, but it is a MODELLING GAP, not a world outcome:
127,000 people exist and none of them can become a scholar, because the simulation
only ever tracks the original 200 individuals and has no promotion path from the
cohorts into that layer. Nothing starved and no limit was hit.

TWO GROUNDED FIXES (design call, not a bug fix):
(a) RECOMMENDED — scale knowledge to the cohort specialist stratum. subsistence already
    computes a real, conserved specialist share (~14% of population) and the cohorts are
    where the masses live by architecture. Knowledge then comes from the scholars among
    the 127k, with tracked NPCs as individual-level flavour. No new machinery, and the
    engine sits on a stock that grows with the society.
(b) Wire NPC promotion: on the death of a tracked individual, promote a replacement from
    the cohorts, holding the tracked layer proportional to population (the lod_system
    promotion concept). More general, but architectural — it touches every
    individual-level subsystem, and deserves its own milestone.

F7 REMAINS BLOCKED ON PURPOSE. Thresholds must not move until the knowledge engine runs
on a stock that grows; tuning the pacing dials against a corps that is dying by
construction would bake the defect into the calibration permanently — a curve that
reaches modernity on schedule and means nothing.

### Quantitative confirmation (300-year run)

The long-horizon audit closes it numerically. From year ~100 onward the published rate
equals the DIFFUSE POPULATION TERM ALONE (1.5e-6 x pop x pressure 0.68), while a naive
count of keeper records predicts 0.35-0.49:

  year |    pop | pop-term x pressure | actual/yr | naive predicted/yr
   100 | 32,546 |              0.0332 |    0.0325 |             0.3596
   200 | 46,802 |              0.0477 |    0.0442 |             0.4285
   300 | 55,318 |              0.0564 |    0.0549 |             0.4916

(The few-percent residual is because actual/yr is a 50-year backward average while the
population grew across the window.) The living scholar corps therefore contributes
EXACTLY ZERO from about year 100: every elder is dead, and the 6-8 "keepers" a
status-blind count still sees are corpses holding an occupation field.

Three independent confirmations of the same cause: the living-keeper column, the
in-place module probe (its output matches the LIVING count to four decimals), and this
long-horizon rate matching the population term alone.

---

## 2026-07-28 — the knowledge engine now runs on the population (design ratified)

Ratified design (owner's decision): "a percentage of the population should move a
society forwards, as long as there is food surplus, and/or pressure to advance. Some
named npcs can make leaps in knowledge and science, like Newton, Einstein and Socrates."
Implemented; see the two commits. Mechanism summary:

- subsistence now PUBLISHES the freed stratum (cohort_stats->specialist_fraction:
  population minus the farmers the harvest needs, ceilinged by regime). Real located
  people, single owner, replacing a signal that only existed as a local.
- knowledge draws its workers from that stratum (learned_share_of_specialists = 3%:
  elders/scribes/scholars; the rest are artisans, builders, traders). The corps
  therefore scales with the living population and CANNOT die out while the population
  lives — which is the defect it replaces.
- Per-worker output stays era-gated by the occupation catalog (elder 0.2 -> scribe 0.6
  -> scholar 1.0).
- Boserup path unchanged: with NO freed stratum, pressure alone still advances a
  society, slower. Surplus and/or pressure, as specified.
- Exceptional individuals: arrival probability 1 - exp(-rate) with rate proportional to
  the number of knowledge workers (great minds cluster where minds are many). A leap is
  ONE MIND's work — genius_equivalent_workers (250) keepers for genius_leap_years (30)
  at the era's per-worker output — so its absolute size does not depend on the size of
  the society. Credited to a LIVING named individual via a memory entry.

  (First cut made a leap worth 30 years of the society's TOTAL output; at ~15,000
  knowledge workers the arrival probability hit ~26%/yr and leaps supplied ~8x the
  ordinary rate. Newton did not do thirty years of all Europe's science. Bounding it cut
  late-era rates 5-6x: 402,843/yr -> 49,261/yr at era 7.)

MEASURED (earthlike, [.society-history]): every world now climbs to era 8, and the
spectrum separates for MECHANICAL reasons — barren deathworld needs 3,935 years to
leave the Neolithic against earthlike's 1,594, because thin land grows fewer people so
fewer can be spared from the land. Garden 1,647; fertile deathworld fastest.

REMAINING PACING GAP, and the honest diagnosis. Modern arrives ~1,900 vs the ~12,000
target, and per-era durations collapse (1,594 / 172 / 37 / 29 / 48 / 14 / 15 years
against real 6,700 / 2,100 / 650 / 1,050 / 950 / 300 / 250). The cause is NOT the
knowledge law: it is a POSITIVE FEEDBACK LOOP with too much gain —

  knowledge -> food ceiling (subsistence knowledge_productivity_max = 26x, halfsat
  35,000) -> population (21,127 -> 2,712,613 on earthlike) -> more freed people ->
  more knowledge workers -> knowledge

Population rises 128x and the knowledge rate 36,000x across the arc, while real history
accelerated ~27x. The loop is real (it IS the escape from the Malthusian trap) but its
gain is unbalanced against the era thresholds, which span only 550x.

NEXT (do NOT tune blind): the per-era rates above are exactly the inputs the RC method
needs (threshold gap = measured rate x real duration), but because population now grows
strongly WITHIN an era the fixed point is less stable than in RC, so expect 2-3
iterations, each a ~10-minute [.society-history] run. The other lever is the loop gain
itself (knowledge_productivity_max / halfsat), which is a MECHANISM constant and must
stay defensible in real units — foraging (~0.1 person/km2) to modern agriculture
(~100+/km2) is ~1000x over 12,000 years, so 26x through a saturating function is not
obviously wrong and should be argued from land productivity, not fitted to the curve.

### The capital gate is right, but capital PER HEAD cannot pace the climb

Setting the material thresholds from the measured curve (500/1000/1400/1900/2600/
3400/4100 per head, each just above what a society held at the transition it made on
knowledge alone) STALLED ALL FOUR WORLDS at era 1 for 13,000 years. Reverted to 0
(gate present and unit-tested, calibration withdrawn) — a gate no world can pass reads
as a failed civilisation when it is really an unreachable threshold, which is exactly
the kind of fake outcome the doctrine forbids.

WHY, and it is structural rather than a bad number. Capital per head PEAKS EARLY AND
FALLS: 645 at year 20 while surplus was 2.2, down to 321 by the era-2 transition at
year 1,647. Population growth pushes surplus back toward ~1.1, which cuts investment
(a share of surplus) while wear continues, so the stock settles at a low Malthusian
equilibrium. Capital per head is therefore FLAT across the arc — it grew only ~13x
(321 -> 4,320) while knowledge grew ~600,000x (3,835 -> 2,300,616).

That 4-orders-of-magnitude separation IS the design premise confirmed: knowledge is
information and spikes; built capacity is matter paid for out of a surplus the
population keeps eating. But it also means a per-head gate can slow the FIRST era and
then never bind again, because per-head capacity does not climb era on era — that is
the Malthusian trap, and escaping it (productivity outrunning population, the
demographic transition) is not modelled.

TWO PATHS, needs a design call:
  (a) Gate on TOTAL accumulated capital — civilisation-scale industrial capacity
      ("can this society build a railway network"). Total DOES grow strongly because
      population grows: 128M at era 2 -> 24.8B at era 8 on the garden world, ~193x.
      No new mechanism needed; thresholds set from that curve.
  (b) Keep per-head and model the escape: productivity growth outrunning population,
      so capital per head can actually rise across eras. Truer, and it is the real
      historical mechanism, but it is a demographic-transition milestone of its own.

The mechanism, the stock (cohort_stats->productive_capital, invested at 8% of surplus,
worn at 3%/yr), the delta plumbing, the era-catalog columns and the tests all stay in
place; only the shipped thresholds are zeroed pending that decision. The unit tests are
now self-contained (they write their own gated eras.csv) so they keep proving the gate
regardless of what the shipped data is calibrated to.

### Rise and fall: the law is in, but nothing in the world can trigger it yet

Design direction: civilisations rise, build grand works and perish, repeatedly, creeping
forward. Implemented (see commits): a society carries only what its learned stratum and
institutions sustain and forgets the excess (~2%/yr); writing is the ratchet (retention
scales elder 0.2 -> scribe 0.6 -> scholar 1.0), so a literate society keeps far more
through a collapse; an era is LOST when knowledge falls below what was needed to enter
it (hysteresis 0.75); apply_deltas no longer refuses backward era transitions. Four
unit tests pin it, including a dark age going negative and a recovered society
re-climbing the era it lost.

MEASURED, and this is a NEGATIVE RESULT worth recording: across all four spectrum
worlds, ZERO falls. Every world rises monotonically to era 8 (garden 1,964; earthlike
1,909; barren 4,473). The fall mechanism never fires.

WHY: nothing in the current model can durably break a growing society. Population rises
21,127 -> 5.7M without a single sustained reversal, surplus never stays below 1, so the
learned stratum only ever grows and sustainable knowledge always exceeds holdings. The
shocks that exist are survivable by construction:
  - famine is buffered by the granary and outrun by a food ceiling that only ever RISES
    (knowledge_productivity_max 26x);
  - epidemics are mortality blips against a population growing several %/yr;
  - war kills at levy scale (<=10% of a province) and destroys grain, NOT works;
  - secession fragments polities but touches neither knowledge nor capital.

The missing ingredient is that the CARRYING CEILING CANNOT FALL. Knowledge raises it;
nothing lowers it. Rome's soil exhaustion, Maya deforestation and drought, Easter
Island — the historical collapses are ceiling collapses, and a society that has grown
to fill its ceiling has no slack when the ceiling drops. Candidate mechanisms, all with
existing hooks in the world model:
  1. soil exhaustion under intensive farming (Province.soil_health already exists and is
     already read by production);
  2. deforestation as the forest is converted (geography.forest_coverage is already a
     natural-capital term in subsistence);
  3. capital destruction on conquest — warfare already has a carry-limited sack with an
     explicit burn sink, so extending it from grain to works is the same pattern;
  4. epidemic severity scaling harder with the urban density the climb now produces
     (urban hits 99% by era 4 — plague in dense cities is the obvious pressure).

Until at least one of those lands, "rise and fall" is a law with no trigger: correct in
the unit tests, invisible in history. Recorded rather than tuned — making the fall fire
by weakening the food ceiling or strengthening a shock ARBITRARILY would be the exact
magic rail the doctrine forbids; the ceiling has to be able to fall for REASONS.

### RISE AND FALL, OBSERVED (earthlike, the Boserup escape)

The land law plus knowledge retention plus technique-dependent sustainability finally
produce the behaviour the design has been aiming at. Earthlike trace, 500-year samples:

  year |    pop | knowledge | soil
   500 |  8,929 |        21 | 0.174
  1000 | 10,601 |        57 | 0.207
  1500 |  7,499 |         1 | 0.052   <- DARK AGE (knowledge 57 -> 1, 98% lost)
  2000 | 11,010 |        69 | 0.281   <- recovery
  2500 | 11,831 |       125 | 0.131
  3000 |  7,574 |         7 | 0.212   <- DARK AGE AGAIN (125 -> 7)
  3500 |  9,927 |        58 | 0.168   <- recovery
  ...
  5000 | 15,295 |       231 | 0.191
  8000 | 27,173 |     1,475 | 0.115
 11000 | 44,811 |     3,773 | 0.022
 11500 | 167,989|    10,590 | 0.009   <- ERA 2. Escaped.

Two full collapses, each losing >90% of accumulated technique, with population crashing
and rebuilding alongside — then sustained accumulation and escape into the Bronze Age.
Nothing about that sequence is scripted: it falls out of land degradation, the
retention law (a society forgets what its surviving learned stratum cannot carry), and
the Boserup escape (pressure -> innovation -> sustainable technique -> headroom)
interacting.

Compare the earlier states of this same world: with no soil model it marched to era 8
by year 1,909; with fixed-share soil it never left the Neolithic in 13,000 years and
oscillated at bare subsistence. The difference is that sustainability now IMPROVES with
technique, so the trap has an exit that must be earned.

Note the escape is late (era 2 at 11,500 vs ~6,700 real) and soil ends very degraded
(0.009) while population explodes on the era-2 food technology — the post-escape
dynamics need their own look. But the qualitative shape — repeated collapse, partial
retention, slow net progress — is now emergent rather than absent.

### The spectrum after the land laws — the Earth anchor lands at 11,938

All four worlds now escape the Neolithic and reach the modern era, with UNCHANGED era
thresholds (the RC calibration was never re-tuned for this):

  GARDEN (bounty 1.80)             modern @  7,337
  FERTILE DEATHWORLD (1.20)        modern @  8,891
  BARREN DEATHWORLD (0.40)         modern @ 10,482
  EARTHLIKE (1.00)                 modern @ 11,938   <- real Earth ~12,000 (0.5% off)

The anchor is essentially exact and was NOT fitted: it fell out of soil degradation,
knowledge retention and the Boserup escape interacting. Earthlike is the SLOWEST world
because it has no edge — moderate bounty, moderate hazard — while a garden has slack
against degradation and a deathworld's hazard drives the adversity term in the
knowledge engine. An unremarkable world taking the standard twelve millennia is a
defensible reading.

Note this REVERSES an earlier documented property: gardens used to stagnate ("abundance
breeds stagnation", era 2 Developing). With land that can be worn out, abundance is
slack against degradation, so gardens are now fastest. That is mechanically coherent
but it contradicts the older design note, which should be reconciled deliberately
rather than left as two conflicting claims in the docs.

REMAINING GAP — per-era distribution, not the total. Earthlike durations against real:
  Neolithic 11,069 (real 6,700), Bronze 530 (2,100), Iron 123 (650), Classical 85
  (1,050), Medieval 92 (950), Early-modern 19 (300), Industrial 20 (250).
The total is right and the shape is wrong: the dawn is too long and everything after it
collapses to decades. That IS the threshold calibration (F7) and it is now measurable
against a settled mechanism — which is what was missing every previous time it was
attempted.

Still no ERA-level falls: the collapses observed (knowledge 57 -> 1, 125 -> 7) happen
inside era 1, where there is no lower era to fall to. Era regression will only show once
a society collapses from era 2+, which needs shocks that bite AFTER the escape —
conquest destroying works being the obvious candidate.

### CIVILISATIONS NOW RISE AND FALL — twice, but without a ratchet

After the F7 threshold pass, era-level FALLS appear for the first time. Garden:

  RISE era 4 @ 4,605   pop 1,584,825  capital/head 378
  FALL era 3 @ 4,901   pop 2,218,924  knowledge -67/yr   spec 0%  capital/head 29
  FALL era 2 @ 4,936   (35 years later)  knowledge -770/yr
  FALL era 1 @ 5,130   pop   116,897                     capital/head 1
  RISE era 2 @ 7,494 -> era 3 @ 8,786 -> era 4 @ 9,014
  FALL era 3/2/1 @ 9,312 / 9,347 / 9,541      <- a SECOND full cycle
  RISE era 2 @ 11,884
  final: era 2 at 13,000 (OvershootCrash)

Two complete civilisational cycles: Classical height, total collapse to the Neolithic,
rebuild, Classical again, collapse again. Earthlike does the same (era 4 @ 8,520, back
to era 1 by 9,143). The crash signature is textbook OVERSHOOT: at the moment of
collapse population is at its PEAK and still rising (1.58M -> 2.22M) while knowledge is
already falling — the society kept growing into land it had ruined. Specialists go to
0%, cities empty, capital/head falls 378 -> 1.

THE DEFECT: it is a LIMIT CYCLE, not a ratchet. Every cycle rebuilds to the same height
and falls the same way; all four worlds end at era 2 and none reaches modern. Knowledge
returns to ~550 each time, below the 750 needed to leave the Neolithic, so each
civilisation starts from scratch. "Slowly creeping forward" requires each cycle to
retain more than the last.

WHY: retention is scoped to the CURRENT era's institutions (per_worker_output: elder 0.2
-> scribe 0.6 -> scholar 1.0). When a society falls back to era 1 it loses its scribes,
retention collapses to oral levels, and the records go with them. Historically this is
wrong in a specific way: WRITTEN RECORDS OUTLIVE THE INSTITUTIONS THAT MADE THEM.
Monasteries preserved classical texts through the European dark ages; clay tablets
outlasted Sumer. A society that once had writing does not forget as an oral one does.

NEXT MECHANISM (the ratchet): retention should follow the highest institution a society
has ever ACHIEVED, decaying slowly, rather than snapping back to the current era's
level. Records persist and can be recovered; that is what makes each cycle start higher
and turns a limit cycle into slow net progress.

Also note the F7 pass undershot as predicted (era 2 at 3,101 on garden vs a 6,700-year
Neolithic target) — but that is now entangled with the collapses, so thresholds should
not be iterated again until the ratchet exists, or the calibration will be fitting to a
transient.

### R1A landed: records slow the fall but cannot survive a 2,400-year dark age

Measured effect of codified knowledge: the fall from era 4 to era 3 now takes 496 years
(was 296) and forgetting runs at -39/yr (was -67/yr) — the corpus arrests roughly 40% of
the loss. But the floor is unchanged at ~554 and the peaks are identical (73,426 then
73,284), so the limit cycle survives.

The arithmetic explains it. Our dark ages last ~2,400 years; at the 1%/yr record loss
rate that is e^-24, i.e. total annihilation of any corpus. That loss rate is NOT wrong —
~90% of classical Latin literature was lost over the 400 years 500-900 CE, about
0.6%/yr — the problem is that our dark ages are six times longer than real ones.

They are that long because the COLLAPSE IS TOO DEEP: population falls 2.1M -> 120K, a
94% crash, and rebuilding from 120K takes millennia. The western Roman collapse lost
perhaps 50-75% with the countryside persisting, and the Carolingian revival came
300-400 years later — which is why monastic copying could carry the corpus across it.

So the ratchet is correctly built and blocked upstream. The fix is the next roadmap
items, which reduce collapse depth: 2C specialist inertia (the stratum currently
evaporates 17% -> 0% in ONE TICK, which is what makes the crash instant and total) and
2A/2B the wage valve and urban graveyard (population overshoots with no braking). Do not
compensate by making records more durable — that would be curing a symptom of collapse
depth with a knowledge dial.
