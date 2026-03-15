# Module: npc_behavior

## Purpose
Evaluates each significant NPC's motivations, selects daily actions via the expected_value decision engine, updates emotional state, processes memory formation and decay, and updates relationships. This is the core AI module and typically the heaviest per-tick operation. Province-parallel.

## Inputs (from WorldState)
- `provinces[].npcs[]` — all significant NPCs in each province; provides the NPC struct (id, role, motivations, risk_tolerance, memory_log, known_evidence, known_relationships, relationships, capital, social_capital, contact_ids, status, current_province_id, travel_status)
- `provinces[].npcs[].motivations.weights[8]` — MotivationVector indexed by OutcomeType (financial_gain, security_gain, career_advance, revenge, ideological, relationship_repair, self_preservation, loyalty_obligation); weights sum to 1.0
- `provinces[].npcs[].memory_log[]` — up to MAX_MEMORY_ENTRIES (500) MemoryEntry structs with tick_timestamp, type, subject_id, emotional_weight, decay, is_actionable
- `provinces[].npcs[].relationships[]` — sparse directed RelationshipGraph with trust (-1.0 to 1.0), fear (0.0 to 1.0), obligation_balance, shared_secrets, last_interaction_tick, is_movement_ally, recovery_ceiling
- `provinces[].npcs[].known_evidence[]` — KnowledgeMap of evidence tokens the NPC is aware of, with confidence (0.0-1.0) decaying at config.knowledge.confidence_decay_rate per tick
- `provinces[].npcs[].known_relationships[]` — KnowledgeMap of relational knowledge
- `provinces[].npcs[].risk_tolerance` — 0.0-1.0; willingness to act on knowledge; drifts toward baseline over time
- `provinces[].npcs[].status` — NPCStatus (active, imprisoned, dead, fled, waiting); only active and waiting NPCs are evaluated
- `provinces[].conditions` — regional stability_score, crime_rate, criminal_dominance_index affecting NPC risk assessments
- `provinces[].market_data` — current prices and employment data influencing economic decisions
- `provinces[].community` — grievance_level, institutional_trust feeding opposition formation checks
- `active_loans[]` — loan records relevant to NPC financial decisions
- `evidence_pool[]` — active evidence tokens that NPCs may become aware of or act on
- `deferred_work_queue` — pending DeferredWorkItems for this tick (NPC relationship decay batches every 30 ticks, knowledge confidence decay)
- `config.npc_decision_model.*` — utility weight overrides per NPC type for routine decisions
- `config.inaction_threshold_default` — proposed default 0.10; NPCs with best_action EV below this threshold choose inaction
- `config.risk.*` — MIN_RISK_DISCOUNT (default 0.05), RISK_SENSITIVITY_COEFF (default 2.0)
- `config.trust_ev_bonus` — relationship modifier scaling on expected value
- `config.satisfaction_decay_rate` — 0.002; approximately 500-tick half-life for worker satisfaction computation
- `config.npc.fear_decay_rate` — 0.002 per tick; applied in batched 30-tick operations via DeferredWorkQueue

## Outputs (to DeltaBuffer)
- `npc_deltas[]` — one NPCDelta per NPC that changed state this tick:
  - `capital_delta` (additive) — money gained or spent from actions (shopping, bribes, fines)
  - `new_status` (replacement) — status transitions (active to waiting when EV below threshold; active to fled on flight action)
  - `new_memory_entry` (append) — new MemoryEntry formed from this tick's events, observations, interactions, or hearsay
  - `updated_relationship` (upsert by target_npc_id) — trust, fear, obligation_balance changes from interactions
  - `motivation_delta` (additive) — slow motivation weight shifts driven by accumulated memory
- `consequence_deltas[]` — new ConsequenceEntry items when NPC actions trigger deferred consequences (e.g., whistleblower filing, journalist investigation, criminal defection)
- `evidence_deltas[]` — new EvidenceToken when NPC actions create evidence (e.g., publish_story, file_report)
- Cross-province effects via `CrossProvinceDeltaBuffer` — when an NPC action targets a different province (media story propagation, migration intent, criminal shipment)

## Preconditions
- financial_distribution has completed (NPCs must have up-to-date capital and income state).
- All Tier 1-4 modules have applied their deltas (production, prices, labor, economy state are current).
- DeltaBuffer is pre-reserved for up to 2,000 NPCDelta entries per province worker.
- NPC memory_log.size() <= MAX_MEMORY_ENTRIES (500) for every NPC at entry.
- MotivationVector weights sum to 1.0 for every NPC at entry.

## Postconditions
- Every active NPC has been evaluated (or skipped via lazy evaluation if no state change since last full evaluation).
- NPCs whose best_action EV exceeded inaction_threshold have queued exactly one DeferredWorkItem (WorkType::consequence) in the deferred_work_queue.
- NPCs whose best_action EV was below inaction_threshold have status set to waiting.
- All new MemoryEntry items respect MAX_MEMORY_ENTRIES cap; if at cap, lowest-decay entry is archived before append.
- Knowledge confidence values have decayed by config.knowledge.confidence_decay_rate for entries not reinforced this tick.
- Relationship updates respect recovery_ceiling constraint: trust cannot exceed recovery_ceiling after update.
- All motivation_delta shifts preserve the sum-to-1.0 invariant on MotivationVector.weights after renormalization.

## Invariants
- MotivationVector.weights always sums to 1.0 across all 8 OutcomeType values.
- memory_log.size() <= 500 for every NPC after execution.
- Relationship.trust is clamped to [-1.0, 1.0]; fear to [0.0, 1.0].
- Relationship.recovery_ceiling >= 0.15 (RECOVERY_CEILING_MINIMUM) and trust never exceeds recovery_ceiling.
- NPC capital never goes negative from module-initiated spending (actions requiring capital check affordability first).
- Same seed + same inputs = identical NPC decisions and delta output (deterministic action selection; ties broken by recency, close-call variance uses seeded RNG).
- Province-parallel execution: Province A's NPC behavior does not depend on Province B's results within the same tick.
- Knowledge confidence values remain in [0.0, 1.0].

## Failure Modes
- NPC with corrupted MotivationVector (weights not summing to 1.0): log warning, renormalize weights, continue.
- Memory log exceeding MAX_MEMORY_ENTRIES at entry: archive lowest-decay entries until at cap before evaluation.
- expected_value() produces NaN (e.g., division by zero in risk_discount): clamp to 0.0, log error, NPC defaults to waiting.
- Action precondition references non-existent entity (deleted business, retired evidence token): skip action candidate, log warning.
- Province worker exception: propagate to orchestrator (base game module; no graceful disable).

## Performance Contract
- Target: ~50ms for NPC behavior step across all 6 provinces on 6 cores (province-parallel).
- Acceptable: < 200ms total. Hard ceiling: < 500ms (30x fast-forward budget).
- Worst case: ~2,000 NPCDelta entries per tick step.
- Lazy evaluation: only run full decision loop on NPCs whose state changed since last tick. Stable NPCs skip.
- Priority queuing: NPCs with high emotional_weight recent memories evaluated first. NPCs with no actionable memories evaluated every 7 ticks instead of every tick.
- worker_satisfaction() called only when whistleblower eligibility check is needed, not unconditionally per NPC.
- Memory iteration is O(MAX_MEMORY_ENTRIES) per NPC per evaluation; acceptable given call frequency gating.

## Dependencies
- runs_after: ["financial_distribution", "npc_business", "commodity_trading", "real_estate"]
- runs_before: [] (end of Pass 1 chain; nothing in the current tick depends on this module's output)

## Test Scenarios
- `test_high_ev_action_selected_over_inaction`: Set up an NPC with one available action whose expected_value exceeds inaction_threshold (default 0.10). Verify the NPC queues a DeferredWorkItem and status remains active (not waiting).
- `test_below_threshold_produces_waiting_status`: Set up an NPC where all candidate actions produce EV below inaction_threshold. Verify NPC status is set to waiting and no DeferredWorkItem is queued.
- `test_motivation_vector_drives_action_preference`: Create two NPCs with identical situations but different MotivationVector weights (one prioritizes financial_gain, the other career_advance). Provide two available actions, one aligned to each motivation. Verify each NPC selects the action aligned with their dominant motivation.
- `test_memory_decay_archives_old_entries`: Create an NPC at memory cap (500 entries). Add a new memory via the behavior engine. Verify the entry with the lowest decay value was archived and memory_log.size() remains at 500.
- `test_risk_discount_reduces_high_exposure_ev`: Create an NPC with risk_tolerance 0.3 and an action with exposure_risk 0.8. Verify the expected_value is reduced by risk_discount factor max(0.05, 1.0 - (0.8 - 0.3) * 2.0) = 0.05.
- `test_relationship_modifier_boosts_cooperative_action`: Create an NPC with trust 0.8 toward a target. Provide a cooperative action targeting that NPC. Verify EV is multiplied by (1.0 + 0.8 * config.trust_ev_bonus), producing a higher EV than the same action with neutral trust.
- `test_whistleblower_eligibility_triggers_correctly`: Set up a worker NPC with worker_satisfaction below 0.35, a witnessed_illegal_activity memory with emotional_weight below -0.6, and risk_tolerance above 0.4. Verify the NPC's candidate actions include a whistleblower action. Then remove one condition (raise satisfaction above 0.35) and verify the whistleblower action is no longer available.
