# Design: Regime-Differentiated Unrest Response + National Roll-up (V1)

Status: design approved 2026-06-10 (scope decisions below). Implemented in slices.

## Problem

The emergence baseline shows mass unemployment → grievance pins at 1.0 →
province conditions saturate at extremes for a decade with **no state response,
no spread, no national consequence**. Findings:
- `community_response` runs the GDD §14.2 escalation ladder but is **regime-blind**
  (`government_type` is read in exactly one place — `political_cycle` gating
  whether elections occur).
- There is **no state suppression / crackdown** mechanic (Tiananmen/Kent State is
  unmodeled); the GDD's "suppress" is only a *player* intervention.
- Unrest is **province-siloed**: no national legitimacy/stability that aggregates
  provincial grievance, so six provinces at max grievance produce no national crisis.

## Scope decisions (owner: human)

- **Regime response: full branch.** The state's reaction to unrest differs by
  `government_type`: Democracy/Federation → electoral turnover + policy
  concession; Autocracy → suppression with backlash; FailedState → fragmentation.
- **Spatial: province grievance + national roll-up.** Add a national legitimacy
  that aggregates provincial conditions and drives the regime response. **No**
  cross-province contagion and **no** cross-border / world reaction in V1 (EX).
- Nation-level revolution / civil war / multi-nation dynamics remain **EX** per
  the Feature Tier List ("War as simulation failure mode | EX").

## Architecture

Owner module: **`political_cycle`** (sequential, `runs_after: community_response`,
so it sees every province's grievance/stability each tick). It gains the national
roll-up and the regime-branched response. `community_response` stays the
province-level escalation engine.

New WorldState-visible national state on `NationPoliticalCycleState`:
- `float national_legitimacy` (0–1) — does the population accept the government's
  right to rule. **Derived per tick** (recomputed before any consumer reads it),
  so transient/not-serialized, like `NPCBusiness.net_signal`.

New delta channel: `NationDelta { nation_id; optional<float> legitimacy_update;
optional<float> approval_delta; }` + `apply_nation_deltas`. The regime-response
branches reuse this channel plus the existing `RegionDelta` (grievance/stability)
and `political_cycle` election machinery.

### National legitimacy roll-up (Slice 1)

Population-weighted aggregate across the nation's provinces, EMA-smoothed:

```
per province p (weight = population):
    raw_p = w_trust*inst_trust + w_stab*stability - w_griev*grievance - w_unemp*unemployment
legitimacy_target = clamp01( sum(weight*raw_p) / sum(weight) )
national_legitimacy = ema(national_legitimacy, legitimacy_target, alpha)
```

So sustained mass grievance/unemployment craters legitimacy — the national "the
country is in crisis" signal that triggers the response.

### Regime-branched response (Slice 2), keyed on `government_type` when
`national_legitimacy` falls below a crisis threshold (and/or provincial grievance
is high):

- **Democracy / Federation — accountability.** Incumbent `national_approval`
  craters with legitimacy; the next election (existing `political_cycle` pipeline)
  turns the incumbent out. A *responsive* government also concedes: emit
  `RegionDelta` grievance relief in the worst provinces (policy addresses the
  grievance), which lets grievance relax from its peak. Resolution = ballot +
  concession. Legitimacy recovers if conditions improve.

- **Autocracy — suppression (the Tiananmen path).** No election valve. When
  unrest crosses threshold, a **suppression event**: cut province grievance and
  knock the community response_stage down short-term (force works *now*), but
  (a) raise a grievance *floor* and drop `national_legitimacy` further (martyrs /
  illegitimacy), and (b) accumulate a repression count. Repeated/escalating
  suppression with legitimacy already low → **legitimacy collapse**, modeled in
  V1 as `government_type → FailedState` (regime falls); full civil war is EX.
  Suppression also emits an evidence/consequence trail (state violence is
  observable).

- **FailedState — fragmentation.** No suppression capacity, no elections. The
  state recedes: `criminal_dominance_index` rises, stability stays floored,
  grievance neither resolved nor suppressed. The province is effectively
  ungoverned.

## Validation (emergence ratchets)

- Slice 1 (alive): national legitimacy reacts — craters under the mass-grievance
  baseline (proves the roll-up is wired).
- Slice 1 (ratchet): a legitimacy crisis gets *resolved* — legitimacy does not
  simply crater and pin (the state responds somehow). Fails until Slice 2.
- Slice 2 (ratchets, per regime — tested on worlds forced to each
  `government_type`):
  - Democracy: a legitimacy crisis produces incumbent turnover and grievance
    relaxes from its peak.
  - Autocracy: a legitimacy crisis produces suppression (grievance/stage knocked
    down) and, if sustained, regime change (→ FailedState); suppression leaves an
    evidence/consequence trail.
  - FailedState: criminal dominance rises under sustained crisis.

## Dependency note

The escalation ladder is currently jammed at stage 4 by a separate keystone bug
(year-1 NPC-inactivity → cohesion→0 → collective-action stages gated shut, see
emergence_baseline_2026-06-10.md). The regime response is triggered by
**national legitimacy** (driven directly by grievance/unemployment), **not** by
the community reaching stage 6, so it fires independently of that bug — and gives
the pinned-grievance state a discharge it currently lacks. Fixing the cohesion
keystone is tracked separately and makes the *organized-opposition* path (stage 6)
reachable, which feeds legitimacy too.
