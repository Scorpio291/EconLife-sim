# Module: random_events

## Purpose
Rolls for random occurrences each tick per province using a Poisson probability model conditioned on regional state, selects event types and templates by weighted draw, applies per-tick effects for active events, expires completed events, and generates consequence deltas including evidence tokens, NPC memory entries, and regional condition modifiers. Uses deterministic RNG seeded from world_seed. Province-parallel.

## Inputs (from WorldState)
- `current_tick` — for event duration tracking and expiry checks
- `world_seed` — determinism anchor for all RNG calls
- `provinces` — per-province climate stress, instability, infrastructure rating for probability and weight conditioning
- `regional_markets` — spot prices for economic event effects (price spikes/crashes)
- `npc_businesses` — business state for accident event targeting (facility disruption, owner identification)
- `significant_npcs` — NPC state for human event effects (memory entries, scene card generation)
- `active_random_events` — currently active events being tracked for per-tick effects and expiry

## Outputs (to DeltaBuffer)
- `RegionDelta` — agricultural_output_modifier (natural events), infrastructure_damage (natural/accident events), stability impact
- `MarketDelta.spot_price_override` — price shocks from economic events (+-0.10 to +-0.40 on affected goods)
- `BusinessDelta.cash_delta` — cash impact from economic events (credit tightening/investment opportunity)
- `FacilityDelta.output_rate` — facility disruption from accident events (-0.10 to -1.0, partial to full)
- `NPCDelta.new_memory_entry` — witnessed_physical_hazard (accident), economic stress (economic), witness memories (human events)
- `EvidenceDelta` — EvidenceToken generated for accidents at severity >= `config.events.evidence_severity_threshold` (0.3), with `subject_id = facility.owner_id`
- `CommunityDelta` — grievance or cohesion modifiers from human events
- `SceneCardDelta` — scene card generated for player if in affected province during human events
- `ActiveRandomEventDelta` — new ActiveRandomEvent records pushed; expired events marked with `end_tick = 0`

## Preconditions
- Calendar module has executed (date context available for seasonal event weighting).
- Province conditions (climate_stress_current, instability, infrastructure_rating) are current for this tick.
- Event template registry loaded from `/data/events/event_templates.json` at startup; immutable during session.
- RNG state is deterministic and reproducible from `world_seed + current_tick + province_id`.

## Postconditions
- Each LOD 0 province has been evaluated for a new random event firing via the Poisson model.
- For provinces where an event fires: event type selected by conditioned weighted draw, template selected within type, ActiveRandomEvent created with severity in [severity_min, severity_max] and duration in [duration_ticks_min, duration_ticks_max].
- All currently active events (`active_random_events` where `current_tick < end_tick`) have had their per-tick effects applied.
- All expired events (`current_tick >= end_tick`) have been marked resolved (`end_tick = 0`).
- Evidence tokens generated exactly once per qualifying event (tracked by `evidence_generated` flag on ActiveRandomEvent).

## Invariants
- Poisson probability formula: `p_event_this_tick = 1.0 - exp(-adjusted_rate / ticks_per_month)` where `adjusted_rate = base_rate * (1.0 + climate_stress * climate_amplifier) * (1.0 + instability * instability_amplifier) * economic_volatility_modifier`.
- `random_event_base_rate` default: 0.15 events per province per month (~1.8/year at baseline).
- `climate_event_amplifier` default: 1.5; `instability_event_amplifier` default: 1.0.
- Event type selection weights: natural 0.25*(1+climate_stress), accident 0.20*(1+(1-infrastructure_rating)), economic 0.30*economic_volatility_modifier, human 0.25*(1+instability). Weights normalized to sum to 1.0 before draw.
- Template selection within type uses `base_weight * climate_stress_weight_scale * instability_weight_scale * infrastructure_weight_scale` conditioned on province state.
- Evidence token generation threshold: severity >= `config.events.evidence_severity_threshold` (default 0.3). Below threshold: local memory entries only, no evidence token.
- Evidence tokens for accidents always set `subject_id = facility.owner_id` regardless of who owns the facility (player or NPC).
- Deterministic: same seed + same province state = identical event sequence on any core count.
- Natural event effect ranges: agricultural_output_modifier -0.05 to -0.40; deposit accessibility +0.0 to -0.20; infrastructure_damage +0.01 to +0.15.
- Accident event effect ranges: facility output_rate -0.10 to -1.0; infrastructure_damage +0.01 to +0.05.
- Economic event effect ranges: spot_price shift +-0.10 to +-0.40; business cash +-modifier.

## Failure Modes
- No matching template for selected event type in province: log warning, skip event generation for this province this tick. Province is re-evaluated next tick.
- Province references invalid facility for accident event: skip facility-specific effects, still apply regional infrastructure damage.
- Template data file missing or malformed at startup: panic with descriptive error (templates are required content).

## Performance Contract
- Province-parallel execution across up to 6 provinces.
- Target: < 10ms total for 6 provinces (Poisson roll + template selection + effect application).
- Per-province: < 2ms including active event per-tick processing.
- Active event count expected to be low (0-3 per province typically); linear scan acceptable.

## Dependencies
- runs_after: ["calendar"]
- runs_before: []

## Test Scenarios
- `test_poisson_rate_zero_no_events_fire`: Set `random_event_base_rate = 0.0`. Run 100 ticks across all provinces. Verify zero events fire.
- `test_baseline_rate_produces_expected_frequency`: Set base_rate to 0.15 with all province modifiers at 0.0 (neutral). Run 3600 ticks (10 simulated years). Verify event count per province is approximately 18 (1.8/year) within statistical tolerance (chi-squared test, p > 0.01).
- `test_high_climate_stress_increases_natural_events`: Two provinces: one with climate_stress = 0.0, one with climate_stress = 0.8. Run 1000 ticks. Verify the high-stress province fires significantly more natural-type events.
- `test_high_instability_increases_human_events`: Province with instability = 0.9 vs instability = 0.1. Run 1000 ticks. Verify the unstable province generates more human-type events proportionally.
- `test_low_infrastructure_increases_accident_events`: Province with infrastructure_rating = 0.2 vs 0.9. Verify the low-infrastructure province fires more accident events.
- `test_event_severity_within_template_bounds`: Fire an event using template with severity_min = 0.3, severity_max = 0.7. Verify ActiveRandomEvent.severity is in [0.3, 0.7].
- `test_event_duration_within_template_bounds`: Template with duration_ticks_min = 5, duration_ticks_max = 15. Verify ActiveRandomEvent duration is in [5, 15].
- `test_evidence_token_generated_above_severity_threshold`: Accident event at severity 0.5 (above threshold 0.3). Verify EvidenceToken is generated with correct subject_id = facility.owner_id. Verify `evidence_generated` flag is set to true.
- `test_no_evidence_below_severity_threshold`: Accident event at severity 0.2 (below threshold 0.3). Verify no EvidenceToken generated. Verify NPC memory entries still created.
- `test_evidence_generated_only_once`: Active event spanning 10 ticks with severity above threshold. Verify exactly one EvidenceToken is created across all 10 ticks (evidence_generated flag prevents duplicates).
- `test_event_expires_at_end_tick`: Event with started_tick = 10, duration = 5. At tick 15, verify event is marked resolved (end_tick = 0) and per-tick effects cease.
- `test_natural_event_reduces_agricultural_output`: Drought event fires in a province. Verify RegionConditions.agricultural_output_modifier is reduced by amount within [-0.05, -0.40].
- `test_accident_event_disrupts_facility`: Industrial fire at a facility. Verify Facility.output_rate is reduced within [-0.10, -1.0] range.
- `test_economic_event_shifts_spot_price`: Market shock event. Verify RegionalMarket.spot_price for affected good shifts within +-[0.10, 0.40] range.
- `test_human_event_may_generate_scene_card`: Human event fires in player's current province. Verify a scene card is added to pending_scene_cards.
- `test_deterministic_across_core_counts`: Run 100 ticks with identical seed on 1 core and 6 cores. Verify identical event sequence, severity, duration, and effects.
- `test_template_weight_conditioning`: Province with high climate stress. Verify drought templates (high `climate_stress_weight_scale`) are selected more frequently than templates with low climate scaling.
