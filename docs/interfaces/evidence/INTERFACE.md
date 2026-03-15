# Module: evidence

## Purpose
Manages the lifecycle of evidence tokens: creation from observable actions, accumulation by holders and secondary holders, actionability decay via batched DeferredWorkQueue processing, and discovery propagation through the NPC knowledge graph. The evidence system is the foundation of the consequence architecture -- every investigative, legal, media, and political consequence in the simulation ultimately traces back to evidence tokens created by this module or by downstream modules that call into the evidence creation API.

Evidence tokens are created when actors (player, NPC businesses, criminal organizations) perform actions that produce observable signals. Each token has a type (financial, testimonial, documentary, physical), a subject (the actor the evidence is against), a primary holder, and an actionability score that degrades over time. Tokens propagate through the NPC social graph via knowledge sharing -- propagation is instantaneous (information, not physical) per TDD Section 6 physics principle. The player sees tokens they created directly; secondary tokens created by NPCs observing player actions are invisible until discovered through intelligence investment.

## Inputs (from WorldState)
- `evidence_pool` -- all active `EvidenceToken` records in the world; iterated for decay batch processing
- `significant_npcs` -- NPC memory logs, knowledge maps (`known_evidence`, `known_relationships`), relationship graphs; read to determine evidence creation triggers, holder credibility, and propagation targets
- `npc_businesses` -- `criminal_sector` flag, `regulatory_violation_severity`; actions by businesses generate evidence tokens
- `provinces` -- province list for province-parallel dispatch; facility lists per province drive physical evidence creation
- `deferred_work_queue` -- `WorkType::evidence_decay_batch` items fire every 7 ticks per token
- `current_tick` -- used for token creation timestamps and decay batch scheduling
- `world_seed` -- used by `DeterministicRNG` for evidence discovery probability rolls

## Outputs (to DeltaBuffer)
- `EvidenceDelta.new_token` -- new `EvidenceToken` appended to `evidence_pool`; includes `id`, `type`, `subject_npc_id`, `holder_npc_id`, `location_id`, `actionability`, `player_aware`, `created_tick`
- `EvidenceDelta.actionability_update` -- updated `actionability` value for existing tokens after decay batch processing
- `NPCDelta.new_evidence_awareness` -- evidence token id added to NPC's `known_evidence` map when propagation occurs
- `NPCDelta.memory_entry` -- memory entries created when NPCs witness or learn about evidence (type: `observation` or `hearsay`)
- DeferredWorkQueue entries -- reschedule `evidence_decay_batch` at `current_tick + 7` after each batch execution

## Preconditions
- `deferred_work_queue` has been drained (Step 2 complete); any `evidence_decay_batch` items due this tick have been queued for processing.
- NPC behavior module (tick step 7) has completed; NPC actions that generate evidence are finalized.
- All `EvidenceToken.holder_npc_id` values reference valid NPCs (or 0 for orphaned tokens).
- All `EvidenceToken.subject_npc_id` values reference valid actors (player, NPC, or NPC business).

## Postconditions
- Every `evidence_decay_batch` item that fired this tick has had its token's actionability updated and been rescheduled at `current_tick + 7`.
- No token's actionability drops below `config.actionability_floor` (default 0.10); evidence never fully disappears.
- New evidence tokens created this tick have valid `id`, `type`, `subject_npc_id`, `holder_npc_id`, and `created_tick` fields.
- Evidence propagation via `share_evidence` NPC actions has been resolved: receiving NPCs' `known_evidence` maps are updated in the same tick's DeltaBuffer. No transit delay for knowledge propagation.
- `player_aware` is set to `true` for tokens the player created directly; secondary tokens remain `player_aware == false` until a discovery event.

## Invariants
- Evidence actionability is always in `[config.actionability_floor, 1.0]` (default floor: 0.10). Evidence never disappears; minimum floor is an engine constant.
- Decay runs as DeferredWorkQueue batch every 7 ticks per token (`WorkType::evidence_decay_batch`). Per-batch decay amount: `EVIDENCE_BATCH_DECAY_AMOUNT` (default 0.014). Over 7 ticks this equals ~0.002/tick effective rate, consistent with `base_decay_rate`.
- Credible holder decay: `decay_this_batch = config.base_decay_rate * 7`. Discredited holder decay: `decay_this_batch = config.base_decay_rate * config.discredit_decay_multiplier * 7` (default multiplier: 5.0).
- NPC credibility evaluation: `is_credible = false` when ANY of: (a) `holder.public_credibility < 0.3`, (b) NPC has memory entry with `type == credibility_damaged` AND `emotional_weight < -0.5`, (c) NPC has memory entry with `type == accepted_player_payment`.
- Evidence propagation confidence scaling on share: `received_confidence = sharer_confidence * trust_factor(relationship_score)` where `trust_factor` normalizes relationship score (0-100) to [0.1, 1.0].
- `EVIDENCE_SHARE_TRUST_THRESHOLD` (default 0.45): minimum trust level for an NPC contact to share an evidence token with the player.
- Evidence types: `financial`, `testimonial`, `documentary`, `physical`. Type is determined by the generating action, not by the holder or observer.
- Floating-point accumulations (aggregate actionability changes) use canonical sort order (`token_id` ascending) for deterministic batch processing.
- Same seed + same inputs = identical evidence state regardless of core count.
- All random draws go through `DeterministicRNG`.

## Failure Modes
- Orphaned token (holder NPC is dead or fled): token remains in `evidence_pool` with actionability decaying at the discredited rate (no credible holder). If `secondary_holders` exist, the highest-credibility secondary holder is promoted to primary holder.
- Token references a deleted/invalid `subject_npc_id`: log warning, skip token for propagation purposes. Token remains in pool for forensic reference but generates no new consequences.
- NaN or negative actionability from floating-point edge case: clamp to `config.actionability_floor`, log diagnostic.
- DeferredWorkQueue batch item references a token that no longer exists in `evidence_pool`: skip silently, do not reschedule.

## Performance Contract
- Province-parallel execution for evidence creation (each province's facilities processed independently).
- Decay batch processing is sequential (fired from DeferredWorkQueue, one token at a time).
- Target: < 15ms total for evidence creation + decay processing across ~500 active tokens and 6 provinces on 6 cores.
- Per-token decay batch: < 0.02ms average.
- Evidence propagation (knowledge map updates): < 10ms total for ~200 share events per tick.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["npc_behavior"]
- runs_before: ["facility_signals", "investigator_engine", "media_system"]

## Test Scenarios
- `test_criminal_action_generates_evidence_token`: An NPC business with `criminal_sector = true` producing drugs. Verify an `EvidenceToken` of type `physical` is created with `subject_npc_id` = business owner and `actionability` proportional to the observable signal.
- `test_financial_evidence_from_suspicious_transaction`: A money laundering transaction exceeds the reporting threshold. Verify a `financial` type `EvidenceToken` is created with the banker NPC as `holder_npc_id`.
- `test_testimonial_evidence_from_whistleblower`: A worker NPC with `witnessed_illegal_activity` memory entry and sufficient `risk_tolerance` generates a `testimonial` evidence token. Verify `holder_npc_id` is the whistleblower NPC.
- `test_actionability_decay_credible_holder`: A token with a credible holder decays by `config.base_decay_rate * 7` per batch (every 7 ticks). Run 700 ticks and verify actionability has halved from 1.0 to ~0.50.
- `test_actionability_decay_discredited_holder`: A token with a discredited holder (credibility < 0.3) decays 5x faster. Run 140 ticks and verify actionability has halved.
- `test_actionability_floor_never_breached`: A token decays for 2000 ticks. Verify actionability never drops below `config.actionability_floor` (0.10).
- `test_evidence_propagation_instantaneous`: NPC A shares evidence token with NPC B via `share_evidence` action. Verify NPC B's `known_evidence` map is updated in the same tick's DeltaBuffer with confidence scaled by trust_factor.
- `test_share_trust_threshold_blocks_low_trust`: NPC A (trust = 0.30 toward player) attempts to share evidence with player. Verify sharing is blocked because trust < `EVIDENCE_SHARE_TRUST_THRESHOLD` (0.45).
- `test_player_aware_set_on_direct_creation`: Player performs an action that generates evidence. Verify `player_aware = true` on the resulting token.
- `test_secondary_token_player_unaware`: NPC observes player criminal activity and creates evidence token. Verify `player_aware = false` until a discovery event.
- `test_orphaned_holder_promotes_secondary`: Primary holder NPC dies. Verify highest-credibility secondary holder is promoted to primary and decay rate recomputes based on new holder's credibility.
- `test_province_parallel_determinism`: Run 50 ticks of evidence creation and decay with 6 provinces on 1 core and 6 cores. Verify bit-identical evidence pool state.
