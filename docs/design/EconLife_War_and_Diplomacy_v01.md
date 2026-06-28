# EconLife — War & Diplomacy: rational polities, not scripted wars (v01)

Status: **proposed** (2026-06-25). Design; no behavioral change yet. The grounded
model for war, treaties, alliances, politics, and backstabbing — the realistic
substance behind M6 ("war") of the Medieval Band
(`EconLife_Medieval_Band_Expansion_v01.md` §5.5). Nation-level; generalizes across
all eras (feudal lords → kingdoms → nation-states), and its reach obeys the same
logistics law as the ox-cart and the photon
(`EconLife_Logistics_and_Political_Scale_v01.md`).

---

## 0. Principle — war is an outcome, never a script

War is not a random event and not a hand-authored script. It is what rational
polities *do* when competing for conserved grain/territory under two hard
constraints — **physical reach** (the ox-cart limit: you can only fight what you can
supply an army to) and the **balance of power** — with **reputation** as the currency
that makes a promise worth anything. Treaties, alliances, the politics of who fights
whom, and backstabbing are all EMERGENT from expected-value decisions over that
landscape. Nothing here is rolled on a table or written into a quest.

Reality balance falls out of three forces, not a difficulty knob:
- **War is costly** (casualties, grain burned/diverted, war-weariness) → peace is the
  default equilibrium.
- **The ox-cart keeps war local** (force can't be projected past the haulage radius)
  → sprawling conquest needs water/roads, exactly as §3.5 says → most wars are
  border wars.
- **The balance of power self-corrects** (a rising hegemon is ganged up on) →
  conquest is bounded, not runaway.

Result: long peace punctuated by wars of opportunity, shifting alliances, occasional
dramatic betrayal, and the rise/fall/balancing of powers — and it feeds the
population curve (war dips, §5.5) and the spectrum (war-torn worlds).

---

## 1. Polities and their grounded attributes

A **polity** is a nation (`WorldState.nations`); at the dawn the feudal **lords** (§5)
are proto-polities (a lord + manor + garrison over their provinces). Each has, all
computed from the grounded economy/geography — not hand-set:

- **Military power** = surplus-fed garrison (M5 tithe → garrison) + a population levy,
  scaled by tech tier. Power *is* concentrated surplus: a rich, high-tithe polity
  fields a bigger army. No abstract "strength stat".
- **Reach** = the ox-cart/haulage radius (§3.5): the set of polities it can project
  force against, and how much force survives the march. Landlocked/heavy-gravity →
  short reach, local wars; river/coastal → long reach, able to campaign far.
- **Relations** `diplomatic_relations[other] ∈ [-1,1]` (the existing, currently-empty
  Nation field) — evolves from interactions: war and betrayal drive it down, trade
  and kept treaties up.
- **Reputation (faith)** — a polity's track record of keeping its word, derived from
  its history of honoured vs broken pacts. This is the price tag on betrayal.

---

## 2. The war/peace decision (politics = expected value)

Each polity periodically (a "diplomatic tick", quarterly-ish) evaluates each
**reachable** neighbour and picks the action that maximises expected value:

```
EV(attack t) =  P(win) * value(seizable grain + territory of t)
              − P(lose) * cost(own casualties + grain burned)
              − reputation_cost(if a treaty/alliance with t is broken)
              − war_weariness
```
- `P(win)` from own power + committed allies vs t's power + t's allies.
- `value(...)` is t's conserved grain surplus + the productive territory (provinces)
  at stake — so polities attack the **weak, grain-rich, and reachable**, which is
  exactly how history reads.
- Attack only when `EV(attack) > EV(stay at peace)`. A balanced neighbourhood yields
  peace; a clear power+prize+reach advantage yields war.

This reuses the same EV-decision machinery NPCs/businesses already use — war is just
a polity-scale rational choice.

---

## 3. Alliances — the balance of power, emergent

When a polity faces a neighbour whose power (with *its* allies) exceeds the polity's
own defence, `EV(ally against the threat) > EV(stand alone)` → it seeks allies; those
under the same shadow accept. Allied power pools for mutual defence (and, by treaty,
shared offence). Consequences that emerge, not scripted:
- A **rising hegemon triggers a balancing coalition** — the others ally *against* the
  strongest. Conquest is self-limiting.
- Alliances **dissolve when the threat is gone** (the EV that formed them evaporates).
- Small polities survive by **bandwagoning or balancing**; great powers by **deterrence**.

## 4. Treaties — the equilibrium of a balanced board

Peace / non-aggression / trade pacts. A treaty *holds while* `EV(peace) ≥ EV(attack)`
for **both** sides; while active it lowers mutual conflict probability and raises
relations + trade flow. Treaties are not promises the engine enforces — they are
equilibria the power landscape sustains. When the landscape shifts (a neighbour
weakens, a prize appears), the treaty is back under EV pressure.

## 5. Backstabbing — rational betrayal with a reputation economy

A polity breaks a treaty/alliance when
```
EV(betray)  =  surprise_bonus * value(off-guard ally's grain/territory)
             − value(the relationship/its future)
             − reputation_cost
```
exceeds keeping faith. Betrayal grants a **one-time surprise bonus** (the victim is
undefended — its garrison faced elsewhere) but a **lasting reputation hit**: a known
backstabber's future `EV(ally with them)` is discounted by everyone (high betrayal
risk), so it **cannot form alliances** and gets **ganged up on**. So:
- Faith is *mostly* rational (the reputation economy punishes betrayal).
- Betrayal is **occasional and opportunistic** — taken by a polity already strong
  enough not to need future allies, against a rich, vulnerable partner.
- A board of serial backstabbers collapses into low-trust, everyone-for-themselves
  warfare (a valid, grim spectrum outcome — a low-faith world).

No "treachery die". Betrayal is the EV calculation with reputation priced in.

---

## 6. Conserved outcomes (the link to M6 war mortality)

When war resolves (ties to §5.5):
- **Grain/territory seized** → moves to the victor (conserved — to the winner, not to
  nothing). Provinces may change polity; a crushed polity is absorbed or fragments.
- **Casualties** → drawn from cohorts on both sides, combatants (the levy/garrison)
  first — a real population pulse (the curve's war dips).
- **Grain burned / diverted** to the campaign → a conserved loss (to destruction /
  the army's mouths, like the oxen of §3.5).
- **Reputation/relations** updated for all observers (war, betrayal, kept faith).

Nothing minted or vanished — people, grain, and territory only move or are consumed.

---

## 7. Architecture & hooks

- **Relations:** fill the existing `Nation.diplomatic_relations` map (and an analogous
  per-lord map at the dawn). A new lightweight per-pair state for active treaties /
  alliances (status + terms), and a per-polity **reputation/faith** scalar.
- **Power & reach:** power from the M5 garrison/tithe + levy; reach from the §3.5
  haulage layer (`grain_logistics`, generalised to "force projection" — same
  link→deliverable cost, applied to an army instead of grain).
- **Engine:** generalise the **criminal territorial-conflict engine** (already
  exposed per-province via M2's `territorial_conflict_stage` work) from org rivalry to
  polity war — it already has conflict initiation, escalation, and resolution; war
  adds the EV decision, alliances/treaties, casualties + seizure.
- **Politics:** intertwine with `political_cycle` (a costly lost war hits legitimacy →
  unrest/turnover; a glorious win raises it) — closes the war↔domestic-politics loop.
- **Cohort scale:** during history-gen this is **aggregate** (polity power, relations,
  outcomes as numbers); individual battles/leaders materialise at **player entry**.

---

## 8. Decisions

- **W1:** dawn polity unit — do feudal lords (§5) act as polities directly, or only
  nations (era-gated, so war begins when states form)? Lean: lords are proto-polities
  so feudal war exists, generalising to nations.
- **W2:** reach model — reuse `grain_logistics` delivered-fraction for *force
  projection* (army supply eaten en route), or a separate but parallel cost? Lean:
  reuse (one logistics law).
- **W3:** reputation representation — a per-polity faith scalar + the relations map, or
  reuse the trust/social-capital systems? Lean: a dedicated polity-faith scalar
  (distinct from NPC trust).
- **W4:** territory transfer granularity — whole provinces change polity on decisive
  loss, or gradual dominance shift (like the criminal `dominance_by_province`)? Lean:
  gradual dominance, with absorption at a threshold.
- **W5:** calibration target — what peace:war ratio and conquest bound read as
  "realistic" on the spectrum (mostly-peace with periodic local wars; rare
  hegemonic surges that get balanced)?

---

## 9. One-paragraph summary

War is rational polities competing for conserved grain under physical reach (the
ox-cart) and the balance of power, with reputation pricing every broken promise.
Attack when the expected value of seizing a weak, rich, reachable neighbour beats
peace; ally when a hegemon threatens (the balance of power, self-correcting); sign
treaties that hold while peace pays; betray when the prize off-guard exceeds the
relationship plus the reputation hit — and become a pariah if you do it too often.
Grain, people, and territory only ever move or are consumed. The outcome is a
realistic history — long peace, local wars of opportunity, shifting coalitions, the
occasional dramatic betrayal, powers rising and being balanced — that carves the
population curve and sorts the world spectrum into the peaceful, the war-torn, and the
faithless, none of it scripted.
