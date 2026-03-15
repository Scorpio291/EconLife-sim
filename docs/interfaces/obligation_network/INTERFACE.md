# Module: obligation_network

## Purpose
Processes the obligation network each tick: evaluates escalation for open and overdue `ObligationNode` entries, transitions obligation status through the escalation state machine (open -> escalated -> critical -> hostile), schedules scene cards for demand escalation, and triggers hostile creditor actions when obligations reach critical thresholds. The module also handles partial payment processing, renegotiation outcomes, and new obligation creation from favor events.

In V1, `WorldState.obligation_network` tracks only `ObligationNode` entries where the player is creditor or debtor. NPC-to-NPC obligations are modeled implicitly through NPC motivation weights and relationship trust values -- they do not generate `ObligationNode` entries in V1. The full NPC-to-NPC obligation graph is expansion scope. The obligation escalation path is not scripted -- it emerges from creditor motivation x time x the player's current visible resources.

## Inputs (from WorldState)
- `obligation_network` -- all open `ObligationNode` records; iterated per tick for escalation evaluation
- `significant_npcs` -- creditor NPC motivation model (`MotivationVector`), `risk_tolerance`, relationship graph; read to determine escalation rate and hostile action selection
- `player` -- `visible_net_worth` used in `player_wealth_factor` calculation; `obligation_node_ids` for player-side tracking; reputation scores for context-gating hostile actions
- `current_tick` -- used for `time_overdue` calculation against `ObligationNode.deadline_tick`
- `world_seed` -- used by `DeterministicRNG` for hostile action probability rolls

## Outputs (to DeltaBuffer)
- `ObligationNode.current_demand` -- updated demand value after per-tick escalation growth
- `ObligationNode.status` -- status transitions: `open` -> `escalated`, `escalated` -> `critical`, `critical` -> `hostile`
- `ObligationNode.history` -- new `EscalationStep` entries appended on status transitions
- Scene card schedule entries -- `obligation_escalation_demand` scene card queued on transition to `escalated`
- Consequence entries -- `ConsequenceType::obligation_creditor_unilateral_action` queued on transition to `critical`
- `NPCDelta.trust_delta` -- trust erosion from overdue obligations: `trust_delta = -0.001/tick` per overdue open obligation
- New `ObligationNode` entries -- created when favor events (from scene card outcomes or NPC behavior actions) generate new debts

## Preconditions
- NPC behavior module (tick step 7) has completed; NPC motivation weights and risk tolerance are settled for this tick.
- All `ObligationNode.creditor_npc_id` values reference valid, active NPCs. Obligations with dead or fled creditors are resolved via estate/succession rules before this module runs.
- Player `visible_net_worth` is computed and current (updated by financial module).
- Scene card system is available for scheduling (scene cards are queued, not immediately delivered).

## Postconditions
- Every `ObligationNode` with `status in {open, escalated, critical}` has been evaluated exactly once this tick.
- `current_demand` for overdue obligations has grown by `demand_growth_per_tick` (unless deadline has not yet passed).
- Status transitions are atomic: at most one status change per obligation per tick.
- Scene cards and consequences scheduled by this module are in the deferred work queue for future delivery.
- Trust erosion deltas have been written for all overdue obligations.

## Invariants
- Demand growth formula: `demand_growth_per_tick = creditor_urgency * config.escalation_rate_base * (1.0 + player_wealth_factor)`.
- `creditor_urgency = creditor.motivation.weights[dominant_motivation]` where `dominant_motivation = creditor.motivation.highest_weight_type()`.
- `player_wealth_factor = min(player.visible_net_worth / config.wealth_reference_scale, config.max_wealth_factor)` (default: wealth_reference_scale = 1,000,000; max_wealth_factor = 2.0).
- Escalation threshold: `node.current_demand > node.original_value * config.escalation_threshold` (default 1.5x) -> status becomes `escalated`.
- Critical threshold: `node.current_demand > node.original_value * config.critical_threshold` (default 3.0x) -> status becomes `critical`.
- Hostile action trigger: `node.status == critical AND creditor.risk_tolerance > config.hostile_action_threshold` (default 0.7) -> status becomes `hostile`.
- Hostile action context gates: `report_to_law_enforcement`, `expose_player`, `public_accusation` are always available. `contact_rival_criminal_org` requires `player.reputation.street > 0.0`. `contact_rival_competitor` requires player to have active `NPCBusiness` operations.
- Partial payment reduces `current_demand` by the payment fraction of `original_value`. Renegotiation resets `current_demand` to agreed value and resets `deadline_tick`, returning status to `open`.
- FavorType determines obligation value: 16 favor types (financial_loan through whistleblower_silenced) each carry distinct `original_value` scaling.
- Overdue obligations erode trust: `trust_delta = -0.001/tick` applied to the creditor-player relationship.
- Floating-point accumulations use canonical sort order (`obligation_node_id` ascending) for deterministic processing.
- Same seed + same inputs = identical obligation state regardless of core count.
- All random draws go through `DeterministicRNG`.

## Failure Modes
- Creditor NPC is dead or fled: obligation status frozen at current state; no further escalation. If creditor has a designated successor (e.g., criminal org leadership), obligation transfers. Otherwise, obligation resolves as `forgiven` after `config.orphan_obligation_timeout_ticks` (default: 180 ticks).
- Player `visible_net_worth` is negative: `player_wealth_factor` = 0.0 (min clamp); escalation proceeds at minimum rate.
- NaN in demand growth calculation: clamp `current_demand` to `node.original_value * config.critical_threshold * 2.0` (maximum reasonable demand), log diagnostic.
- Hostile action candidate set is empty after context gating: creditor defaults to `expose_player` (always available).

## Performance Contract
- Province-parallel execution: obligation nodes are processed per province based on creditor NPC's `home_province_id`.
- Target: < 10ms total for ~200 active obligation nodes across 6 provinces on 6 cores.
- Per-obligation evaluation: < 0.05ms average.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["npc_behavior"]
- runs_before: ["trust_updates"]

## Test Scenarios
- `test_overdue_obligation_demand_grows`: An obligation with `deadline_tick` in the past. Verify `current_demand` increases by `demand_growth_per_tick` each tick according to the escalation formula.
- `test_not_yet_overdue_no_escalation`: An obligation with `deadline_tick` in the future. Verify `current_demand` does not change.
- `test_status_transitions_open_to_escalated`: An obligation whose `current_demand` crosses `1.5 * original_value`. Verify status transitions to `escalated` and an `obligation_escalation_demand` scene card is scheduled.
- `test_status_transitions_escalated_to_critical`: An obligation whose `current_demand` crosses `3.0 * original_value`. Verify status transitions to `critical` and a `ConsequenceType::obligation_creditor_unilateral_action` is queued.
- `test_hostile_action_risk_tolerance_gated`: A critical obligation where creditor `risk_tolerance = 0.5` (below threshold 0.7). Verify status remains `critical` and no hostile action triggers.
- `test_hostile_action_context_gate_criminal`: A hostile creditor selects `contact_rival_criminal_org`. Verify this option is only available when `player.reputation.street > 0.0`.
- `test_wealthy_player_faces_steeper_demands`: Two scenarios: player with 100k net worth vs 2M net worth. Same obligation, same creditor. Verify the wealthier player's demand grows faster due to `player_wealth_factor`.
- `test_partial_payment_reduces_demand`: Player makes a partial payment of 30% of `original_value`. Verify `current_demand` decreases by that fraction and status potentially reverts.
- `test_renegotiation_resets_deadline`: Player successfully renegotiates via scene card. Verify `current_demand` is set to new agreed value, `deadline_tick` is reset, and status returns to `open`.
- `test_trust_erosion_from_overdue_obligation`: An overdue obligation persists for 100 ticks. Verify creditor-player trust has decreased by 0.1 (100 * -0.001/tick).
- `test_dead_creditor_freezes_obligation`: Creditor NPC dies. Verify obligation status is frozen and no further escalation occurs until succession or timeout.
- `test_province_parallel_determinism`: Run 50 ticks of obligation processing with 6 provinces on 1 core and 6 cores. Verify bit-identical obligation state.
