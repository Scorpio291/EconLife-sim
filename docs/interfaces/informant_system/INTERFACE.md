# Module: informant_system

## Purpose
Evaluates every NPC under arrest pressure for informant cooperation each tick: computes flip probability from NPC state (risk_tolerance, trust, mutual incrimination obligations, compartmentalization), executes cooperation rolls, converts disclosed NPC knowledge into high-actionability evidence tokens delivered to the arresting LE NPC, and processes player countermeasures (pay_silence, threaten_silence, relocate_witness, eliminate). There is no separate loyalty field — defection probability emerges from existing NPC fields as specified in GDD section 12.2.

Not province-parallel: informant records involve cross-province relationships (arresting LE NPC may be in a different province from the informant's knowledge targets), and the countermeasure actions (relocation, elimination) have cross-province effects.

## Inputs (from WorldState)
- `informant_records` — all InformantRecord records: npc_id, arresting_le_npc_id, status (potential/cooperating/protected/eliminated), flip_probability, pressure_started_tick, cooperation_started_tick, disclosed_knowledge_ids
- `significant_npcs` — NPC records for informant NPCs: risk_tolerance, relationships (trust with criminal target), knowledge_map (KnowledgeEntry records with confidence), memory_log, status
- `obligation_network` — ObligationNode records for mutual incrimination check (favor_type IN {evidence_suppressed, whistleblower_silenced} between NPC and player)
- `evidence_pool` — existing evidence tokens for context; new tokens generated on cooperation
- `current_tick` — for cooperation timing and pressure duration tracking
- `config.informant.base_flip_rate` — default 0.005; per-tick cooperation probability base under arrest pressure
- `config.informant.risk_tolerance_scale` — default 0.30; risk tolerance contribution to flip probability
- `config.informant.trust_scale` — default 0.25; low trust contribution to flip probability
- `config.informant.mutual_incrimination_suppression` — default 0.08; per obligation node reduction
- `config.informant.compartmentalization_bonus` — default 0.10; reduction for operatives with no operative knowledge
- `config.informant.max_flip_probability` — default 0.20; per-tick cap
- `config.informant.cooperation_actionability_scale` — default 0.85; knowledge confidence to token actionability
- `config.informant.meter_fill_per_disclosure` — default 0.08; InvestigatorMeter fill per disclosed knowledge entry
- `config.informant.silence_payment` — default 50000; silence payment amount
- `config.opsec.personnel_violence_multiplier` — default 3.0; fill_rate multiplier on elimination

## Outputs (to DeltaBuffer)
- `InformantRecordDelta.status` — transitions: potential -> cooperating (on flip roll success), potential -> protected (on player countermeasure), potential/cooperating -> eliminated (on eliminate action)
- `InformantRecordDelta.flip_probability` — updated each tick for potential informants; set to 0.0 for protected informants
- `InformantRecordDelta.disclosed_knowledge_ids` — KnowledgeEntry ids revealed to LE on cooperation
- `EvidenceTokenDelta` — high-actionability tokens generated from disclosed knowledge: financial (identity_link knowledge), testimonial (activity knowledge), documentary (evidence_token knowledge). Actionability = confidence * cooperation_actionability_scale.
- `InvestigatorMeterDelta.current_level` — additive: token.actionability * meter_fill_per_disclosure per disclosed knowledge entry
- `NPCDelta.memory_log` — employment_negative MemoryEntry (weight -0.7) on threaten_silence countermeasure
- `NPCDelta.risk_tolerance` — increased by 0.10 on threaten_silence
- `NPCDelta.status` — set to dead on eliminate countermeasure
- `PlayerDelta.wealth` — deducted by silence_payment on pay_silence countermeasure
- `ObligationDelta` — new ObligationNode(FavorType::whistleblower_silenced) on pay_silence
- `EvidenceTokenDelta` (countermeasure) — financial + testimonial tokens on pay_silence (payment trail); testimonial on threaten_silence (if NPC has LE relationship); physical on relocate_witness (departure); high-actionability physical on eliminate (violence)
- `InvestigatorMeterDelta.fill_rate` — multiplied by personnel_violence_multiplier (3.0) on eliminate action

## Preconditions
- Investigator engine has completed; InvestigatorMeter statuses are current and arrest pressure is established.
- Legal process module context: arrest pressure originates from LegalCase at investigation or arrested stage.
- Obligation network is current for mutual incrimination evaluation.
- NPC knowledge maps are current from preceding tick modules.

## Postconditions
- Every InformantRecord with status == potential has had flip_probability recomputed and cooperation roll executed this tick.
- Cooperating informants have all knowledge entries with confidence > 0.4 converted to evidence tokens held by the arresting LE NPC.
- Protected informants have flip_probability set to 0.0 and are not evaluated for cooperation.
- Eliminated informants have all knowledge removed and evidence tokens they held decay at discredited rate.
- Player countermeasures generate their own evidence trail as specified.

## Invariants
- Flip probability formula: `flip_probability = base_flip_rate + risk_factor + trust_factor - incrimination_suppression - compartmentalization_bonus`, clamped to [0.0, max_flip_probability].
- Quantified formula: `BASE_FLIP_RATE (0.02) + risk_tolerance_factor * 0.15 + trust_deficit * 0.20 - intimidation * 0.25 - loyalty * 0.10`, result clamped to [0.0, 0.5].
- Risk factor: `(1.0 - npc.risk_tolerance) * config.informant.risk_tolerance_scale`. Low risk tolerance increases flip rate.
- Trust factor: `(1.0 - trust_with_criminal_target) * config.informant.trust_scale`. Zero trust maximizes trust contribution.
- Mutual incrimination: each obligation node with favor_type IN {evidence_suppressed, whistleblower_silenced} reduces flip probability by mutual_incrimination_suppression (0.08). NPC is equally exposed if they talk.
- Compartmentalization: if NPC has zero KnowledgeEntry of type activity with confidence > 0.5, cooperation value to LE is low, reducing flip probability by compartmentalization_bonus (0.10).
- Cooperation converts knowledge to evidence: identity_link -> financial token; activity -> testimonial token; evidence_token -> documentary token; else -> testimonial token. Actionability = confidence * cooperation_actionability_scale.
- Eliminate countermeasure: NPC death, all knowledge removed, InvestigatorMeter fill_rate multiplied by 3.0. Generates high-actionability physical evidence and triggers violence escalation.
- Relocate countermeasure: NPC physically moved to different province; LE loses direct pressure access; flip_probability drops to base * 0.2.
- InformantStatus enum: potential=0, cooperating=1, protected=2, eliminated=3.
- Same seed + same inputs = identical informant output (deterministic).
- All random draws (flip roll) through `DeterministicRNG`.

## Failure Modes
- Arresting LE NPC dead or fled: no pressure applied; informant record stalls. Flip probability stays at base but no cooperation consequence fires. Log warning.
- NPC already dead when flip roll succeeds: cooperation cannot proceed; record set to eliminated. Log diagnostic.
- Silence payment exceeds player wealth: payment fails; NPC remains at potential status. Player notified. Log warning.
- NaN or negative flip_probability from edge case: clamp to 0.0, log diagnostic.

## Performance Contract
- Sequential execution (not province-parallel); informant records are cross-province.
- Target: < 5ms total for all informant evaluations (~20-80 informant records typical).
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["investigator_engine", "criminal_operations"]
- runs_before: ["legal_process"]

## Test Scenarios
- `test_flip_probability_increases_with_low_risk_tolerance`: NPC with risk_tolerance 0.1 under arrest pressure. Verify flip_probability includes high risk_factor contribution ((1.0 - 0.1) * 0.30 = 0.27).
- `test_high_trust_suppresses_flip`: NPC with trust 0.9 toward criminal target. Verify trust_factor is low ((1.0 - 0.9) * 0.25 = 0.025).
- `test_mutual_incrimination_reduces_flip`: NPC with 3 mutual incrimination obligation nodes. Verify flip_probability reduced by 3 * 0.08 = 0.24.
- `test_compartmentalization_bonus_applied`: NPC with zero activity knowledge entries (compartmentalized). Verify flip_probability reduced by 0.10.
- `test_cooperation_converts_knowledge_to_evidence`: NPC flips. Has 5 KnowledgeEntries with confidence > 0.4: 2 activity, 1 identity_link, 2 evidence_token. Verify 5 evidence tokens generated with correct types (testimonial, financial, documentary) and actionability = confidence * 0.85.
- `test_meter_fill_on_disclosure`: Cooperating NPC discloses 3 knowledge entries with average actionability 0.60. Verify InvestigatorMeter incremented by 3 * 0.60 * 0.08 = 0.144.
- `test_pay_silence_protects_and_creates_obligation`: Player pays silence_payment (50000). Verify NPC status = protected, flip_probability = 0.0, ObligationNode created, and financial + testimonial evidence generated.
- `test_threaten_silence_adds_negative_memory`: Player threatens NPC. Verify employment_negative MemoryEntry (weight -0.7) added and risk_tolerance increased by 0.10.
- `test_eliminate_removes_knowledge_and_spikes_meter`: Player eliminates informant. Verify NPC status = dead, all knowledge removed, InvestigatorMeter fill_rate * 3.0, and high-actionability physical evidence generated.
- `test_relocate_reduces_flip_probability`: Player relocates witness to different province. Verify flip_probability drops to base * 0.2 and physical EvidenceToken generated on departure.
- `test_flip_probability_capped_at_maximum`: NPC with worst-case parameters (zero trust, zero risk_tolerance, no incrimination). Verify flip_probability does not exceed config.informant.max_flip_probability (0.20).
- `test_determinism_across_runs`: Run 100 ticks with multiple informant records using fixed seed. Verify bit-identical cooperation outcomes and evidence output.
