# Unrest Response Calibration — baseline from real & fictional conflicts

Purpose: set the slice-2 regime-response dials (in `PoliticalCycleConfig`) from
how states have actually responded to mass unrest, so the numbers encode a
dynamic, not a guess. All values are tunable config; the observer
(`emergence_observe`) is the tuning loop.

## The dynamics we're calibrating

From the historical/fictional record, four robust patterns recur:

1. **Suppression works in the short term.** Force clears the streets and knocks
   the visible escalation back — *every* time it's applied with will and capacity.
   - Tiananmen Square (PRC, 1989): the army cleared the square in a night; the
     regime survived 35+ years.
   - Hong Kong (2019–20), Belarus (2020), Iran (2009 Green, 2022 Mahsa Amini):
     sustained crackdowns ended the protests on the street.
   - *1984* (fiction): the high-surveillance, high-coercion pole — the boot
     stamps forever; suppression never fails.

2. **But suppression raises a grievance *floor* — the martyr/backlash effect.**
   Visible state violence radicalizes and is *remembered*; the rebound often
   exceeds the dispersal.
   - Kent State (US, 1970): 4 students killed → the largest student strike in US
     history (~4M students, 900+ campuses). Tiny crackdown, enormous backlash.
   - Sharpeville (1960) & Soweto (1976) (Apartheid SA): massacres radicalized a
     generation and drove decades of sanctions.
   - Bloody Sunday (Tsarist Russia, 1905): one volley shattered the Tsar's
     legitimacy and triggered the 1905 revolution.
   - *Star Wars*: "The more you tighten your grip, Tarkin, the more star systems
     will slip through your fingers"; Alderaan galvanizes the Rebellion; *Andor*'s
     Ferrix funeral. *The Hunger Games*: Rue's death → District 11 rising; the
     Mockingjay. *Les Misérables*: the June Rebellion is crushed but seeds.

3. **Whether the regime survives depends on coercive capacity + elite loyalty,
   and collapse comes fast once those go.**
   - Survives: PRC, North Korea, Belarus (Russian patron), Pinochet's Chile
     (1973–88) — high capacity, suppression holds for years/decades.
   - Collapses fast: East Germany/Leipzig (Oct 1989, the regime declined to
     shoot → Wall fell within a month); Romania (Dec 1989, Ceaușescu ordered a
     crackdown, the army defected, he was executed ~1 week later); Iran 1978–79
     (escalating crackdowns *accelerated* the revolution); Tunisia 2011 (military
     refused to fire → weeks to collapse). *V for Vendetta*: when fear finally
     breaks, the regime falls at once.
   - The trigger for collapse is **sustained repression while already
     illegitimate** — the apparatus stops obeying.

4. **Regime *type* sets the discharge channel.**
   - **Democracy/Federation** discharges at the ballot + concession: incumbents
     are voted out after crises, and policy concessions relieve grievance (US
     1932 New Deal; Chile's 1988 plebiscite; Egypt 2011 — the military sided with
     the street → turnover). The valve is accountability, not force.
   - **Autocracy** has no ballot valve, so it reaches for suppression (pattern
     1–3): short-term calm, rising grievance floor, legitimacy bleed, eventual
     collapse-to-chaos if pushed far enough.
   - **Failed state** has neither ballot nor coercive capacity: the state
     recedes and criminal/warlord economies fill the vacuum (Somalia, Libya post-
     2011, Syria's ungoverned zones, Myanmar post-2021).

## Calibrated defaults

Legitimacy and grievance are both [0,1]. Responses fire on a **monthly cadence**
(30 ticks) — crackdowns, elections, and concessions are discrete events, not
per-tick. A nation is "in crisis" when `national_legitimacy < crisis_threshold`.

| Dial | Default | Grounding |
|---|---|---|
| `legitimacy_crisis_threshold` | **0.30** | A clear majority has withdrawn consent (Bloody-Sunday / Arab-Spring inflection). Below this the state must act. |
| **Autocracy — suppression** | | |
| `suppression_response_stage_cut` | **2** | A crackdown knocks community escalation back ~2 rungs — clears the streets (Tiananmen, HK, Belarus). |
| `suppression_grievance_immediate` | **−0.15** | Short-term dispersal of expressed grievance. |
| `suppression_grievance_floor_rise` | **+0.06 / crackdown** | The martyr ratchet (Kent State, Sharpeville, Ferrix, Rue). Repeated suppression pushes the floor *above* its start — "tighten your grip." Net long-run grievance rises despite short-term cuts. |
| `suppression_legitimacy_hit` | **−0.05 / crackdown** | Legitimacy bleed at home and abroad (Tiananmen sanctions, Sharpeville, Bloody Sunday). |
| `collapse_legitimacy_floor` | **0.08** | Only the most illegitimate regimes fall; high-capacity autocracies (China, NK, *1984*) keep legitimacy off the absolute floor and survive. |
| `collapse_repression_count` | **8** | ~8 months of monthly crackdowns while fully illegitimate → the apparatus defects → regime falls to FailedState (Romania, Iran-79, Syria trajectory). Survivors never accumulate this *and* stay below the floor. |
| **Democracy/Federation — accountability** | | |
| `crisis_approval_hit` | **−0.08 / month** | Incumbent approval craters in crisis → turnover at the next election (1932, Egypt-2011, Kent-State→electoral pressure). |
| `concession_grievance_relief` | **−0.10 / month** in worst provinces | Policy addresses the grievance (New Deal, welfare concessions, Chile plebiscite). Lets grievance relax → legitimacy recovers. |
| `concession_province_count` | **2** | Concessions target the worst-off provinces, not everywhere at once. |
| **FailedState — fragmentation** | | |
| `failed_state_dominance_rise` | **+0.02 / month** | Criminal/warlord economy fills the vacuum; stability stays floored (Somalia, Libya, Syria, Myanmar). |

## Expected behavior by regime (the per-regime ratchets)

- **Democracy/Federation under mass unemployment:** legitimacy craters, then
  *recovers* from its trough as concessions relieve grievance and the incumbent
  is turned out. Grievance relaxes from its peak. (Flips the "state responds" and
  "grievance relaxes" ratchets.)
- **Autocracy under mass unemployment:** suppression knocks escalation/grievance
  down short-term, but the grievance floor climbs and legitimacy bleeds; under
  sustained crisis the regime eventually collapses to FailedState. (A
  suppression/evidence trail is observable.)
- **FailedState:** no suppression, no turnover; criminal dominance rises over time.

These encode the contrast the design is after: the same mass unemployment yields
a Kent-State-style accountability spiral in a democracy, a Tiananmen-style
suppression-then-attrition path in an autocracy, and warlord drift in a collapsed
state.

## Observed (slice 2, seed 42, same mass-unemployment shock, three regimes)

Forcing each regime through the observer (`emergence_observe`) over 10 years:

- **Autocracy → collapse.** Legitimacy craters to 0 within a year; monthly
  crackdowns accumulate; once legitimacy < `collapse_legitimacy_floor` and
  repression_count ≥ `collapse_repression_count`, the regime falls
  (`government_type → FailedState`) — the Romania/Iran-79 path. Grievance is
  briefly lower than the no-response case (suppression dispersal) before the
  martyr ratchet and the collapse take over.
- **Democracy/Federation → partial resolution.** Concessions bend grievance down
  (year-2 grievance ~0.67 vs autocracy ~0.99) and restore institutional trust;
  this **lifted province stability off the floor** — the "stability does not
  collapse" emergence ratchet flipped from failing to passing and is now a
  regression guard. The incumbent's approval craters (→ turnover at the next
  election). The regime does not collapse — the ballot is the valve.
- **FailedState → fragmentation.** Criminal dominance rises monotonically.

**Caveat — timing realism depends on the upstream keystone.** These runs sit on
top of the year-1 NPC-inactivity bug that forces unemployment→1 and grievance→1
almost immediately, so every regime hits crisis within a year and the autocracy
collapses in ~8 months. Once that keystone is fixed (crisis develops gradually),
the same dials will produce realistic timescales — high-capacity autocracies
surviving for years, democracies absorbing shocks via turnover — without
re-tuning, because the dials encode *rates and thresholds*, not a fixed clock.
The three branches are locked in as deterministic unit tests
(`[political_cycle][unrest]`) that construct controlled crisis worlds, so they
validate the mechanics independently of the substrate bug.
