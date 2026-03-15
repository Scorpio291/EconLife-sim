# Module: protection_rackets

## Purpose
Processes all active ProtectionRacket records each tick: collects payment from extorted businesses, escalates enforcement against businesses that refuse payment, accumulates community grievance contributions, and handles racket lifecycle (establishment, active collection, refusal escalation, lapse, expulsion). Revenue flows directly to the criminal organization's cash pool with no production facility, no goods, and no supply chain — mechanically simpler than the drug economy. The core loop is: demand -> payment or refusal -> escalation.

Province-parallel execution: each province's rackets operate independently on local businesses and criminal org presence.

## Inputs (from WorldState)
- `protection_rackets` — all ProtectionRacket records: id, criminal_org_id, target_business_id, demand_per_tick, status (active/refused/lapsed/expelled), escalation_stage, last_payment_tick, demand_issued_tick, community_grievance_contribution
- `npc_businesses` — target NPCBusiness records: cash (for payment ability check), revenue_per_tick (for demand calculation), profile (defensive_incumbent affects refusal probability), province_id
- `provinces` — province list for province-parallel dispatch; includes conditions.criminal_dominance_index (affects refusal probability) and community.grievance_level (accumulated by racket activity)
- `significant_npcs` — criminal_enforcer NPCs for escalation consequences; target business owner NPCs for refusal decision and memory entries
- `current_tick` — for overdue tick calculation and escalation timing
- `config.racket.demand_rate` — default 0.08; racket demand as fraction of target's revenue_per_tick
- `config.racket.grievance_per_demand_unit` — default 0.00001; grievance per currency unit of demand per tick
- `config.racket.incumbent_refuse_probability` — default 0.40; base refusal rate for defensive_incumbent profile
- `config.racket.default_refuse_probability` — default 0.20; base refusal rate for other profiles
- `config.opsec.personnel_violence_multiplier` — default 3.0; InvestigatorMeter fill_rate multiplier during violence escalation

## Outputs (to DeltaBuffer)
- `BusinessDelta.cash_delta` — deducted from target business cash by demand_per_tick (payment); credited to criminal_org cash
- `BusinessDelta.protection_cost_per_tick` — set to demand_per_tick on paying businesses; included in cost_per_tick for strategic decision calculations
- `CriminalOrgDelta.cash` — credited by demand_per_tick per active paying racket
- `ProtectionRacketDelta.status` — transitions between active, refused, lapsed, expelled
- `ProtectionRacketDelta.escalation_stage` — advances through demand_issued, warning, property_damage, violence, abandonment based on ticks overdue
- `CommunityDelta.grievance_level` — additive: min(1.0, grievance_level + community_grievance_contribution) per tick for active rackets and rackets at warning+ escalation
- `ConsequenceDelta` — escalation consequences: intimidation scene card (warning), facility_incident severity 0.4 (property_damage), personnel violence (violence), business bankruptcy/exit (abandonment)
- `EvidenceTokenDelta` — physical EvidenceToken on property_damage; testimonial EvidenceToken on violence (from witnesses)
- `InvestigatorMeterDelta.fill_rate` — multiplied by personnel_violence_multiplier (3.0) during violence escalation stage
- `NPCDelta.memory_log` — employment_negative MemoryEntry for target business owner NPC on warning (emotional_weight -0.5)

## Preconditions
- Criminal operations module has completed; CriminalOrganization territory and dominance values are current.
- NPC businesses have current cash and revenue_per_tick values from production and financial modules.
- Province conditions (criminal_dominance_index) are current for refusal probability calculation.

## Postconditions
- Every active or refused ProtectionRacket has been processed exactly once this tick.
- Paying businesses have cash decremented by demand_per_tick; criminal org cash incremented by same amount.
- Refused rackets have escalation_stage advanced if ticks_overdue crosses stage thresholds (5: warning, 15: property_damage, 30: violence, 60: abandonment).
- Community grievance_level in province incremented for all active rackets and rackets at warning or higher escalation.
- Businesses that cannot pay (cash < demand_per_tick) remain active but no payment occurs; sustained inability triggers strategic_decision: exit province.
- Lapsed rackets (org stopped collecting) and expelled rackets (org expelled from province) are not processed.

## Invariants
- RacketStatus enum: active=0, refused=1, lapsed=2, expelled=3.
- RacketEscalationStage enum: demand_issued=0, warning=1, property_damage=2, violence=3, abandonment=4.
- Demand formula: `demand_per_tick = target_business.revenue_per_tick * config.racket.demand_rate`.
- Grievance formula: `community_grievance_contribution = config.racket.grievance_per_demand_unit * demand_per_tick`. Applied each tick when status == active OR escalation_stage >= warning.
- Refusal probability formula: `refuse_probability = (defensive_incumbent ? 0.40 : 0.20) + (1.0 - province.criminal_dominance_index) * 0.30 - target.regulatory_violation_severity * 0.10`. High criminal dominance reduces refusal; businesses with their own violations are more susceptible.
- Escalation stage thresholds (ticks overdue): warning=5, property_damage=15, violence=30, abandonment=60.
- Violence escalation: InvestigatorMeter fill_rate multiplied by config.opsec.personnel_violence_multiplier (3.0). Testimonial evidence from witnesses.
- Protection rackets are NOT modeled as ObligationNodes. They use the ProtectionRacket struct exclusively (per TDD 37).
- Floating-point accumulations use canonical sort order (racket_id ascending within province) for deterministic summation.
- Same seed + same inputs = identical racket output regardless of core count.
- All random draws (refusal decision) through `DeterministicRNG`.

## Failure Modes
- Target business bankrupt or exited province: racket lapses automatically. Revenue stops. Log diagnostic.
- Criminal org expelled from province: all rackets for that org in province set to expelled status. Log diagnostic.
- Enforcer NPC dead or imprisoned: escalation consequences cannot be executed; escalation stage advances but consequence is delayed. Log warning.
- NaN or negative demand_per_tick: clamp to 0.0, log diagnostic. Racket produces no revenue.

## Performance Contract
- Province-parallel execution across up to 6 provinces.
- Target: < 5ms total for all protection rackets across 6 provinces (~50-200 rackets typical) on 6 cores.
- Per-racket processing: < 0.025ms average.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["criminal_operations"]
- runs_before: ["community_response", "investigator_engine"]

## Test Scenarios
- `test_active_racket_collects_payment`: Business with cash 10000 and demand_per_tick 800. Verify business cash decremented by 800 and criminal org cash incremented by 800.
- `test_demand_scales_with_revenue`: Business A with revenue 10000 and Business B with revenue 5000, both at demand_rate 0.08. Verify demand_per_tick is 800 and 400 respectively.
- `test_refused_racket_escalates_to_warning`: Racket refused at tick 0. Advance 5 ticks. Verify escalation_stage transitions to warning and intimidation MemoryEntry (weight -0.5) added to target business owner.
- `test_refused_racket_escalates_to_property_damage`: Racket refused. Advance 15 ticks. Verify escalation_stage transitions to property_damage and facility_incident consequence (severity 0.4) queued. Physical EvidenceToken generated.
- `test_violence_escalation_triggers_meter_multiplier`: Racket at violence stage (30 ticks overdue). Verify InvestigatorMeter fill_rate multiplied by 3.0 and testimonial EvidenceToken generated from witnesses.
- `test_abandonment_bankrupts_or_exits_business`: Racket at 60 ticks overdue. Verify escalation_stage transitions to abandonment and target business enters bankruptcy or exits province. Province criminal_dominance_index increments.
- `test_grievance_accumulates_per_tick`: Active racket with demand_per_tick 800. Verify province grievance_level increases by 800 * 0.00001 = 0.008 per tick.
- `test_business_cannot_pay_no_escalation`: Business with cash 100 and demand 800. Verify no payment extracted, no escalation triggered, status remains active.
- `test_refusal_probability_affected_by_dominance`: Province with criminal_dominance_index 0.8 (high). Verify refusal probability is lower (dominance suppresses resistance).
- `test_lapsed_racket_not_processed`: Racket with status == lapsed. Verify no payment collected, no grievance accumulated, no escalation.
- `test_province_parallel_determinism`: Run 50 ticks of racket processing with 6 provinces on 1 core and 6 cores. Verify bit-identical cash, grievance, and escalation outputs.
