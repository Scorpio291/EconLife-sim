# EconLife — Realism Roadmap (research-backed)

**Status: ALL TIERS IMPLEMENTED, 2026-07-28.** Each item below carries its own LANDED
note with what was measured, what was wrong on the first attempt, and why. The original
research text is kept beneath each note so the reasoning that motivated it stays visible.

**Purpose:** identify the mechanisms needed to move the simulation from "civilisations
cycle at a fixed height forever" to something that reproduces real history, WITHOUT
adding caps, floors or guiderails. Every item below is a mechanism with state, a rule,
and a measured defect it addresses.

## Where it stands

An earthlike world now reaches every historical era within two years of its real date,
from mechanism alone, and the world spectrum falls out of the SAME era thresholds rather
than being tuned per world (garden reaches era 8 at year 12,042; fertile deathworld at
11,196; barren deathworld is still Neolithic after 13,000 years).

Two guards hold that: the thresholds must strictly increase, and the earthlike climb must
land within 1,500 years of every historical date (`[pacing]`, in `ctest -L emergence`).
Recalibrate with `tools/calibration/` after ANY change that moves the knowledge or food
trajectory — the procedure is sequential and bottom-up, and the reason is documented there.

### THE LARGEST REMAINING GAP: the economy never industrialises (measured 2026-07-30)

Reaching "era 8" on a knowledge threshold is not the same as having an industrial
economy. Measured (`[.society-industrialise]`), at era 8 — nominally the year 2000 —
an earthlike world still has **92% of its people farming and 0.8% in towns**. England had
22% in agriculture by 1851 and 9% by 1900, with urbanisation at 54% and 77%. The model
reaches the era label while staying Neolithic in shape, and this is a larger gap than the
collapse depth that occupied the preceding work.

Two upstream defects pin it, both now identified precisely by printing the HELD stratum
against the SUPPORTED one:

| era | held | supported | regime ceiling |
|---|---|---|---|
| 2 barter | 4.6% | **15.0%** | 0.15 |
| 3 coinage | 5.7% | **15.0%** | 0.18 |
| 4 money | 6.2% | **18.0%** | 0.22 |
| 5 feudal | 7.0% | **22.0%** | 0.27 |
| 6 mercantile | 7.9% | **23.3%** | 0.35 |

1. **The per-regime specialist ceiling is a RAIL and it binds.** `supported` sits exactly
   on `specialist_ceiling_{subsistence,coinage,money}` = 0.15/0.18/0.22 — a hard
   `std::clamp` capping the non-farming share by era regardless of what the food balance
   says. That is a behaviour-shaping cap on finite values, which the grounding doctrine
   forbids. The thing it stands in for is real (you cannot coordinate a large non-farming
   population without institutions and a way to move food to them), but it should be a
   COST expressed through grain logistics and urban capacity, not a wall. Removing it is
   a doctrine fix with large behavioural consequences and needs recalibration.
2. **The held stratum never converges on the supported one** — 7.9% against 23.3% after
   millennia. The asymmetric shed/growth inertia is still ratcheting downward against a
   noisy target, despite the granary-defence fix. Worth a focused look; tripling the
   non-farming share would move urbanisation with it.

Two improvements landed in the same pass, both correct and both currently INERT because
they sit downstream of the above:

- **R11 — machines replace hands.** Capital gated how much knowledge a place could USE
  (R9) but never did the other and more famous thing. An American farmer fed ~3 people in
  1800 and ~150 by 2000, almost none of it from working harder. Leverage now multiplies
  the labour a farmer supplies, so the same harvest needs fewer of them; output stays
  bounded by the land, so it frees hands rather than conjuring food. Inert at present
  because capital per head never gets high enough (48-58 at eras 7-8).
- **The investment horizon shortens under threat.** The fixed 33-year horizon left
  investment at 0.2% of intended at this model's peak stress, and three centuries of that
  against 3%/yr wear erased the capital stock (capital/head 622 -> 34 across the
  industrial crisis), which is not what happened to Rome or anywhere else. People under
  threat build what pays back before the danger arrives, so the horizon used is the
  shorter of the service life and the time a place expects to be left alone. The term now
  saturates at exp(-1) — about a third of intended investment — reached physically rather
  than by a floor.

A side effect worth keeping: with machine leverage in, a THINLY SETTLED but mechanised
province can strip its soil, which is exactly what industrial agriculture does. The soil
test now runs unmechanised so it exercises the soil law alone.

### What is still open

1. **Stress contagion LANDED 2026-07-28 (R5) — and the falls are still shallow, for a
   deeper reason that is now identified.** Three changes went in, all about scope rather
   than new forces:
   - **Stress is the POLITY's, not the province's.** All three legs are computed over
     polity aggregates and every member reads its state's index. **Peak PSI went from
     ~0.1 to 1.49** — a fifteenfold rise — and factional deaths from nothing to 0.74%/yr.
   - **Immiseration is measured against what people are USED TO** (`wage_reference`, a
     generation-scale average, persisted at v29) rather than against subsistence. This
     was the binding constraint: a population whose numbers track its food supply is never
     ABSOLUTELY starving, so the mobilisation term was identically zero for the entire
     12,000-year climb and the multiplicative index was zero with it. Turchin's variable
     is the wage against trend; Davies' J-curve says revolutions follow reversals after
     improvement, not steady poverty.
   - **Refugees carry a collapse across borders**, conserved, only ever toward better-fed
     reachable neighbours — so a province whose neighbours are all worse off starves in
     place, and one that flees makes its neighbours' land carry more mouths.

   **What it did not do: produce era regressions.** The stress is now large but it arrives
   at the very END of the climb (coal exhaustion plus a population that has caught up),
   where the run terminates at era 8. During the climb the surplus improves nearly
   monotonically, so the wage stays at or above its reference and the mobilisation term
   stays near zero.

   **R6 LANDED 2026-07-28 — knowledge is now per province.** See below; this paragraph
   records the diagnosis that led there.

   **The reason is now clear, and it is not about stress at all: KNOWLEDGE IS GLOBAL.**
   `technology.knowledge_level` is a single number for the whole world, so there is no
   such thing as one civilisation falling while another rises — there is one civilisation
   with six provinces. Every fall the record actually contains (the Bronze Age collapse,
   Rome, the Maya, the Khmer) is a REGIONAL civilisation overshooting while others do not.
   Making knowledge per-polity or per-culture-area, with diffusion between neighbours, is
   the next real piece of work; the ratchet (R1A/R3E/R3F) and the political map (R3F) are
   already in place to support it.
   **R6 FOLLOW-UP, measured 2026-07-30: absorptive capacity, and what it did and did not
   buy.** The era trace could not answer whether REGIONS fall, because the era is the
   frontier — a maximum — so it only moves when the LEADING province does. Measuring the
   spread across provinces directly (`[.society-regional-falls]`) gave the answer:

   - Provinces genuinely diverge: the laggard holds 68-93% of the frontier throughout, so
     the world is no longer one civilisation with six provinces.
   - But the deepest regional drawdown was **8.6%** — nothing ever went backwards. The
     cause was that diffusion was UNCONDITIONAL: a collapsing province was topped straight
     back up by its neighbours.
   - Diffusion now requires a receiver. `absorptive_capacity` gates it on the receiving
     province's learned stratum, because a dark age is not a shortage of knowledge in the
     world but a shortage of anyone able to receive it — Greek mathematics sat intact in
     Byzantium and Baghdad the whole time western Europe could not read it. That took the
     deepest drawdown to **39.7%**: a real regional dark age.

   **And it still does not produce regional falls DURING THE CLIMB.** The 39.7% is the
   dawn transient; measured separately, the climb shows a deepest drawdown of 8.5% in ZERO
   episodes past a tenth. The absorption gate is right and it makes a fall STICK once one
   happens — it does not cause one.

   Nothing causes one, and the reason is now visible in the couplings rather than in any
   single mechanism. The stabilising ones are strong and all correct individually: grain
   diffuses to whoever is short (R3D), refugees leave for wherever is better (R5),
   knowledge diffuses from whoever knows more (R6). Together they mean no province's
   conditions can diverge far enough from its neighbours' to scatter its stratum while the
   world as a whole is rising. The destabilising mechanisms all exist but are calibrated
   mild, and PSI's multiplicative form correctly refuses to fire unless all three legs go
   at once.

   So the open question is no longer "which mechanism is missing" but **"is any local
   catastrophe in this model big enough to break a region while its neighbours prosper?"**
   — a lost war, a plague wave, a soil collapse. That is a calibration question about the
   existing shocks, and it should be answered with measurement rather than another
   mechanism.

   **R7 + THE MEASUREMENT THAT FOUND THE REAL ANSWER (2026-07-30).** Rather than add
   another mechanism, I measured how bad it ever gets for the WORST-HIT single province
   over a whole climb (`[.society-worst-province]`). World means hide exactly this: a mean
   surplus of 1.5 says nothing about the province sitting at 0.4.

   Earthlike, after the founding transient: worst provincial surplus 0.498, worst soil
   0.369, smallest stratum 0.59%, worst annual war death fraction 0.100, peak PSI 1.586.
   So the shocks ARE large. But only **22 years of war in 12,000**, and they all cluster
   before year 1,500.

   **The polity count explains everything.** The world starts with 5 polities, holds for
   1,250 years, unifies permanently around year 1,500, and NEVER FRAGMENTS AGAIN in the
   remaining 11,250 years. With one polity there is nobody to fight, no frontier for
   asabiya to be forged at, one shelter for the corpus, and a single pooled treasury
   carrying every province — so no shock can break one region while its neighbours
   prosper. It is the precise counterfactual to real history: Rome's third-century crisis
   nearly fragmented the empire and the Gallic and Palmyrene empires did break away, the
   Han fell apart, the Caliphate fell apart, and Europe never unified at all.

   **R7 supplies the missing coupling:** the centre's ability to overawe a member is
   divided by `1 + weight * PSI`. Empires come apart from the inside, and the inside is
   what the index measures. It is correct and cheap — and currently INERT, because PSI is
   zero for the whole climb.

   **So the chain bottoms out one step earlier than any of this.** No immiseration -> no
   stress -> no fragmentation -> no war -> no shocks -> no regional falls. And there is no
   immiseration because **the carrying ceiling rises smoothly and monotonically for the
   entire climb**, so the wage never falls. Knowledge accumulates every year and diffusion
   smooths it across provinces, so the ceiling never plateaus — while Turchin's secular
   cycles require a roughly CONSTANT carrying capacity within a cycle, with population
   oscillating around it. Ours has a capacity that outruns population indefinitely.

   Tried and reverted: making fertility answer to expectations rather than to this year's
   harvest, so births lag conditions and overshoot. Demographically sound, equilibrium-
   neutral, and it bought almost nothing (peak PSI 1.59 -> 1.85, no behavioural change)
   while introducing a fragile cross-module dependency. Recorded in the code at the site.

   **RESOLVED 2026-07-30 by R9 + R10 — and the falls now happen.** The prompt was: why
   not both, and look at Europe vs Africa vs the Americas, or British industrialisation
   against Russia and China. That is the right frame, and it named an axis the model did
   not have.

   **R9 — knowing is not having.** The ceiling rose with knowledge ALONE, so the model
   could not express the most conspicuous fact about 1800: Qing China and Britain did not
   differ much in what they knew. China had the books, the embassies and the engineers;
   Russia sent students to Britain for decades. What differed was the capital stock to
   deploy any of it. The ceiling now rises with knowledge TIMES how much a place has built
   the means to use — a society that knows everything and has built nothing farms like the
   dawn. It also gives the ceiling the one thing it never had: a way DOWN that needs
   nobody to forget, because capital wears out at ~3%/yr and is rebuilt only out of a real
   surplus. Hungry years went from 199 to 914 immediately.

   **R10 — imperial overstretch.** Chasing why the world still would not fragment turned
   up an arithmetic impossibility: the hold problem assumed the centre could bring its
   WHOLE army against one rebellious province, so a member holding 1/N had to out-muscle
   0.8 x cohesion x (N-1)/N. For N >= 3 that is unreachable at ANY cohesion — not
   unlikely, impossible. An empire has to garrison everywhere at once, so the force
   available against one member is what is left after holding the rest. (Force projection
   at the grain-haulage rate was tried first and broke the Alexander arc: soldiers and
   orders travel where wagons of grain cannot.)

   **Measured together, earthlike now rises AND falls.** Dates hold — Iron, Classical and
   Medieval land exactly on their historical years, the rest within a year — and through
   the industrial transition:

   | | polities | population | capital/head | urban | gini |
   |---|---|---|---|---|---|
   | era 6 @ 11,451 | 1 (empire) | 2,248,187 | 622 | 8% | 0.42 |
   | era 7 @ 11,751 | — | **1,255,846** | **34** | **0%** | 0.50 |
   | era 8 @ 12,001 | 6 (shattered) | 3,325,826 | 46 | 1% | 0.57 |

   The population halves, the capital stock is wiped out, the towns empty, inequality
   jumps, the empire fragments from one polity into six, PSI peaks at 3.68 (was 1.27) and
   war returns (58 war years, up from 22) — and then it recovers to era 8.

   **AND IT HAPPENS MORE THAN ONCE** (`[.society-cycles]`, which counts every episode
   where a world loses a fifth or more of its population from a running peak). Each world
   shows a founding correction — the world-gen population settling onto what the land can
   actually feed, over the first few thousand years — and then TWO genuine civilisational
   collapses in the late climb:

   | earthlike | population | knowledge across it |
   |---|---|---|
   | fall, yr 10,774 -> 11,810 (1,036 yrs) | 3,096,854 -> 1,250,568 (**-60%**) | 106,111 -> **468,354** |
   | fall, yr 11,901 -> | 8,342,400 -> 3,325,826 (**-60%**) | 583,492 -> **695,528** |

   Garden: -80% and -83%, recovering in 591 and 567 years. Fertile deathworld: -40% and
   -71%. Depths of 40-83% are the right register (the Black Death took ~50%, the Maya
   lowlands ~90%) and so are the recovery times.

   **Knowledge RISES through every one of them** — 4.4x across a 60% population collapse
   in the earthlike case. That is the ratchet and the cycle running together, which is
   exactly what the brief asked for: rise and fail, again and again, slowly creeping
   forward. The society falls; the civilisation keeps what it learned and goes on
   learning.

2. **Perpetual legal persons** (Tier 4) — the corporation, the monastery, the guild
   endowment, so capital and archives survive a named owner's death. Needs an ownership
   model the commons arc does not have. See the Tier 4 note.
3. **Coal endowment is small relative to the population scale** — ~310 tonnes a head
   cumulatively against Britain's ~900, and only one province in six has any. The
   mechanism is right; world-gen's seeding is what limits it.

---

## The measured starting point

After the land laws (soil degradation + Boserup escape) and knowledge retention:

- Civilisations rise to a Classical level, collapse to subsistence, and rebuild —
  **twice** within 13,000 years, at the SAME height each time.
- At collapse, population is at its PEAK and still rising (overshoot).
- Specialist and urban fractions drop from 14-17% / 32% to **exactly 0% in one tick**.
- Knowledge returns to ~550 each cycle, below the 750 needed to leave the Neolithic —
  so every civilisation starts from scratch. **A limit cycle with no ratchet.**
- Nobody reaches an industrial or modern era.
- Population growth ~0.18%/yr sustained.

## What the research says we already got RIGHT

- **Overshoot is correct behaviour, not a bug.** In the Turchin-Korotayev demographic-
  fiscal model, instability `W` lags population `N` quadratically, so population is
  still rising when collapse begins. Our defect is the missing ratchet, not the
  overshoot. (arXiv 1504.04688)
- **Soil-stock degradation with a technique escape** is the right shape: overshoot
  requires a resource that degrades faster than the population notices (St Matthew
  Island reindeer: 29 -> 6,000 -> 99% dead in two winters once the slow-regrowing
  lichen mat was destroyed).
- **Famine being a weak brake matches history.** Famine's demographic bite is mostly
  lost births plus emigration, with fast rebound; crisis mortality was <5% of all
  deaths in England before 1800. Our famine gate not braking anything is CORRECT —
  do not "fix" it by strengthening famine.

## What we got WRONG

- **Urbanisation is too HIGH, not too low.** We peak at 32%; Rome peaked ~11% and
  Europe was still only **8.5% urban in 1800**. Pre-modern cities were mortality sinks.
- **Population grows ~5x too fast.** Real long-run pre-industrial growth was ~0.04%/yr
  (0.02%/yr in the first millennium CE) against our ~0.18%/yr.
- **Knowledge is modelled as a stock tied to the living stratum.** Historically it is
  ARTIFACTS that outlive the stratum. ~90% of classical Latin literature was lost
  500-900 CE; what survived did so by existing in many copies in many houses.

---

## TIER 1 — the minimum set to break the limit cycle

The research is unusually crisp here: a knowledge FLOOR collapse cannot breach, plus a
ceiling that can RISE. Without both you get either an eternal Classical plateau or
eternal oscillation — which is exactly what we measured.

### 1A. Codified knowledge as institution-held copies (the ratchet)
**Why our cycle has no ratchet:** retention is scoped to the CURRENT era's institutions,
so a society falling to era 1 loses its scribes and forgets like an oral culture. But
monasteries preserved classical texts through the dark ages, clay tablets outlasted
Sumer, and the Graeco-Arabic translation movement re-imported an entire dead Greek
corpus into a living culture.

- **Split** `K_tacit` (decays with the learned stratum — the current model, keep it)
  from `K_codified`, held by INSTITUTION objects with their own survival roll
  independent of polity survival (monastery, academy, exam bureaucracy, canon).
- Works don't decay smoothly. Each COPY is an artifact with a loss hazard; a work
  survives iff >=1 copy does: `dN/dt = (lambda - mu)N`, extinction probability from
  N0 copies = `(mu/lambda)^N0`.
- Substrate matters: `mu` ~0.1%/yr (clay) to ~3%/yr (papyrus), x10-50 during a sack.
- Successor polities INHERIT `K_codified` cheaply — that is the trans-dynastic carrier
  that made China's collapses recoverable (bureaucracy, exams, Confucian canon
  re-adopted by every successor including conquerors) and Rome's partially so (Latin,
  a transnational Church).
- **Hooks:** knowledge_module retention; warfare's sack (raise `mu` on conquest — the
  burn sink already exists); grain_logistics routes for copy diffusion.
- **Fixes:** no ratchet. Each cycle starts higher than the last.

### 1B. Energy stock as "ghost acres" (the ceiling that rises)
**Tainter's equation is our limit cycle in one line:** `B(C) = B_max(1 - exp(-gamma*C))`
against linear upkeep `kappa*C`. Without an energy-base shift `B_max` is FIXED, so the
peak necessarily recurs at the same height.

- An organic economy is bounded by photosynthesis on finite acres; coal substitutes a
  STOCK for that flow. England & Wales sustained-yield acre-equivalents: 4.3M (1750),
  11.2M (1800), **48.1M (1850)** — exceeding the ~37M acres of actual land surface.
- `effective_land = arable + coal_energy / yield_per_acre_of_firewood`.
- Calibration: pre-industrial ~18 GJ/cap/yr, industrial ~27 GJ/cap/yr.
- **Hooks:** the deposit system already models finite located resources with extraction
  (coal deposits exist); subsistence's carrying ceiling consumes natural capital.
- **Fixes:** no escape to sustained growth; fixed peak height.

### 1C. Induced innovation — relative prices select the technique
Britain adopted labour-saving, coal-burning machines because its wage/energy price
ratio made them pay; identical knowledge sat unused where labour was cheap and fuel
dear. This supplies the TRIGGER that turns knowledge into an era transition.

- `P(adopt coal technique)` gated on `wage / energy_price > threshold_recipe`;
  invention effort allocates toward the input with the highest cost share.
- **Hooks:** recipes already carry labour and energy intensity; price_engine has wages.
- **Fixes:** why the same knowledge industrialises one province and not another.

**1B AND 1C LANDED 2026-07-28** as a new `energy_base` module (interface spec:
docs/interfaces/energy_base/INTERFACE.md). Coal is now a real, conserved, located,
finite escape: the best seam in a province is worked, every tonne burned comes out of
that named deposit as a `DepositDelta`, and the ghost acres it buys enter the carrying
ceiling at the same weight as arable land — because that is literally what they
substitute for. Adoption is Allen's ratio (the real wage against what the seam costs to
work), so knowing how to burn coal is not enough; it has to pay.

Three constants were wrong on the first pass and each was corrected against the primary
series rather than nudged:
- **acres per tonne 2.0 → 0.8.** Read off Wrigley's own acre-equivalents against English
  output (4.3M acres for ~5M tonnes in 1750, 48.1M for ~60M in 1850), not from the
  half-remembered coppice-yield rule, which is 2.5x too generous and does not reproduce
  his numbers.
- **tonnes per head 0.8 → 3.3.** 0.8 is the PRE-industrial figure for total energy from
  all sources; it describes the economy coal replaces, not the one it builds, and it
  held the ghost acres under 0.4 of the land base where the record has them passing 1.0.
  England 1850: ~60M tonnes for ~18M people.
- **tonnes per deposit unit 1e6 → 6.7e6.** A seeded deposit is a COALFIELD; the one that
  actually carried an industrial revolution (Yorkshire/Notts) held ~2e10 recoverable
  tonnes, and world-gen's richest seeding is 3000 units.

Measured (earthlike, seed 7): the coal age opens around year 8,000 and the seams are
empty by ~9,200 — a ~1,200-year industrial phase, with the ghost index peaking near 1.0
in the province that actually has coal, matching England 1850.

**It did not break the stall, and the reason is worth recording.** This earthlike world's
entire coal endowment is 835 deposit units (~5.6e9 tonnes) against a peak population of
18M — about 310 tonnes a head cumulatively, against Britain's ~900 — and only one
province in six has any. So the WORLD ceiling rises by a few percent even while the coal
province's own doubles. The stall at era 6 is therefore not an energy problem: knowledge
production is proportional to population, population to the food ceiling, and the era-7
threshold sits just above where that lands. That is F7 (threshold recalibration), which
was blocked on 1A/1B and is now unblocked.

What is still missing is the FALL. Every world now only ever rises. The endogenous
social trigger for collapse is 2D (elite overproduction / PSI), and it is next.

---

## TIER 2 — make the cycle itself realistic

### 2A. Malthusian wage valve (the missing population brake)
Fixed land means more people cut the marginal product of labour; real wages fall, which
depresses fertility (later marriage) and raises mortality. England shows NO real-wage
trend 1200-1800 despite tripling population.

- `birth_rate = b_max * w^eps_b` (eps_b ~ +0.3..0.5),
  `death_rate = d_min * w^-eps_d` (eps_d ~ 0.5..1), where `w = consumption/subsistence`.
- This is an EQUILIBRIUM, not a cap — the growth rate falls out of where they cross.
- **Fixes:** population 5x too fast; no sustained reversals.

### 2B. Urban graveyard effect
Pre-modern cities had deaths exceeding births and grew ONLY by in-migration. London
1735: 23,707 burials vs 16,691 baptisms; roughly half of England's entire natural
increase was consumed by London mortality.

- `urban_death_rate = rural_death_rate * (1 + gamma * density^alpha)`, with urban
  births < urban deaths until a sanitation/germ-theory technology flips it.
- Urban share also bounded endogenously by marketable surplus and transport:
  `max_urban_share = (yield_per_farmer - subsistence - seed - transport_loss)/yield`.
  At pre-modern yields this emerges at 10-20% with NO clamp.
- **Hooks:** population_aging cohort mortality; grain_logistics already models the
  ox-cart radius (a cart eats its load in ~300-500 km).
- **Fixes:** 32% urbanisation (should be ~10%); adds a self-limiting mortality sink.

**LANDED 2026-07-28.** Both halves are in, plus two things the measurement forced.

The 32% figure turned out to be a *capacity signal*, not a headcount: grain_logistics
published "the town the catchment could feed" straight into `urban_population`, and
nothing in the world was actually located there. That is now split — `urban_capacity`
is the signal, `urban_population` is the sum of the urban cohorts, and people move
between the two by a conserved flow. Two rails died on the way:

- Births were split **half urban, half rural** regardless of where anyone lived, which
  drove every society toward a 50% urban composition no matter what its land could
  feed. Children are now born where their parents are.
- **Townsfolk still farmed.** Migration had no cost in the harvest, so with the
  graveyard in and nothing to stop it, urbanisation ran away to 95%+ on a world with
  nobody left in the fields. The non-farmers are now the union of the institutional
  stratum and the town, and the harvest is what is left over.

The town's size is now bounded by two independent physical limits — what the harvest
can SPARE (the non-farming stratum) and what haulage can FEED in one place — and it
settles at **3-5%** across the whole spectrum, emergent, with no clamp. A society that
falls into food deficit de-urbanises on its own, because a catchment with no surplus
publishes no capacity.

**The soil trap, found on the way.** With the population brake in, earthlike stalled at
era 1 for 14,000 years with soil health pinned at 0.10-0.20. The cause was a real
modelling error, not calibration: the sustainable yield was measured against *current*
natural capital, which already includes soil health, so soil cancelled out of the
pressure ratio and the land had **no restoring force at all**. Renewal is absolute, not
proportional — weathering, rainfall and nitrogen fixation are properties of the place,
not of how depleted the topsoil is. Measured against pristine capacity instead, worn
land out-renews what a shrunken population takes from it and recovers.

Measured after both (earthlike, seed 7, 14,000 years): soil recovers 1.00 -> 0.90,
population 21k -> 18M, urbanisation 3-5% throughout, surplus 1.15-1.35 through the
agrarian phase. The spectrum came back with it: garden reaches era 8 at year 8,852,
earthlike and fertile-deathworld reach era 6, barren deathworld reaches era 2.

**What is still wrong, and why it is 1B/1C's problem.** Era pacing is now front-loaded
backwards: era 2 takes 5,500 years (right) but eras 3-8 take ~3,000 between them, the
last few at 120-160 years each. And every world now only ever RISES — the falls are
gone. Earthlike parks at era 6 with 18M people at a surplus of 0.755 and knowledge
slowly decaying: medicine cut mortality, the population ate the gain, and the
food ceiling saturated. That is Tainter's fixed `B_max` again, on the food side, and it
is exactly what 1B (energy ghost acres) and 1C (induced innovation) exist to fix. Do
not chase it with dials.

### 2C. Specialist stratum as a stock with inertia
Currently `specialists = population - farmers_needed`, recomputed every tick with no
memory, so the entire scholar-and-city class evaporates in ONE TICK when food tightens
(measured: 17% -> 0%). Real societies hold their non-farmers through lean decades via
stores, patronage, tribute and simple inertia — and that persistence is WHY overshoot
deepens instead of self-correcting.

- Make it a stock adjusting toward the food-supported level over years/decades.
- **Fixes:** instantaneous total collapse; supplies the social half of the overshoot
  lag; is the precondition for elite overproduction (2D).

### 2D. Elite overproduction / Political Stress Index
Gives collapse an ENDOGENOUS SOCIAL trigger independent of food. Population growth
depresses wages and inflates elite incomes, so elite numbers grow faster than the
positions supporting them; surplus elites form rival factions and the fiscal base
erodes.

- `PSI = MMP x EMP x SFD`, where `MMP = w_rel^-1 * N_urban * N_20-29`,
  `EMP = elite_count / elite_income_share`, `SFD = state_debt * (1 - trust)`.
  Multiplicative — all three must be elevated. Secular cycles run 200-300 years.
- **Hooks:** we already compute gini, urban share, cohort age structure, institutional
  trust, and government budget.

**LANDED 2026-07-28** as the `structural_demography` module (interface spec:
docs/interfaces/structural_demography/INTERFACE.md). Every ingredient was already in the
model and nothing joined them; what was missing was the observation that the GAP between
the stratum a society HOLDS (which has generational inertia, R2C) and the one its harvest
SUPPORTS is itself a destabilising force. Subsistence now publishes both.

    mmp = max(0, 1 - w) * youth_share
    emp = max(0, held - supported) / max(sentinel, supported)
    sfd = (1 - granary_cover) * (1 - institutional_trust)
    PSI = mmp * emp * sfd
    p_faction = 1 - exp(-PSI * conflict_death_rate_at_unit_stress)

Nothing here is a mood. The stress KILLS (a third independent competing risk in the
cohort mortality composition, kept separate from war because a society can be doing
both), EATS (retinue rations drawn from the granary, counting only the extra over the
ordinary ration, so nothing is consumed twice) and ERODES STABILITY, which feeds back
into births and deaths.

---

## TIER 3 — texture and de-synchronisation

- **Recurrent plague waves — LANDED 2026-07-28 (R3B).** `plague_susceptible_fraction` is
  now a real persisted stock (schema v27). A wave reaches `attack_rate x susceptible` and
  kills in proportion; survivors carry resistance so the stock falls by what was reached;
  turnover (~1/30 a year at a pre-modern life expectancy of ~35) refills it. **Neither the
  recurrence interval nor the declining lethality is written anywhere** — both fall out of
  that one stock. Measured: shifts the earthlike climb by ~40 years, well inside the
  pacing gate, so the historical dates survive. The wage-response lag is not modelled.
- **Recurrent plague waves (original note).** England fell 4.8M (1348) -> 2.6M (1351) -> nadir 1.9M in
  1450: recovery took 150-200 years BECAUSE plague recurred. Model waves at 10-20 year
  intervals with declining lethality, not one blip. Wage response lags ~25 years.
- **Asabiya — LANDED 2026-07-28 (R3C).** `cohort_stats.asabiya` is a real persisted stock
  (schema v28), updated annually by warfare after that year's conquests and secessions:
  `dA/dt = growth * A(1-A) * frontier - decay * A * (1-frontier)`, where `frontier` is the
  share of a province's neighbours under another polity. Logistic up (which is why a
  people with no solidarity can never develop any, and why the stock is seeded above
  zero), exponential down. Both directions are century-scale.

  It multiplies **STRENGTH, never the headcount** — and getting that wrong first was
  instructive. Folding it into `levy` made a beaten polity lose 12.5% of its population
  when only 10% had been mustered: more dead than there were soldiers. `levy` is also what
  casualties are apportioned over and what a seceding member takes with it, so warfare now
  carries a parallel `polity_strength` and the two quantities stay distinct. Soldiers eat
  as bodies and fight as a people.

  Flows into everything warfare already does: who dares attack, who wins, what holds and
  what secedes. A cohesive march can break off an empire that could hold a softer province
  of the same size.
- **Asabiya (original note)** generated at contested frontiers, decaying in
  the interior: `dA/dt = +h*A(1-A)*frontier - dec*A*(1-frontier)`, polity power ∝ A*N.
  Adds a second oscillator so collapses DE-SYNCHRONISE across provinces instead of one
  global sawtooth. Hooks directly onto warfare's existing cohesion model.
- **Trade-network fragility — LANDED 2026-07-28 (R3D).** A province's food balance is now
  its own harvest PLUS what actually arrives: `effective_output = output +
  grain_import_rate`, signed, so an exporter loses exactly what it sent and an importer
  gains exactly what came. `import_dependence` is published alongside it.

  The flow used is the REAL conserved diffusion grain_logistics already performs (stored
  grain down the scarcity gradient, draft teams eating the difference), NOT the catchment
  capacity signal — which is a view of the same surplus and would have counted it twice.
  A first attempt using the capacity signal did exactly that, and also broke on tick 0
  (before grain_logistics has run) by reporting every province in deficit. The signed
  real flow has neither problem and degrades correctly to zero for an isolated province
  or a world without haulage.

  The cascade is not modelled directly; it is what happens when a province whose
  neighbours fed it loses the neighbours. Measured: a dependent province's surplus falls
  by exactly its import dependence when the route fails.
- **Trade-network fragility (original note).** Sea transport decoupled cities from their hinterland
  (Egypt shipped ~130kt of grain a year to Rome; moving grain 70 miles by road cost more
  than sailing it 1,400). Carrying capacity = own land + imports; when a route fails,
  famine scales with `import_dependence`. The Late Bronze Age collapse was a small-world
  network cascade — severing Ugarit cut Cyprus's tin and copper.
- **Printing — LANDED 2026-07-28 (R3E).** `printing_copy_mult` multiplies the rate at
  which the learned stratum commits knowledge to records, saturating in accumulated
  knowledge toward a hundredfold gain. Gated on what a society KNOWS rather than on a
  calendar year or an era number, so a world that develops faster or slower than Earth
  still gets the press at the right point in its own development; and saturating rather
  than switching, because presses spread — there is no year in which printing turns on.

  This is what ends the possibility of a dark age. Roughly 90% of classical Latin
  literature was lost between 500 and 900 CE because every copy was a hand-made object in
  a named building that could burn; a work in ten thousand houses cannot be lost by
  anything short of the end of the civilisation. The corpus is the floor under forgetting
  (R1A), and printing is what puts that floor out of reach.
- **Printing (original note).** Europe held <30,000 books before 1450 and 8-12 million by 1500; print
  cities grew ~35pp faster 1500-1600. Multiplies `K_codified` copy count by two orders
  of magnitude — this is what makes the ratchet permanent.
- **Polycentrism — LANDED 2026-07-28 (R3F). TIER 3 COMPLETE.** Warfare's emergent
  political map is now exposed as `cohort_stats.polity_id`, and the written corpus decays
  more slowly the more INDEPENDENT jurisdictions hold it:
  `loss / (1 + weight * (shelters - 1))`.

  An idea suppressed or burned in one polity survives in the next — Tyndale printed in
  Antwerp, Galileo circulated in the Netherlands, Descartes published in Amsterdam — and
  the reason Europe's scientific revolution could not be stopped is that nobody was in a
  position to stop it everywhere at once. A unified empire offers no such refuge.

  So conquest now has a real cost, and it lands where it hurts most: an empire that
  absorbs its neighbours gains their levies and loses their refuges, and the loss falls on
  the one stock that lets a civilisation start its next cycle above the last. China's
  unified cycling against Europe's escape is emergent from that, not a dial.
- **Polycentrism (original note).** Innovation survives if ANY polity in a connected culture-area
  shelters it, so conquest/absorption acquires a growth COST and China's unified cycling
  vs Europe's escape becomes emergent rather than a dial.

---

## TIER 4 — the modern transition

- **Quantity-quality fertility transition — LANDED 2026-07-28 (R4A).** Families target
  SURVIVING children: `desired_births = target_surviving / P(survive to 15)`, neutral at
  the pre-modern norm and falling ~44% as survival goes 0.5 -> 0.9. Bounded by what a
  population can physically bear (~55 births per 1000 is the highest ever recorded),
  approached via `1 - exp` rather than clamped — without that a deathworld drove the
  factor to 15x and a soft people bred its way out of being culled.

  **It required two prerequisites that turned out to be missing, and both were latent
  defects rather than new features:**

  - **Children did not die.** The young died at the same rate as the middle-aged, so
    there was nothing for medicine to fix and no transition was available. Roughly half
    of all children born did not reach fifteen, across societies as different as
    classical Rome, Tokugawa Japan and Stuart England. The 5.25x multiplier is DERIVED
    from that datum under this model's mortality, which also makes the quantity-quality
    response exactly neutral pre-modern.
  - **Cohorts did not age.** Births piled into the youth cohorts and stayed there
    forever while nobody replaced the workers who died. Survivable only while the young
    died at the same rate as everyone else — the moment child mortality was represented,
    the youth cohort became a trap that swallowed the entire birth stream and the climb
    stopped dead. There is now an age ladder (youth 18 years, working 47), conserved.
  - **And the birth rate was the MODERN one.** 12 per 1000 stood in for a demography
    whose real components are three times larger; with a real age structure a stationary
    population needs ~29 per 1000, so 12 collapsed earthlike from 21,127 to 1,200 at a
    food surplus of 4.6. It is now the pre-modern ~40 per 1000 that every agrarian
    society actually ran, and the near-zero Malthusian growth EMERGES from real
    components instead of from the gap between two numbers never meant to be compared.

  Thresholds recalibrated afterward by the documented procedure; earthlike lands on every
  historical era date within two years again, and the spectrum holds (garden era 8 at
  12,042, fertile deathworld at 11,196, barren deathworld still Neolithic at 13,000).
- **Quantity-quality fertility transition (original note, Galor).** Once returns to human capital
  exceed returns to child quantity, families substitute education for fertility,
  breaking the Malthusian income->births feedback. Model DESIRED SURVIVING children:
  `desired_births = target_surviving / P(survive_to_15)`. As survival goes 0.5 -> 0.9,
  births fall ~44% from that channel alone. Flagged by the research as the single
  highest-value fix for "population still rising at collapse".
- **Institutions that outlive people — PARTLY LANDED 2026-07-28 (R4B). ROADMAP COMPLETE
  as specified.** The fixed 8%/yr surplus->capital rate is now DERIVED, exactly as the
  roadmap asked: `s_eff = s * exp(-expropriation_hazard * horizon)`, where the horizon is
  how long capital must survive to be worth raising (its service life, 1/depreciation,
  ~33 years).

  The hazard is composed from things the model already tracks as real located facts, not
  from a governance dial: institutional trust (whether people believe their property is
  safe), the Political Stress Index (a polity coming apart is exactly when things get
  taken), and the war death fraction (an army killing a percent of you this year is also
  burning your granaries). At 1%/yr a society builds ~72% of what it wanted to; at 5%/yr,
  under a fifth — the difference between a civilisation that accumulates and one that
  merely survives.

  **STILL OPEN:** perpetual LEGAL PERSONS as such — the corporation, the monastery, the
  guild endowment — so that capital and archives survive a specific owner's death rather
  than being dispersed at every generation. That needs an ownership model the commons arc
  does not have (proto-capital is per-resident and dies with them), so it is a real piece
  of remaining work rather than something folded in here. What IS modelled is the
  credible-commitment half: the security that makes long-horizon investment rational.
- **Institutions that outlive people (original note).** Perpetual legal persons (corporation, monastery,
  guild endowment) so capital and archives survive an owner's death; credible commitment
  against confiscation lengthening investment horizons. Our fixed 8%/yr surplus->capital
  rate should be DERIVED: `s_eff = s * exp(-expropriation_hazard * horizon)`.

---

## Recommended order

1. **1A codified knowledge** — without it every other improvement is erased each cycle.
2. **2C specialist inertia** — small change, fixes the 0%-in-one-tick discontinuity and
   feeds 2D.
3. **2A wage valve + 2B urban graveyard** — DONE 2026-07-28. Brought urbanisation to
   3-5% (emergent) and uncovered the soil-renewal error that was pinning the whole
   agrarian phase. See 2B for the measurements.
4. **1B energy + 1C induced innovation** — DONE 2026-07-28 as the `energy_base` module.
   The ceiling can now rise, and the rise is finite because the stock is. See 1B.
5. **2D elite overproduction, Tier 3 texture, Tier 4 transition.**

Do NOT iterate era thresholds again until 1A and 1B land: the calibration would be
fitting to a transient that these mechanisms are about to change.
**Both have now landed (2026-07-28), so F7 is unblocked** — and it is needed: the
measured climb reaches era 2 at year ~5,500 (right) and then crosses eras 3 through 8 in
~1,300 years, because knowledge grows exponentially in a population that grows with the
ceiling. The pacing is front-loaded backwards against the thresholds we have.

## R3A. Ideas get harder to find — the natural limiter on advancement speed

**Not in the original roadmap; found by trying to do F7 and discovering it was impossible.**

The knowledge engine was strictly linear in the number of people doing knowledge work,
with no dependence on how much was already known. That is empirically wrong and the
evidence is overwhelming: US research productivity has fallen roughly 41-fold since the
1930s while the number of researchers rose more than twenty-fold; sustaining Moore's law
now takes about eighteen times the research effort it took in 1971; the same pattern holds
for crop yields and for medical progress. The easy discoveries are made first, and every
one made leaves the next harder. Producing vastly more data than fifty years ago has not
bought flying cars.

Its absence was structural, not cosmetic. Measured: an earthlike world spent 6,700 years
nearly flat and then crossed FIVE eras in 1,300 years on a near-vertical spike, because
population and knowledge fed each other with nothing in between — after which knowledge
PEAKED and decayed. The knowledge held at each era's historical year was therefore not
even monotone (the Medieval target year held less than the Classical one), so **no set of
era thresholds could have placed the eras at their real dates**. F7 was not a calibration
problem and could not have been fixed by calibration.

- Jones' semi-endogenous form: `dK/dt ~ L / K^beta`, expressed against a reference stock
  so the dawn is untouched — `production /= (1 + K/halfsat)^beta`, `beta = 1`.
- It applies to the genius leaps too: Newton had harder problems available than Archimedes
  and Einstein harder ones than Newton. A genius is one mind at the frontier of the day,
  and the frontier recedes.
- Nothing is capped. The next discovery simply costs more than the last.
- **Fixes:** the knowledge trajectory is now monotone across all 13,000 years, with a
  realistic accelerate-then-decelerate shape, which is what made F7 possible at all.

## F7 — the era thresholds, re-anchored (DONE 2026-07-28)

Calibrated against EARTHLIKE, because it is Earth. Measuring from the dawn of
agriculture, the climb now lands on the real dates:

| era | historical | reached | error |
|---|---|---|---|
| Bronze Age | 6,700 (3300 BCE) | 6,701 | +1 |
| Iron Age | 8,800 (1200 BCE) | 8,801 | +1 |
| Classical | 9,450 (550 BCE) | 9,451 | +1 |
| Medieval | 10,500 (500 CE) | 10,502 | +2 |
| Early Modern | 11,450 (1450 CE) | 11,451 | +1 |
| Industrial | 11,750 (1750 CE) | 11,752 | +2 |
| Turn of Millennium | 12,000 (2000 CE) | 12,001 | +1 |

And the spectrum falls out of the SAME thresholds rather than being tuned per world:
garden reaches era 8 at 11,816, fertile deathworld at 11,136 (a harsh but bountiful world
out-innovates a comfortable one — the Deathworlders premise), and barren deathworld never
leaves era 2 in 13,000 years.

**Calibrate SEQUENTIALLY, bottom-up.** Each era transition itself accelerates knowledge
(better occupations, a higher specialist ceiling, tech multipliers), so fitting all seven
thresholds at once oscillates: values measured while a society is stuck in era 1 are
meaningless once it reaches era 2. Set every later threshold unreachable, calibrate one
era, fix it, move up. The script that does this is in the session scratchpad and the
procedure is recorded here because it will be needed again after any mechanism change.

Two guards now hold the result: a unit test that the thresholds strictly increase (the
condition whose violation made F7 impossible), and an emergence test that the earthlike
climb reaches every era within 1,500 years of its historical date.

## R6. Knowledge is held somewhere (LANDED 2026-07-28)

Knowledge was a single global number, and that was the deepest reason no civilisation could
ever fall. With one figure for the whole world there is no such thing as one society
collapsing while another rises: there is one society with six provinces, and the only
trajectory available to it is the world's. Every fall the record contains is REGIONAL —
Mycenaean Greece lost literacy for four centuries while Egypt and Assyria carried on
writing; the Maya lowlands emptied while the highlands did not; Rome's west fell and its
east did not.

- `cohort_stats.knowledge_level` per province, persisted (schema v30). Production,
  forgetting and decay are local, against the province's own stratum and archives.
  Adversity is local too, so a place pressing on its land intensifies while its neighbour
  does not.
- `technology.knowledge_level` is the MAXIMUM over provinces: the frontier, which is what
  an era is dated by anyway. The Bronze Age is dated by whoever had bronze.
- **Diffusion, and knowledge is NOT conserved.** A province learns a hundredth of the gap
  a year from each better-informed neighbour, and the neighbour forgets nothing — copying
  a text leaves the original. This is what lets a dark region relearn rather than restart
  (Greek mathematics returned through Arabic), and why catching up is faster than leading,
  since ideas get harder to find against what a place already knows.
- The carrying ceiling and mining technique use LOCAL knowledge, so a region that loses
  its engineers really does farm and mine worse.
- A genius leap happens in ONE PLACE and spreads only by contact.

**One defect caught in the rewrite:** the adversity factor was accidentally dropped from
the genius term, which let a single mind out-produce a two-million-person society. A genius
works under the same necessity as everybody else — which is why hard times produce hard
thinking.

Thresholds recalibrated afterward (each province now accumulates roughly a sixth of what
the shared pool did). Earthlike lands on every historical era date **within one year**:
Bronze 6,689/6,700 · Iron 8,801/8,800 · Classical 9,451/9,450 · Medieval 10,501/10,500 ·
Early Modern 11,451/11,450 · Industrial 11,750/11,750 · Turn of Millennium 12,001/12,000.
The spectrum holds: garden era 8 at 11,952, fertile deathworld at 11,066, barren deathworld
never leaves era 1.

**What it does not yet do:** the four observed seeds still show no era REGRESSION. The
capability is now present — a province can lose what it knew while its neighbours keep it,
and the ceiling and mining follow — but a regional collapse deep enough to pull the world's
frontier down has not been observed. The frontier is the max, so it falls only when the
LEADING province falls, which the diffusion actively prevents. Whether that is right (the
species does not forget what any one civilisation forgets) or whether era should be
per-polity rather than global is the next question, and it is a design question rather than
a defect.

## R12. The rail came out and four things were behind it (LANDED 2026-08-20)

The per-regime specialist ceiling — `clamp(specialists, 0, population x
{0.15 subsistence, 0.18 coinage, 0.22 money, ... 0.45 industrial})` — was a
behaviour-shaping cap on a finite quantity, which the grounding doctrine forbids
outright. Measured, it **bound at every era**: the supported share sat exactly on
15.0%, 18.0%, 22.0% for the whole climb, and the model reached era 8 with 92% of its
people still farming. It is gone, replaced by the physical constraint it stood in
for — a non-farmer must be fed from somebody else's field and the grain has to reach
him, which is the ox law `grain_logistics` already computes as `urban_capacity`. The
stratum is now `min(what the harvest can spare, what haulage can provision)`: two real
limits, the smaller binding.

Removing it exposed four defects it had been holding down. That is what removing a rail
is for, and each one is a distinct failure mode now written into
`docs/design/EconLife_No_Rails_Rule.md` with the test that catches it.

**1. The granary scaled the stratum instead of feeding it.** The level a society would
defend was `max(supported, held x granary_cover)`. Because `target_store` scales with
population, any growth holds cover below 1 permanently, so `defended < held` every tick
and the stratum ratcheted down forever — 7.9% held against 23.3% supported after
millennia. What stored grain actually buys is the ability to carry people *this*
harvest cannot, so it is now `supported + food_store / (population x annual need)`: an
additive number of person-years, not a multiplier on the stock it is supposed to
protect.

**2. A per-tick rate in a harness that runs one tick per year.** The stratum inertia was
`rate / ticks_per_year` applied per tick while the history harness fast-forwards
annually, so it advanced 365x too slowly there — and every measurement of it taken
before this was taken under that regime. It looked exactly like a slow mechanism. All
four stocks this module evolves (granary, soil, capital, stratum) now move on one
stated annual cadence.

**3. The labour saturation was scaled against the wrong quantity.** `labor_half_saturation`
was a flat 1,500 workers while provinces hold 15,000 to 3,000,000 people, so every
province sat deep in the saturated region where marginal labour is worth nothing and
the food balance read almost the entire workforce as spare — which is precisely what
the specialist ceiling had been compensating for.

Making it proportional to natural capital fixed the scale and broke the physics: the
carrying ceiling and the half-saturation then *both* carried soil fertility, so their
ratio — the marginal product of a pair of hands, which is what a thinly settled
population lives on — came out identical on a river valley and on scrubland. Land
quality had been silently divided out. Hands are spent by the acre and harvest is taken
by the quality, so production now separates the two: `natural_capital_of` (extent x
quality) sets the ceiling and the new `workable_extent_of` (the areal and stock terms,
unscaled by soil wear — exhausted fields are still fields and still cost the same
labour) sets the saturation.

**4. The growth signal was the specialist solve reported back to itself.** `labor_needed`
is solved so the harvest equals `need + granary_demand`; at a full store that is
`need + full_upkeep`, which was also the denominator of the population-growth signal.
The signal therefore read ~1.0 **by construction**, at every level of abundance,
forever. The demography could never see a rich world, so the population never grew into
its land, so labour stayed spare, so the assignment freed still more specialists — a
closed ring with no external quantity in it. Measured under it, a dawn world sat at
7,000-13,000 people for six and a half millennia with 47-64% of them off the land, and
reached the industrial era with 97% not farming because nobody had ever needed to.

The question fertility answers is whether the land can feed another mouth, not whether
this year's labour allocation does — a society that goes hungry puts its scribes back
in the fields. So the signal is now measured on the harvest the **whole workforce**
would bring in, against sustainable need. Actual output, which drives the granary, the
soil pressure and starvation, is unchanged and still farmer-only. With the ring broken
the dawn population grows instead of freezing.

**Enforcement.** Five `[no-rails]` unit tests, each asserting a shape rather than a
number: what a province can spare does not depend on its era label; haulage binds and
responds to the world; stocks move only on their stated cadence (a mid-year tick must
not shift the stratum); hands are spent by the acre and harvest taken by the quality
(and at saturation the ground stops mattering); and the growth signal moves when the
land alone is made better, with the stratum free to absorb every scrap of the
difference.

**Still open — the dawn keeps too many people off the land.** With the ring broken and
the constant re-derived against the provinces people actually settle, the earthlike
dawn runs a surplus of 1.6-2.0 and spares 30% of its population, against a historical
5-15%. The arithmetic is tight and worth stating: below the knee of the production
curve the sparable share is `1 - 1/S`, so a society with surplus S **necessarily** has
that share spare. History ran at S ~= 1.02-1.05 because population pressed hard on
land; here population grows at 0.016%/yr while the knowledge-driven ceiling outruns it,
so S climbs instead of being eaten. Two candidate causes, neither yet measured:
mortality that does not relax with abundance (`food_surplus_mortality_relief` 0.15,
floor 0.5, against a fertility elasticity of 0.40), and an urban share of 30-45% at the
dawn, which puts the urban-graveyard penalty on nearly half the population and may be
the brake. This is the next link in the chain, not a regression from the railed
version — under the rail the same slack existed and was simply not visible.


## R13. The population presses on its land, and the land answers (LANDED 2026-08-22)

R12 left one thing open: the dawn spared 30% of its people against a historical 5-15%,
because the population never grew into its land. Chasing that down found the cause, and
the cause was four more rails and one inert world.

**The population could not grow, because a constant would not let it.**
`commons_stability_floor` = 0.76 was substituted for the political stability score in
pre-market eras. Stability multiplies births and divides into mortality, so that single
number fixed the surplus at which births met deaths — 1.45, measured — and below the knee
of the production curve the sparable share is exactly `1 - 1/S`, so it fixed the size of
the non-farming class with it. Its own comment said so: *"Higher => population pushes
closer to the ceiling (less surplus)."* The political channel is now neutral in the
commons and real mortality does the work.

**The mortality it stood in for was the right number from the wrong end of the chain.**
`base_annual_death_rate` held 0.008 — Earth's crude death rate in the 2020s — while the
tech tree's medicine multiplier (herbal medicine, aqueducts, sewers, inoculation, germ
theory, vaccination, modern sanitation: x0.215 over the arc) then cut it *further*, to
0.0017. Every figure in the comment was real; it simply sat before the modifiers instead
of after them. It is now the pre-medical adult rate for a population that is NOT hungry
(hunger is applied separately, and feeding the observed 35-40/1000 in here counts
malnutrition twice) — 0.014/yr, the British peerage of 1550-1750, with the youth
multiplier re-derived so half of children still fail to reach fifteen. Both ends of the
record now land: 29 per 1000 at the dawn, 6 per 1000 modern, with medicine doing the fall
between them.

**The granary was adding person-years to a fraction.** The stratum's defended level read
`supported_fraction + food_store / annual_need` — a share of people plus years of grain.
A full reserve holds `granary_reserve_years` of food for everybody, so the second term
read 2.0, the target pinned at 1.0, and every society in the model was being told to put
its entire population off the land. It had survived a rewrite of the very line it was on.
A granary carries the stratum it HAS through a bad year and never funds a bigger one, so
it now covers the gap between what is held and what this year supports, bounded by both.

**And what can be spared is not what can be taken.** The model asserted that anyone the
fields did not need became a specialist. That is the one thing a subsistence economy never
did: a village that could feed itself with two thirds of its people had underemployed
farmers, not potters. A non-farmer eats grain somebody else grew and gave up, so somebody
had to be able to find it, measure it and enforce a share — and that reach has two real
channels. Reciprocity moves food among people who can know one another and collapses
beyond that (`kin_obligation_scale`, Dunbar); records move it among strangers and improve
without limit. The earliest writing anywhere, Uruk IV, is ration lists and grain accounts,
which is not a coincidence: writing is the technology of extraction, and it is why the
first cities and the first tax registers appear together. This also gets the surplus theory
of state formation the right way round — societies did not build states because they had a
surplus; they could keep a surplus once they built the apparatus to claim one.

**The environment was a set of constants that fed people for free.**
`geography.forest_coverage` sat at exactly 0.2822 for four thousand years while the
population living off it doubled, and `fishing_effort` was an annual harvest fraction
unrelated to whether anybody lived there — a province emptied by plague landed the same
catch. Now:

- **The wild stock is a stock.** `cohort_stats.forest_health` is the standing forage and
  game as a share of what the climate carries, drawn down when the take exceeds what the
  woods renew and closing back over at forest-regrowth rates. It is the counterpart of
  `soil_health`, and it is what makes a forager economy self-limiting — the reason
  hunter-gatherers ran at 0.01-0.1 persons/km2 against early farming's 5-20.
- **The fishery is pressed by the fishers.** Subsistence publishes `commons_fishers` (the
  fishery's share of the food base applied to the food-producing workforce) and
  seasonal_agriculture turns it into the effort behind its Schaefer harvest, as a
  first-arrival rate in the boats. Two populations interacting, instead of one of them
  being a number.
- **DESERTIFICATION.** Losing a field's fertility and losing the field are different
  things on different clocks. `cohort_stats.topsoil` is the soil profile itself, and it is
  the ceiling `soil_health` can recover to — you cannot restore fertility to ground that
  has gone. It erodes when the land is worked past what it renews AND the wild cover that
  held it is gone, multiplied, because it takes both: the Dust Bowl needed the sod-busting
  and the drought, the Mediterranean hills needed the deforestation and the goats. It
  re-forms at a centimetre every two to four centuries, which is to say never, on the
  timescale of the society that caused it.

**One more found by asking what the fish stock does over the whole climb.** It did
nothing: it sat at exactly its carrying capacity, to three decimals, from year 6,000 to
the industrial era while the population living off it went from 7,500 to three million.
A quantity resting precisely on its bound, which is what that always means. The seasonal
closure asked whether TODAY fell in the closed season — `tick_of_year / ticks_per_year <
seasonal_closure` — which is right only if every tick runs. Any caller that samples the
year at one point samples the same point every year, and the history harness fast-forwards
on year-aligned ticks, so `tick_of_year` was always 0 and every fishery in the dawn was
closed for thirteen thousand years. It is now an open-FRACTION multiplier: the same annual
landings, and stride-free. (The Schaefer equilibrium `N* = K(1 - F/r)` carries the stride
in both terms and it cancels, so a coarse stride gets the right fishery, just later — an
explicit LOD scaleback, documented at the site.) The same pass stopped a stale
`commons_fishers` standing in for a live one once subsistence goes inert in market eras.

With that fixed the fishery reads as it should: essentially untouched through the dawn
(a few hundred fishers take less than it renews, so it recovers to just under carrying
capacity), then turning over at the Bronze Age transition and falling monotonically to
55% of capacity by the industrial era, and still falling. It declines far more gently than
the forest — 45% against a near-total collapse — which is the right ordering: fish breed
in a season and a forest takes a century.

**Measured, the earthlike dawn is now a Malthusian one.** The founding population of
21,127 overshoots, strips a quarter of its topsoil and half its woods inside 250 years,
and crashes to 4,569 — a 79% fall. The forest closes back over within fifteen centuries;
the topsoil is still 12% short of pristine four thousand years later and creeping back
geologically. The non-farming share runs 2-13% against a historical 5-15%, rising with the
surplus and falling as the population outgrows what reciprocity can reach. Population
growth runs at hundredths of a percent a year, which is the Neolithic. Era thresholds
recalibrated; earthlike still lands every historical era date and the pacing gate passes.

**Six new `[no-rails]`/`[ecology]` tests**, each asserting a shape rather than a number:
over-taking wild food eats the woods and responds to the pressure; a cut-over wood yields
less but is no smaller; desertification needs both drivers and neither alone will do it;
fishing pressure scales with the fishers; the claim reach is village-scale without writing
and approaches total integration with it, never reaching it; and a granary carries the
stratum it has without ever lifting it.


## R14. Era-gated technology — the rail is confirmed, and it is blocked on the knowledge scale (ATTEMPTED 2026-08-22)

**The rail.** `technology_catalog.cpp`, `aggregate_effects`:

```cpp
for (const auto& n : nodes_)
    if (n.era_available <= era) { e.knowledge_mult *= …; e.food_mult *= …; e.mortality_mult *= …; }
```

Every technology a society holds is decided by the era integer alone. It is handed the
plough, the aqueduct, inoculation and germ theory the moment the era advances, regardless
of what it knows, what it has built, or whether it ever researched anything — the "it
rises with the era/tier/level" form the no-rails rule names as the most dangerous. Two
consequences, both measured:

- **A step at every era boundary.** Earthlike surplus goes 1.58 -> 3.95 and population
  growth 0.037%/yr -> 0.154%/yr across the era 1 -> 2 transition, with nothing in the
  world changed but an integer. The era-1 tree alone multiplies the carrying ceiling by
  ~3.0, so it is also the largest single term in the dawn food balance.
- **No two provinces can ever differ in technique.** The era is global, so Britain and
  Qing China are the same society by construction — which is exactly the case the R9
  "knowing is not having" work exists to express, and it cannot be expressed while this
  stands.

**The replacement, built and reverted.** A technique needs both knowing it and having the
means, each as a saturating share rather than a switch, with a node bounded by its least
penetrated prerequisite: `effect = 1 + (mult - 1) x knows x has x prereqs`, resolved per
province, published annually on `cohort_stats.tech_{food,mortality,knowledge}_mult`. It
worked, and the behaviour was better in exactly the ways predicted: the era boundary
became a ramp (1.88 -> 2.10 instead of 1.58 -> 3.95), and the fertile deathworld began to
rise and fall repeatedly — era 5, collapse to 4, back to 5, collapse to 4, to 3, then 4, 5,
6, with population swinging 1.6M -> 56k -> 1.5M -> 58k. That is the secular cycling the
brief has asked for from the beginning and the model had never produced.

**Why it is reverted.** The knowledge axis has no anchor, and this change needs one.

1. Anchoring a technique's cost on the era knowledge thresholds is the obvious move and it
   is forbidden by our own rule: those are the model's ONE pure pacing dial — "they label
   a trajectory, they do not shape it" — and feeding them back into a mechanism makes them
   load-bearing. It also breaks the calibration in practice, because the thresholds then
   determine the technique costs that determine the knowledge rate that determines the
   thresholds. Measured: era 1 calibrated to 66 and era 2 to 131,911, a two-thousand-fold
   climb in two millennia, and the earthlike world stopped reaching era 7.
2. Anchoring it on the node's own authored `difficulty` is correct in principle — no
   feedback, and "each further unit of difficulty nearly doubles the knowledge a society
   must hold" is Jones applied across the tree rather than along it. But it needs a unit
   bridge from difficulty to accumulated knowledge, and there is nothing to bridge TO: the
   knowledge scale floats. `discovery_difficulty_halfsat` (35,000), the era thresholds and
   the new scale constant all live on one axis that only the calibration pins, and the
   calibration pins it differently depending on how much of the tree is reachable. Set the
   bridge high and no technique is ever reachable (the world tops out near 5,000 knowledge
   and stays Neolithic); set it low and the tree's own knowledge multipliers — a compound
   ~117x across writing, the alphabet, the university, the press and the method — run away,
   crossing every era inside a few centuries and producing a non-monotonic threshold ladder.

**What unblocks it.** The era thresholds should stop being the calibrated quantity. Fix
them as authored content on a stated scale, and calibrate the knowledge PRODUCTION RATE
instead — one dial rather than seven. Then the knowledge axis has a fixed meaning, a
technique's cost can be authored against it without feedback, and the shape of the climb
becomes an emergent prediction rather than a seven-point fit.

That is a change to what the `[pacing]` gate means — it currently asserts all seven
historical dates, and with one dial only one date can be targeted — so it is a design
decision rather than a fix, and it is the prerequisite for this work.

The attempt is kept at `wip-per-province-technique.patch` (session scratchpad) with its
five `[no-rails]` adoption tests, all of which passed.


## R15. An era is a set of techniques (LANDED 2026-08-23)

R14 found the rail and could not remove it: era-gated technology needed a knowledge axis
with a fixed meaning, and the axis floated with a seven-point calibration. This is the
restructure that gives it one, and the tree that makes it worth having.

**The rail, restated.** `aggregate_effects` switched on every node with `era_available <=
era`, so a society was handed the plough, the aqueduct, inoculation and germ theory the
moment an integer ticked over. Measured, the era 1 -> 2 boundary raised the earthlike food
surplus from 1.58 to 3.95 with nothing in the world changed, and no two provinces could
ever differ in technique because the era is global.

**The knowledge axis now means something.** It is measured in technique-equivalents: a
node of authored difficulty d stands for `exp(k(d-1))` times the learning of the simplest
thing in the tree, and the era ladder is the running total of those weights up to each
era. So the ladder is CONTENT — a pure function of the tree, regenerated by
`set_content_exponent.py` and asserted against eras.csv by a unit test. It is never
fitted. Mod the tree and the ladder follows.

**Eras advance by research, not by a number.** A society moves past an era when it has
worked out three quarters of that era's MAIN PATH — and because a node's adoption is
`knows x has x prerequisites`, that single rule subsumes the two fitted gates it replaced.
Knowing how to make bronze is not the Bronze Age; having the smelters is.

**The tree.** 467 nodes, 385 of them pre-modern, each marked `main` or `side`:

| era | main | side |
|---|---|---|
| 1 Neolithic | 10 | 49 |
| 2 Bronze Age | 9 | 44 |
| 3 Iron Age | 7 | 41 |
| 4 Classical | 8 | 44 |
| 5 Medieval | 8 | 51 |
| 6 Early Modern | 8 | 46 |
| 7 Industrial | 8 | 52 |

Side paths are depth: never required to advance, paying in bonuses, unlocks and later
prerequisites. A society that pours everything into its side branches becomes formidable
at what it does and stays where it is, which is a thing societies really do. The graph is
checked: every prerequisite resolves, nothing depends on a later era, no cycles.

**One dial.** `KnowledgeConfig::knowledge_rate` — the clock, bisected against a single
anchor (the Bronze Age at 3300 BCE). `calib_seq.sh` and `set_thresholds.py` are deleted.

**And the `[pacing]` gate stopped grading the speed of ascent.** It asserted seven
historical dates, which could only pass because seven numbers had been fitted to make it
pass. It now asserts the shape of a climb — eras arrive in order, none is skipped, none is
free, the dawn is long — and a companion test asserts that different worlds get different
distances, which is the point of having world classes at all.

**Two defects found on the way, both worth naming.**

*Multiplying independent bonuses.* The authored tree gave many nodes small food and
knowledge gains, composed as a product. A better sickle, a threshing sledge, a heavier
plough and a three-field rotation all raise what an acre yields and they all address the
SAME acre, so the second saves less than the first. Multiplied instead, the pre-modern
tree came to x9,557 on food and x44,758 on scholarship, and six provinces carried 239
million people. Each adopted node now contributes its raw gain to a BUDGET and the effect
saturates toward a bound that is a fact about the world rather than about the tree —
tenfold on organic yields, a sixfold cut in mortality. For small budgets it is
indistinguishable from multiplying; it bends only where reality bends, and it is robust to
a modded tree.

*A threshold the starting position already half satisfies.* At an advance share of 0.5,
four of the Neolithic's ten main techniques are the package a founding people already has
(difficulty at or below the free floor), so a society crossed the line with almost nothing
earned and left the Neolithic in eight hundred years no matter how slowly it learned. The
bisection saturating — the same answer for every value of the dial — is what exposed it.

**Where it stands, honestly.** The mechanism is in, the tree is in, the gates are green
(1,865 fast tests, the behavioural suite, the rewritten pacing gate). The world it produces
is not yet good: earthlike reaches the Bronze Age at 6,626 (anchored), the Iron Age at
7,082 and the Classical era at 8,034, and then oscillates violently in eras 3-4 for the
remaining five thousand years — population swinging 46,000 to 460,000 and back, surplus
between 0.78 and 8.49, knowledge cycling around 600-1,900 rather than accumulating. That
is a Malthusian limit cycle: the food ceiling now moves with technique over a twelvefold
range and the population chases it. It is the next thing to work on, and it is a
behavioural problem downstream of an architecture that is finally the right shape rather
than a defect in that architecture.


## R16. The wheel: five spokes, a hub, and four of five to turn an era (LANDED 2026-08-23)

R15 made the era a set of techniques. This makes it a WHEEL, and expands the main path from
58 nodes to 173.

**Five spokes, each running the whole way from the hearth to the reactor:** energy,
materials, life, knowledge, society. Every era authors a main line in every spoke — 4 to 5
techniques each — so a society always has five things it could be working on and must
choose. Filling that in meant authoring the eras where a spoke had nothing: era 1 and era 6
had no energy content at all, era 7 no society content, and eras 3-4 barely any energy.
Thirty-five nodes were written for those gaps (the hearth, the bow drill, banked embers,
draught animals, the forge hearth, the hypocaust, the horse gin, peat cutting, the Savery
pump, limited liability, trade unions, the factory acts, compulsory schooling and the rest).

**The hub.** Thirty nodes cost nothing to know — fire-making, foraging lore, cordage, flint
knapping, hafting, the bow, oral tradition, burial rites, the first pots and cloth. These
are unarguably on the trunk (there is no civilisation without fire) but they are not things
a society RESEARCHES; they are what a people arrives with. So they sit at the centre of the
wheel, drawn on their spokes, and excluded from the advancement measure.

That exclusion fixes a real defect rather than tidying a diagram. With the founding package
inside the measure, era 1's main path was 40% satisfied before a society had learned a
single thing, and the Neolithic ended in eight hundred years no matter how slowly it
learned. The signature was the clock bisection returning the same year for every value of
the dial — the advancement was not knowledge-limited at all.

**Four of five spokes to advance.** Not all five, which produces only well-rounded societies
and makes any single neglected spoke a hard stop. Not the mean, which lets brilliant
engineering with no institutions carry a society through — the thing several sessions of
work went into making impossible. Four of five is the shape the record has: Song China deep
in knowledge and thin in institutions, Britain rather the reverse, and both advanced. It is
also the first time the model can hold those as different KINDS of society rather than one
society at two speeds.

**The tree now:** 502 nodes — 173 main, 299 side, 30 hub. Materials 151, life 81, knowledge
80, society 65, energy 43 across eras 1-7. Every prerequisite resolves, nothing depends on a
later era, no cycles.

**And the `[pacing]` gate stopped asserting a distance.** It required earthlike to reach era
4 or better. That is grading the ascent, which is the one thing the gate exists not to do —
the same reading fails a world that is simply slow. Liveness is now the only distance claim
it makes: a world must leave the Neolithic. Shape (order, no skips, nothing free, a long
dawn) and divergence between world classes carry the rest.

**Still open, and unchanged by this work:** the earthlike climb stalls in eras 3-4. Bronze
Age at 6,655 (anchored), Iron Age at 7,215, Classical at 11,153, and then a violent
Malthusian limit cycle — population swinging between 46,000 and 460,000, surplus between
0.78 and 8.49, knowledge cycling rather than accumulating. The cycle is: crash, then a small
population sitting on a high technique ceiling, then explosive growth, then overshoot, then
crash. The wheel did not cause it and does not cure it; it is R15's open item and it is the
next thing to work on.


## R17. The limit cycle was a ratio with a collapsing denominator (LANDED 2026-08-23)

Earthlike is the grounding case: it should land near year 12,000. It was instead running a
90% boom-and-bust every seven hundred years and never leaving the Iron Age. Four defects,
and the last is the one that mattered.

**Capital per HEAD is a ratio whose denominator collapses.** It gated two things — how much
of its knowledge a province can apply (R9) and how far machines replace hands (R11) — and
after a die-off it lies in the most damaging possible direction: a province that loses nine
tenths of its people reads as *ten times better equipped*. Measured, that closed a loop that
made the model uninhabitable:

> crash → capital/head spikes → the knowing-and-having gate flies open → the carrying
> ceiling jumps eightfold → the food signal reads 8.5 → fertility pins at the biological
> cap → the population overshoots → crash

A mill serves a valley, not a headcount. Drains, walls, terraces, roads and cleared fields
are fixed to the ground and do not become more useful because fewer people are left to use
them. Both gates now measure capital against **workable extent**, where the same die-off
leaves a province exactly as equipped as it was — which is what a silent mill actually is.
This is the same correction the labour saturation needed in R12, in a different place: ask
what the denominator is, and whether it can collapse.

**Three more found on the way there.**

*Mechanised farming borrowed the wrong knowledge gate.* `machine_leverage` used
`knowledge_productivity_halfsat` — the bar for technique raising the ceiling generally —
so on the content knowledge axis a Bronze Age society read as 80% "knowing machines", and a
fiftyfold labour leverage was one capital spike away. It now has its own gate set at the
industrial rung of the content ladder. A Bronze Age society whose population halved does not
get tractors.

*A technique is not lost at the speed of a treasury.* Making capital load-bearing for
technique created a second positive feedback: political stress raises the expropriation
hazard, which stops investment, which drains capital, which REMOVED TECHNOLOGY, which
lowered the ceiling, which raised stress. A stress spike took the surplus from 1.37 to 0.68
and the non-farming share from 11% to 1% inside a century. Technique effects now move toward
what knowing-and-having supports on a generational clock (~40 years): the fields are still
cleared and the ploughs still in the barn.

*A granary with a hard edge.* Famine mortality was off entirely while any grain remained and
then arrived at full strength the moment the store hit zero. A store protects in proportion
to what is in it, so a full store makes a bad year invisible, a half-empty one halves it,
and an empty one leaves the harvest to speak for itself.

**Measured result: the cycle is gone.** The earthlike dawn now falls to a floor of ~2,600
and holds it for six thousand years without a single crash, the surplus rises smoothly from
1.26 to 2.16, soil recovers from 0.89 to 0.99, and the non-farming share climbs 4% to 16%.
That is a Malthusian dawn rather than a sawtooth.

**Where the climb stands.** Bronze Age 7,259 · Iron Age 7,571 · Classical 8,014 · Medieval
11,430, against history's 6,700 / 8,800 / 9,450 / 10,500 — all four within about 1,500 years
from a single dial. It then stalls: Early Modern and beyond are not reached inside 13,000
years.

**And the clock response has gone bimodal**, which is the next thing to understand. Between
rates the era-2 date jumps discontinuously (7,259 at 0.15, 1,980 at 1.0, and *later* at
values between), which is the signature of a bootstrap threshold: building capital needs a
surplus and the surplus needs applied capital, so a society either gets over the hump or
does not. That is plausible as take-off dynamics and fatal as a calibration surface.
Lowering the Jones exponent (beta 1.0 -> 0.5) was tried and made it worse, so the brake is
not the lever.


## R18. What the falls actually are (MEASURED 2026-08-23)

The brief is a world that rises and falls and creeps forward. R17 removed a cycle that was
too violent to allow any accumulation; the question is what shape should replace it, and
this is the measurement that answers it.

**The falls are not demographic overshoot.** A preventive check was built and reverted:
scaling the fertility above replacement by the headroom under the ceiling, `1 - 1/S`, which
is the logistic and is what the European marriage pattern actually did. It pinned the world
— population at 1,900 for ten thousand years, knowledge crawling to 47 — because this
population is not crowded against its food ceiling at all. It equilibrates where NON-FOOD
mortality balances fertility, at a surplus of 1.2 to 2.0, with the land barely worked.
Damping fertility there does not damp a cycle; it moves the balance point and empties the
world.

So the deep falls come from the CEILING MOVING, not from the population running into a
fixed one — technique, stores and political stress. That is Turchin's structural-demographic
mechanism rather than a Malthusian one, and it means the lever is on the ceiling side.

**And the ratchet is broken, which matters more than the amplitude.** Measured over 13,000
years, knowledge oscillates rather than accumulates: 2,058 → 3,521 → 1,943 → 854 → 1,888 →
3,294 → 4,940 → 6,727 → 2,361 → 4,776 → 2,540. A civilisation that loses two thirds of what
it knew at every collapse can never creep forward, and the climb stalls at the Medieval era
for exactly that reason.

The corpus floor is built correctly (`sustainable = max(stratum_sustains, codified)`) and it
is not the defect. What breaks it is the DEPTH of the falls: at 94% population loss the
learned stratum goes to zero for centuries, nobody recopies, and the corpus decays at 1%/yr
until there is nothing left — 500 years of that leaves 0.7%. Real dark ages lost ~90% of
classical Latin literature over four centuries and left the rest; the difference is that
somebody was still copying.

**So the target shape, stated so it can be tested:** secular cycles of 20-40% amplitude on a
200-300 year period, knowledge rising monotonically THROUGH the falls, occasional era
regression, and the long-run trend reaching era 8 near year 12,000. We currently have
94% falls, a knowledge sawtooth, and a stall at era 5.

**The next lever is the ceiling, not the demography.** Specifically: what makes a fall stop
at 30% rather than 94%, and what keeps a remnant of the learned stratum alive through it.


## R19. The history arc was integrated at one step per year (LANDED 2026-08-23)

The dawn arc ran at ONE orchestrator step per in-game year — a 365x under-sample — and it
was corrupting the thing the whole brief depends on.

**The knowledge ratchet was an integration artefact.** Measured over 9,000 years at
otherwise identical settings, the world's knowledge fell to 68% below its running maximum
at one step per year, and to 1% at four steps or more:

| steps/yr | worst population fall | knowledge drawdown | era reached |
|---|---|---|---|
| 1 | 95% | **68%** | 3 |
| 4 | 84% | **1%** | 4 |
| 12 | 78% | **1%** | 4 |
| 52 | 83% | **1%** | 4 |

The corpus is recopied and diffused per TICK while it is forgotten per YEAR, so starving
the copying by 365x makes a civilisation forget what it knew at every collapse. Every
knowledge measurement taken this session before this was measuring the integration rather
than the model — including the era pacing and the clock it was fitted against.

The history stride is now **12 steps per year**, an explicit level-of-detail choice
documented at the site: the drawdown is already at its fine-stride value by four, twelve
costs about a second per thousand years, and the annual gates still fire exactly once a
year. A caller wanting the real game passes 365.

**And the population swings are NOT a stride artefact** — 78-95% at every stride — so they
are a property of the model and have to be worked as one.

## R20. What the seeds actually do (MEASURED 2026-08-23)

At the new stride, over 13,000 years, earthlike:

| seed | Bronze | Iron | Classical | end era | end pop | knowledge | worst fall | K-drawdown | falls >20% |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 7,426 | 7,778 | — | 3 | 262,073 | 893 | 96% | 75% | 6 |
| 7 | 7,383 | 7,679 | 8,187 | 3 | 61,930 | 896 | 96% | 85% | 4 |
| 42 | — | — | — | **1** | 2,020 | 12 | 88% | 12% | 1 |
| 1000 | 6,307 | 6,784 | 7,442 | 3 | 179,533 | 2,322 | 96% | 91% | 5 |
| 2024 | 10,584 | 11,376 | 12,010 | 3 | 861,498 | 2,419 | 96% | 65% | 2 |

Two things are working. **Seeds genuinely diverge**: seed 42 never leaves the Neolithic at
all, seed 1000 reaches the Bronze Age four thousand years before seed 2024, and end
populations span 2,020 to 861,498. And there are now **four to six falls per world** rather
than one — the repeated rise and fall the brief asks for.

Two things are not. The falls are **96% deep** against a target of 20-40%, and because they
are that deep the learned stratum goes to zero and the knowledge drawdown returns (65-91%
over the full span, even though it is 1% over the first 9,000 years). The ratchet failure is
downstream of the amplitude, which is the same conclusion R18 reached and now has the seed
spread behind it.

**Where the relief goes missing.** In the saturated regime a falling population raises its
own surplus — the Malthusian restoring force. In the LINEAR regime output is proportional to
labour, so surplus is independent of population and that restoring force vanishes exactly
when it is most needed. Measured at the trough, surplus reads 5.3 while the population is
still falling, so the late stage of a collapse is not food-driven at all: it is the age
structure, hollowed out by the famine, carrying the fall a generation past its cause.


## R21. What these people are (LANDED 2026-08-23)

A population was an undifferentiated headcount, and the rates that ought to depend on its
condition were free constants instead. Three stocks now carry it, each measurable in the
historical record — which is the test, because a stock nobody could ever have measured just
moves the arbitrariness one layer down.

| stock | unit | measured by | fed by |
|---|---|---|---|
| `nutrition` | adult stature as a fraction of potential | skeletal series, conscription records | childhood food, on a 25-year lag |
| `health` | share of the year fit to work | days lost to illness | disease, crowding, sanitation, hunger |
| `schooling` | mean years of learning per adult | signature rates, age heaping, guild indentures | teachers the surplus can spare |

**The last fitted number in the climb is gone.** `KnowledgeConfig::knowledge_rate` — "the
clock" — was bisected against a historical date so the Bronze Age would land on 3300 BCE.
That is steering toward a desired result: it made the answer true by construction rather
than as a consequence, and it had been fitted under a broken integration stride besides, so
it was steering toward an artefact. How fast a society works things out now comes out of
what its people carry: mean years of learning per adult (relative to the four an ordinary
literate pre-modern adult has) times the share of the year they are fit to use it.
`calib_rate.sh` is deleted with it, and nothing in the climb is calibrated any more.

**And a workforce is not a headcount.** Effective labour is now working-age share times
fitness times a stunting penalty — a standard-deviation height deficit costing about a tenth
of adult productivity, one of the better-measured facts in economic history.

**Why this matters beyond tidiness: a fall now LASTS.** Measured over the dawn, schooling
builds from 0 to 4.5 years and stature recovers from 0.906 to 0.964 of potential — and in a
collapse schooling goes to **0.00** while stature falls to 0.85. The headcount returns in
fifty years; stature takes a generation and schooling three, because the teachers are dead
and their pupils were never taught. That is the mechanism a ratchet needs to bite on, and
the model had nothing like it.

**Two bugs found in the building, both worth naming.**

*A pass hung off `execute()` in a province-parallel module is dead code.* The orchestrator
dispatches parallel modules through `execute_province` and never calls `execute()`, so the
capability pass compiled, linked, ran never, and read as working — until the stock it was
supposed to move was printed and had not moved.

*A stock entering in YEARS where the term it replaced was dimensionless.* Schooling
multiplied knowledge production directly, so a people with 4.8 years of learning produced
thirty times what the old constant did and the whole climb raced and shattered. It enters
relative to `reference_schooling_years` now, and the term is dimensionless.

**Where it stands.** The dawn is right: a long Neolithic to about year 3,500 with schooling
building 0 to 4 and stature recovering, then the climb. After that the same violent
instability as before — era swinging 5, 1, 5, 1, 5, 4, 3 with population between 34,000 and
1.5 million. The capability system neither caused it nor cures it; it is R18's open item.

Also cleared: `max_specialist_fraction` and the seven `specialist_ceiling_*` constants, the
rail R12 replaced months ago, along with a unit test that was still asserting the schedule
they encoded.


## Sources
Turchin & Korotayev demographic-fiscal model (arXiv 1504.04688); Turchin, Structural-
Demographic Analysis / PSI (PLOS ONE 2023); Tainter, The Collapse of Complex Societies;
Cline on Late Bronze Age network cascade (ScienceDirect 2023); PNAS on Maya lowland
recovery; Clark, The Condition of the Working Class (JPE 2004); Our World in Data,
Demographic Transition and Famines; EH.net, Economic Impact of the Black Death; Munro on
wages before/after the Black Death (MPRA 15748); Davenport on London mortality;
Demography of the Roman Empire; Jedwab et al. on medieval cities; Wrigley, The Path to
Sustained Growth; Allen, The British Industrial Revolution in Global Perspective;
Dittmar, Information Technology and Economic Change (QJE 2011); Gutas, Greek Thought
Arabic Culture; Galor, Unified Growth Theory; North & Weingast (1989); Mokyr/Scheidel
via the Fractured Land hypothesis.
