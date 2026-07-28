# EconLife — Realism Roadmap (research-backed)

**Status:** research synthesis, 2026-07-28. Not yet implemented.
**Purpose:** identify the mechanisms needed to move the simulation from "civilisations
cycle at a fixed height forever" to something that reproduces real history, WITHOUT
adding caps, floors or guiderails. Every item below is a mechanism with state, a rule,
and a measured defect it addresses.

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

---

## TIER 3 — texture and de-synchronisation

- **Recurrent plague waves.** England fell 4.8M (1348) -> 2.6M (1351) -> nadir 1.9M in
  1450: recovery took 150-200 years BECAUSE plague recurred. Model waves at 10-20 year
  intervals with declining lethality, not one blip. Wage response lags ~25 years.
- **Asabiya (collective-action capacity)** generated at contested frontiers, decaying in
  the interior: `dA/dt = +h*A(1-A)*frontier - dec*A*(1-frontier)`, polity power ∝ A*N.
  Adds a second oscillator so collapses DE-SYNCHRONISE across provinces instead of one
  global sawtooth. Hooks directly onto warfare's existing cohesion model.
- **Trade-network fragility.** Sea transport decoupled cities from their hinterland
  (Egypt shipped ~130kt of grain a year to Rome; moving grain 70 miles by road cost more
  than sailing it 1,400). Carrying capacity = own land + imports; when a route fails,
  famine scales with `import_dependence`. The Late Bronze Age collapse was a small-world
  network cascade — severing Ugarit cut Cyprus's tin and copper.
- **Printing.** Europe held <30,000 books before 1450 and 8-12 million by 1500; print
  cities grew ~35pp faster 1500-1600. Multiplies `K_codified` copy count by two orders
  of magnitude — this is what makes the ratchet permanent.
- **Polycentrism.** Innovation survives if ANY polity in a connected culture-area
  shelters it, so conquest/absorption acquires a growth COST and China's unified cycling
  vs Europe's escape becomes emergent rather than a dial.

---

## TIER 4 — the modern transition

- **Quantity-quality fertility transition (Galor).** Once returns to human capital
  exceed returns to child quantity, families substitute education for fertility,
  breaking the Malthusian income->births feedback. Model DESIRED SURVIVING children:
  `desired_births = target_surviving / P(survive_to_15)`. As survival goes 0.5 -> 0.9,
  births fall ~44% from that channel alone. Flagged by the research as the single
  highest-value fix for "population still rising at collapse".
- **Institutions that outlive people.** Perpetual legal persons (corporation, monastery,
  guild endowment) so capital and archives survive an owner's death; credible commitment
  against confiscation lengthening investment horizons. Our fixed 8%/yr surplus->capital
  rate should be DERIVED: `s_eff = s * exp(-expropriation_hazard * horizon)`.

---

## Recommended order

1. **1A codified knowledge** — without it every other improvement is erased each cycle.
2. **2C specialist inertia** — small change, fixes the 0%-in-one-tick discontinuity and
   feeds 2D.
3. **2A wage valve + 2B urban graveyard** — brings population and urbanisation to
   historical magnitudes; both are equilibria, not caps.
4. **1B energy + 1C induced innovation** — the ceiling that rises; without these the
   peak is fixed no matter what else improves.
5. **2D elite overproduction, Tier 3 texture, Tier 4 transition.**

Do NOT iterate era thresholds again until 1A and 1B land: the calibration would be
fitting to a transient that these mechanisms are about to change.

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
