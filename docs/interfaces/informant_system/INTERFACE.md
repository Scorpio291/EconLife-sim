# Module: informant_system

> **Reconciliation note (2026-05-31):** This spec was rewritten to match the
> shipped implementation. The original spec described a much larger feature
> set (player countermeasures, InvestigatorMeter fill, obligation deltas,
> knowledge-type-specific token mapping, an `informant_records` WorldState
> field, and a different InformantRecord shape / status enum) that the code
> does not implement. The aspirational behavior is preserved verbatim under
> [Planned / not yet implemented](#planned--not-yet-implemented) so the design
> intent is not lost; everything above that section reflects current behavior.

## Purpose
Recruits imprisoned criminal NPCs as informants and evaluates them for
cooperation each tick. Recruitment is driven by the `NPCStatus::imprisoned`
signal (set upstream by legal_process): each tick the module self-seeds an
`InformantRecord` for any imprisoned criminal-role NPC
(`criminal_operator` / `criminal_enforcer` / `fixer`) it is not already
tracking. For each not-yet-cooperating record whose NPC is still imprisoned it
computes a flip probability from NPC state (risk tolerance, trust toward the
player, mutual-incrimination obligations, compartmentalization) and rolls for
cooperation. On a successful flip the informant's known evidence is converted
into testimonial evidence tokens. There is no separate loyalty field —
defection probability emerges from existing NPC fields per GDD §12.2.

Not province-parallel: informant records and the player relationships they
reference are cross-province.

## Recruitment model
- The module owns `records_` (a private `std::vector<InformantRecord>`,
  persisted at schema v7). It is **not** a WorldState field.
- At the top of `execute()` the module scans `significant_npcs` in id order and
  creates a `not_cooperating` record for each imprisoned criminal-role NPC not
  already tracked (`arrest_tick = current_tick`,
  `compartmentalization_level = 0` for operators, `1` for enforcers/fixers).
  Deduped by `npc_id`; deterministic.
- This is self-seeding from a WorldState fact rather than a cross-module
  producer/seed-queue, because the flip lifecycle already gates on
  `NPCStatus::imprisoned`.

## Inputs (from WorldState)
- `significant_npcs` — NPC records: `role`, `status` (recruitment requires
  `imprisoned`), `risk_tolerance`, `relationships` (trust with the player),
  `known_evidence` (`KnowledgeEntry` records with `confidence`),
  `current_province_id`.
- `player` — to identify the player relationship used for the trust factor.
- `obligation_network` — `ObligationNode` records; active nodes with
  `favor_type == FavorType::criminal_cooperation` referencing the NPC count as
  mutual-incrimination suppression.
- `current_tick` — cooperation timing.
- `world_seed` — tick RNG seed (`world_seed ^ current_tick`, forked per NPC).
- `config.informant.*` (see `InformantConfig`):
  - `base_flip_rate` — default **0.005**; per-tick cooperation base under arrest pressure
  - `max_flip_probability` — default 0.20; per-tick cap
  - `risk_factor_scale` — default 0.30; risk-tolerance contribution
  - `trust_factor_scale` — default 0.25; trust-deficit contribution
  - `incrimination_suppression` — default 0.08; per mutual-incrimination obligation reduction
  - `compartment_bonus_per_level` — default 0.05; per compartmentalization level reduction
  - `pay_silence_cost` — default 50000; used to scale the per-tick capital cost proxies
  - `violence_multiplier` — default 3.0 (reserved; see Planned)

## Outputs (to DeltaBuffer)
- `NPCDelta.capital_delta` — for each `cooperating` record, a small per-tick
  cost `-pay_silence_cost * 0.001` (witness-protection / handling proxy).
- On a successful flip (`not_cooperating -> cooperating`):
  - one `EvidenceDelta.new_token` per `known_evidence` entry: a `testimonial`
    `EvidenceToken` (origin = informant NPC, subject = the knowledge subject,
    actionability 0.50, decay 0.003, current tick, informant's province).
  - `NPCDelta.capital_delta` of `-pay_silence_cost * 0.10` on the informant.

## Preconditions
- investigator_engine and legal_process have run this tick (the module
  `runs_after` both). Imprisonment status is current.
- Obligation network is current for mutual-incrimination evaluation.
- NPC knowledge maps are current from preceding tick modules.

## Postconditions
- Every imprisoned criminal-role NPC is tracked by exactly one record.
- Every `not_cooperating` record whose NPC is imprisoned had `flip_probability`
  recomputed and a cooperation roll executed this tick.
- Records whose status is anything other than `not_cooperating`/`cooperating`
  are skipped (no recompute, no roll).

## Invariants
- Flip probability:
  `flip_probability = base_flip_rate + (1 - risk_tolerance) * risk_factor_scale
  + (1 - trust) * trust_factor_scale - mutual_count * incrimination_suppression
  - compartmentalization_level * compartment_bonus_per_level`,
  clamped to `[0.0, max_flip_probability]`.
- Risk factor: `(1 - risk_tolerance) * risk_factor_scale`. Low risk tolerance increases flip rate.
- Trust factor: `(1 - trust) * trust_factor_scale`. Zero trust maximizes the contribution.
- Mutual incrimination: each active `criminal_cooperation` obligation node
  referencing the NPC reduces flip probability by `incrimination_suppression`.
- Compartmentalization: peripheral roles seed a higher
  `compartmentalization_level`, reducing flip probability by
  `compartment_bonus_per_level` per level.
- `InformantStatus`: `not_cooperating=0, cooperating=1, silenced=2,
  eliminated=3, relocated=4`.
- `InformantRecord` fields: `npc_id, status, flip_probability, base_flip_rate,
  arrest_tick, cooperation_start_tick, compartmentalization_level`.
- Same seed + same inputs = identical output. All random draws (the flip roll)
  go through `DeterministicRNG`, forked per NPC by `npc_id`.

## Failure Modes
- NPC no longer found or no longer imprisoned: the record is skipped this tick
  (no roll), but retained for a future tick.
- NaN / negative flip probability from an edge case: clamped to 0.0.

## Performance Contract
- Sequential execution (not province-parallel).
- Target: < 5 ms total (~20–80 records typical). Must stay within the tick's
  200 ms budget share.

## Dependencies
- `runs_after`: `["investigator_engine", "legal_process"]`
- `runs_before`: none

## Test Scenarios (current behavior)
- Static formula helpers: `compute_risk_factor`, `compute_trust_factor`,
  `compute_incrimination_suppression`, `compute_compartmentalization_bonus`,
  `compute_flip_probability` (including the max-probability cap and
  non-negativity).
- Self-seeding: `execute()` creates a record for an imprisoned criminal NPC and
  is idempotent across ticks; non-criminal or non-imprisoned NPCs are not
  seeded.
- Cooperation: a seeded record with high propensity flips and emits a
  testimonial `EvidenceDelta` from disclosed knowledge.

---

## Recently implemented (was "planned")

The following behavior was originally specced as backlog and has since been
built — it is now part of the contract:

- **Player countermeasures** (`PlayerCountermeasure`: `pay_silence`,
  `threaten_silence`, `relocate_witness`, `eliminate`): status transitions to
  `silenced`/`relocated`/`eliminated`, `PlayerDelta` wealth deduction on
  pay_silence, a `whistleblower_silenced` obligation node, `risk_tolerance`
  increase and negative memory on threaten_silence, NPC death on eliminate,
  flip drop to `base * 0.2` on relocate, and the corresponding countermeasure
  evidence trail. Drained from `WorldState.pending_informant_countermeasures`.
- **Knowledge-type-specific token mapping** (`identity_link -> financial`,
  `evidence_token -> documentary`, `activity`/`relationship -> testimonial`)
  with a `confidence > disclosure_confidence_threshold` (0.40) disclosure
  filter and `actionability = confidence * cooperation_actionability_scale`.
- **InvestigatorMeter fill** on disclosure: accumulated
  `actionability * meter_fill_per_disclosure` is delivered to the lead
  law-enforcement NPC in the informant's province (lowest active LE id),
  targeting the most-incriminated subject, via
  `NPCDelta.investigator_meter_fill_delta` + `investigator_meter_target`.
  `drain_deferred_work` then stages/escalates the meter and opens a legal case
  at `raid_imminent`.
- Config knobs `disclosure_confidence_threshold`,
  `cooperation_actionability_scale`, `meter_fill_per_disclosure` now exist on
  `InformantConfig`.

## Planned / not yet implemented

Still backlog (design intent, not the contract):

- The `personnel_violence_multiplier` fill-rate spike on `eliminate`.
- **`arresting_le_npc_id` / `disclosed_knowledge_ids`** record fields and
  delivery of tokens to a *specific* arresting LE NPC. The current
  implementation targets the province's lead LE NPC rather than a tracked
  arresting officer.
