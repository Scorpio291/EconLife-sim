# Origin Economy & Early Jobs — Design Note (v01)

*Status: proposal for review. Companion to `EconLife_Mechanical_History_Generation_Plan.md`
(the "Mechanical History Generation" plan, hereafter **MHG**). This note details the
**job/role and production model for the earliest era-bands (MHG Bands 0–2)** — the part
the MHG plan names ("subsistence; foraging, subsistence farming, handicraft; household/
barter") but does not yet flesh out. It does not re-argue the founding-seed architecture,
the era-arc re-basing, or the LOD/performance plan; see MHG for those.*

## 0. Why this note exists
Two facts frame the problem:

1. **The engine has no vocabulary for the origin.** Every `NPCRole` (npc.h) is
   modern-institutional — politician, regulator, prosecutor, corporate_executive, banker,
   lawyer, worker-*at-a-facility*. Production is `Facility` + `Recipe` + `NPCBusiness`.
   There is no forager, farmer, herder, healer, elder, or trader, and no notion of "labor
   applied to land." The entity model assumes year-2000 institutions. MHG Band 5 (modern)
   "exists"; Bands 0–4 are "new" precisely because this vocabulary is missing.
2. **Input-free production already landed** (commit *fertilizer/feed as yield modifiers*):
   crops/livestock now produce a subsistence base from land+labor without the fertilizer/
   feed industry. That is the *production-side* seed of Band 1 — but it is still expressed
   as a **facility running a recipe**, which (see §3) is the wrong primordial unit.

## 1. The master variable: food surplus
At settlement-dawn the only economy is subsistence, and a job is useful **only to the
degree the settlement produces more food than it eats**. The surplus ratio is the dial
from which everything else emerges:

- **surplus ≈ 0** → all hands get food; no specialists, no trade, no inequality, no
  hierarchy, no state.
- **surplus > 0** → it *frees hands*. Freed hands become artisans, healers, warriors,
  traders, chiefs. Specialization, trade, wealth concentration, and proto-politics are all
  **consequences of surplus**, in that order.

This makes surplus the natural **emergence threshold** MHG asks for ("what advances out of
the band: surplus/population/tech thresholds"). Concretely: the fraction of labor *not*
required for food is the budget for every Layer-2 role in §2, and the trigger for
money/market/firm emergence in Band 2.

## 2. The early jobs (mapped to subsystems the sim already tracks)
Two layers; Layer 2 switches on only once Layer 1 throws off a surplus.

**Layer 1 — food/survival base** (gated by land + labor + skill, *not* facilities or
industrial inputs):

| Job | Produces | Gated by |
|---|---|---|
| Forager / gatherer | wild plant food, firewood (fuel), fibers, herbs | forageable biomass |
| Hunter | meat, hides, bone (tool stock) | game density |
| Fisher | fish | water / fishery |
| Subsistence farmer | grain (no fertilizer — *already implemented*) | soil/land + climate |
| Herder / pastoralist | meat, milk, hides, wool, traction | pasture |

**Layer 2 — surplus-funded specialists** (each maps to an existing subsystem, so they are
"useful for the sim" — they drive machinery that already exists):

| Job | Sim subsystem it feeds |
|---|---|
| Toolmaker / potter / weaver / tanner / builder | the first "manufacturing" (raw → tools/pots/cloth/leather/shelter) |
| Healer / herbalist | health (sickness, mortality) |
| Elder / headman / chief | governance → proto-stability & legitimacy |
| Shaman / storyteller | community cohesion (seed of media/legitimacy) |
| Warrior / raider | defense **and** the seed of the criminal/violence economy — taking by force is the original criminal sector |
| Trader / peddler | seed of markets, inter-province trade, inequality |
| Granary / herd keeper | **origin of capital, inequality, and hierarchy** — control of stored food/seed/livestock |

## 3. Production without facilities
A `Facility` is a built, capital-bearing production *site*. That is an **emergent late
form**, not a primordial one — it appears only once accumulated surplus can fund a
dedicated site (cleared/irrigated fields, a kiln, a workshop, a mine). At the dawn the
production unit is **a household or band working the commons**: labor applied to the
province's natural capital, gated by resource availability + labor + skill.

Two ways to model this; recommendation below.

- **(A) Reinterpret "facility" broadly** — a primitive activity (forage band, subsistence
  plot, hunting territory) *is* a facility with zero construction cost, no building, no
  industrial input, worked by people. Cheapest path: the existing Facility/Recipe machinery
  (now input-free-capable) already runs it; `base_construction_cost = 0`, era-0 recipes,
  output gated by located resource + labor. **Conflict:** facilities are owned by an
  `NPCBusiness`, but at the dawn there are no firms — work is household/commons, not a firm.
- **(B) A distinct household/commons subsistence mechanism** — population (cohorts) and the
  few significant NPCs produce food/raw goods directly from province natural capital,
  *outside* the firm/facility model; facilities (and the businesses that own them) only
  appear in Band 2+ when surplus funds them.

**Recommendation: (B) as the model, reached incrementally via (A).** (B) is faithful
("no facilities at the origin; they emerge"), and it cleanly resolves the ownership
conflict — but it is a new production path. (A) is a low-risk *bridge* that proves the
content (era-0 input-free food recipes, carrying-capacity output) on the existing engine
while (B) is designed. The end state is (B): subsistence is a **commons** layer; the
**first facility is founded by a player/NPC when surplus justifies it**, which is also the
first `NPCBusiness` and the first employment relationship (§5 step 4).

## 4. The entity-model gap (what new primitives are needed)
- **Primitive roles.** `NPCRole` needs an early-band set (forager, farmer, herder, hunter,
  fisher, artisan, healer, elder/chief, warrior, trader) — or a more abstract
  `occupation`/activity attribute that the modern roles become a specialization of. Decision
  needed: extend the enum vs. introduce an era-tagged occupation table (data-driven, matches
  the project's CSV-content ethos and avoids recompiles).
- **Capital ≠ cash at the dawn.** "Capital" is stored food + livestock + tools, not a bank
  balance. Either map these to goods inventory the NPC holds, or generalize `capital`.
- **Employment ≠ wage labor.** Labor is kin/reciprocity/share-of-harvest. `labor_market`'s
  wage/posting model is anachronistic until Band 2; it must be gated (§5).

## 5. Emergence thresholds — switching the modern modules on
The conserved extraction→production→consumption *core* is era-agnostic (a major asset per
MHG §2b). The **behavioral layer** is what gates. Proposed switches:

- **Money/priced markets** (`price_engine`, priced `RegionalMarket`): off in Band 1 (barter/
  reciprocity); turns on in Band 2 when trade volume / surplus crosses a threshold.
- **Wage labor** (`labor_market` postings/wages): off until firms exist (Band 2+).
- **Finance** (credit, mortgages, banking): Band 3+.
- **Elections / state legitimacy** (`political_cycle`): off at the dawn — a band of villages
  has chiefs, not a state with national legitimacy. *This is the concrete bug observed when
  the emergence harness was given modern facilities: legitimacy math ran over a world that
  shouldn't have a state, and it sagged.* Governance starts as local elder standing
  (community cohesion / influence_network) and only becomes "national legitimacy" once a
  state forms.

The gate keys are the same family MHG already names: **surplus, population, specialization,
tech tier.**

## 6. The player at the dawn (big fish, small pond)
Low population + few significant NPCs means the player is one of the handful of consequential
people, and individual acts visibly move the settlement:

1. **Subsist** — secure food/water/shelter.
2. **Throw off surplus** — work land/herd/craft beyond need → stored food, livestock, tools
   (proto-capital).
3. **Specialize on a skill** — become the village's best toolmaker / healer / trader (the GDD
   skill list already includes agriculture, culinary, construction…); barter the surplus.
4. **Organize labor** — induce others to work your plot/herd for a share of the harvest: the
   **first employment relationship and first `NPCBusiness` — emergent, not seeded.**
5. **Convert surplus into influence** — patron (redistribute for loyalty → influence_network),
   elder/chief (governance), or warlord (force → criminal path).
6. **Scale with population & surplus** — household → workshop → firm; barter → market; chief →
   state. The modern "start/acquire/inherit a business" game is the *far end* of this arc.

## 7. Mapping to MHG bands & per-band deliverable
This note specifies the **Economic regime + Entity genesis + Content** rows of MHG's
per-band deliverable for Bands 1–2:

- **Band 1 (Subsistence):** Layer-1 jobs; production model (B) via bridge (A); household/
  commons; no money/markets/firms; population at carrying capacity. Content: era-0 input-free
  food + handicraft recipes/goods.
- **Band 2 (Agrarian + market formation):** surplus → Layer-2 specialists → barter → proto-
  markets → money emergence; first firms (facilities founded from surplus); first wage labor;
  early chiefs → states (legitimacy turns on). Content: trade goods, money, early crafts.

MHG's recommended sequence still holds: prove the **history-gen engine** on existing modern
content (Band 5) first; then author bands outward. This note is the **content/regime spec**
those Band 1–2 milestones will implement.

## 8. Open decisions (for review)
1. **Roles:** extend `NPCRole` with primitive roles, or introduce an era-tagged, data-driven
   `occupation` attribute? (Recommend the latter.)
2. **Production unit:** commit to model (B) (commons subsistence distinct from facilities),
   with (A) as the bridge? Or stay facility-only and accept "facility = primitive activity"?
3. **Capital representation:** generalize `capital`, or represent dawn-wealth as goods
   inventory (stored food/livestock/tools)?
4. **Module gating:** is per-band on/off switching of price_engine / labor_market / finance /
   political_cycle acceptable architecture, and where do the thresholds live (config vs. era)?
5. **Scope/Tier:** confirm against the Feature Tier List whether Bands 0–2 are V1 or post-V1
   (MHG §6 flags the same question for the broader effort).
