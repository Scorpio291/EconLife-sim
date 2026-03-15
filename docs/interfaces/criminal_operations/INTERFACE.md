# Module: criminal_operations

## Purpose
Manages the lifecycle and strategic behavior of `CriminalOrganization` entities: quarterly strategic decision evaluation, territorial conflict state machine progression, territory expansion and contraction, dominance tracking per province, and integration with the informal market economy. Criminal organizations are modeled as a `CriminalOrganization` record plus one or more `NPCBusiness` records with `criminal_sector = true`, operating within the same `RegionalMarket` and supply chain infrastructure as legitimate businesses.

In V1, NPC criminal organizations operate in the drug supply chain and protection racket sectors only, matching the V1 Feature Tier List scope. Strategic decisions are made quarterly (every 90 ticks) using the same architecture as legitimate NPCBusiness quarterly decisions but with criminal-specific inputs (territory_pressure, law_enforcement_heat, rival_activity). The territorial conflict state machine governs escalation from economic competition through open warfare to resolution.

## Inputs (from WorldState)
- `criminal_organizations` -- all `CriminalOrganization` records; iterated for strategic decision evaluation and conflict state updates
- `npc_businesses` -- businesses with `criminal_sector = true` referencing `CriminalOrganization.income_source_ids`; read for revenue, production, and market share data
- `significant_npcs` -- criminal organization member NPCs (roles: `criminal_operator`, `criminal_enforcer`); leadership NPCs; read for behavior, travel status, and risk tolerance
- `regional_markets` -- informal market prices and supply for criminal goods per province
- `provinces` -- province adjacency for territory expansion target selection; InvestigatorMeter levels for law_enforcement_heat calculation
- `current_tick` -- compared against `CriminalOrganization.strategic_decision_tick` and `decision_day_offset` for quarterly decision scheduling
- `deferred_work_queue` -- `WorkType::consequence` items for territory establishment, conflict resolution
- `world_seed` -- used by `DeterministicRNG` for expansion team selection and conflict outcome rolls

## Outputs (to DeltaBuffer)
- `CriminalOrganization.dominance_by_province` -- updated dominance values per province; seed value 0.05 on territory establishment
- `CriminalOrganization.conflict_state` -- territorial conflict stage transitions (none -> economic -> intelligence_harassment -> property_violence -> personnel_violence -> open_warfare -> resolution -> none)
- `CriminalOrganization.cash` -- cash changes from operations, expansion costs, conflict costs
- `CriminalOrganization.strategic_decision_tick` -- next decision tick after quarterly evaluation
- `NPCDelta.travel_status` -- expansion team members dispatched to target provinces enter `in_transit`
- `NPCDelta.current_province_id` -- updated when expansion team members arrive at destination
- Consequence entries -- `territory_established` (fires on expansion team arrival), `conflict_resolution` (fires on conflict de-escalation)
- DeferredWorkQueue entries -- expansion team arrival consequences, conflict stage transition schedules
- Evidence tokens -- territorial conflict at property_violence stage and above generates evidence; surveillance of departing personnel creates tokens

## Preconditions
- Evidence module has completed for this tick; evidence tokens from facility signals and other sources are available.
- All `CriminalOrganization.member_npc_ids` reference valid NPCs.
- All `CriminalOrganization.income_source_ids` reference valid `NPCBusiness` records with `criminal_sector = true`.
- Facility signals module has completed; `FacilitySignals.net_signal` values are current for criminal facilities.
- Province adjacency data (`province_route_table`) is loaded and valid for expansion target selection.

## Postconditions
- Every `CriminalOrganization` whose `strategic_decision_tick == current_tick` has had its quarterly decision evaluated exactly once.
- Conflict state machine has been advanced for all organizations with active conflicts; at most one stage per decision cycle.
- Expansion teams dispatched this tick have corresponding travel consequences and DeferredWorkQueue items.
- `dominance_by_province` values are consistent: sum across all organizations per province does not exceed 1.0.
- Territorial conflict at `personnel_violence` or higher triggers InvestigatorMeter fill_rate multiplier via evidence output.

## Invariants
- Strategic decision dispatch: `decision_day_offset` = `hash(org.id) % 90`, set at world gen. First decision at `world_start_tick + offset`; then every 90 ticks. Spreads quarterly load evenly.
- Decision matrix priority order: (1) if `law_enforcement_heat >= 0.60`: reduce activity + accelerate laundering, (2) if `territory_pressure >= 0.60 AND cash_level >= 1.0`: initiate conflict, (3) if `cash_level < 0.50`: reduce headcount + increase price pressure, (4) if `territory_pressure < 0.30 AND law_enforcement_heat < 0.30`: expand territory, (5) else: maintain.
- `territory_pressure = sum(competing_orgs.dominance_by_province for shared provinces)`.
- `cash_level = org.cash / (monthly_operating_cost_estimate * config.business.cash_comfortable_months)`.
- `law_enforcement_heat = max(InvestigatorMeter.current_level across all LE NPCs in org's territories)`.
- Expansion team size: `N = max(config.criminal.min_expansion_team_size, ceil(org.cash / config.criminal.cash_per_expansion_slot))` (defaults: min_team=2, cost_per_slot=5000.0).
- Expansion initial dominance: 0.05 (seed value on territory establishment).
- Conflict stage transitions: one stage per decision cycle maximum. De-escalation via scene card requires `player.regional_authority >= 0.60` and positive relationships with both org leaderships.
- During expansion transit, org cannot conduct operations in target province; cash reduced by expansion cost.
- If expansion team drops below `min_expansion_team_size` during transit (interception), expansion fails; cash refund = `EXPANSION_REFUND_FRACTION * initial_investment`.
- Floating-point accumulations use canonical sort order (`org_id` ascending, then `province_id` ascending) for deterministic dominance updates.
- Same seed + same inputs = identical criminal organization state regardless of core count.
- All random draws go through `DeterministicRNG`.

## Failure Modes
- Organization with zero members (all arrested/killed): organization enters dormant state; no strategic decisions evaluated. Cash and territory frozen; dominance decays at `config.criminal.dormant_dominance_decay_rate` per tick.
- Leadership NPC dead or arrested: promote highest-social_capital member as new leadership. If no members remain, organization enters dormant state.
- Expansion target province has no adjacent province link: expansion rejected; fallback to `maintain` decision. Log warning.
- NaN in cash_level or territory_pressure: clamp to 0.0, log diagnostic. Organization defaults to `maintain` decision.
- Conflict between two orgs where one is destroyed mid-conflict: surviving org's conflict state resets to `resolution` and then `none`.

## Performance Contract
- Sequential execution (tick step 8): criminal organization strategic decisions are infrequent (quarterly, staggered across ticks by decision_day_offset) and trivially cheap per evaluation.
- Target: < 5ms total for ~20 criminal organizations on strategic decision ticks; < 0.5ms on non-decision ticks (conflict state updates only).
- Per-organization strategic decision: < 0.25ms average.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["evidence", "facility_signals"]
- runs_before: ["investigator_engine", "media_system"]

## Test Scenarios
- `test_quarterly_decision_reduce_activity_on_heat`: Organization with `law_enforcement_heat = 0.70`. Verify decision output: `reduce_facility_activity: true, accelerate_laundering: true, initiate_conflict: false`.
- `test_quarterly_decision_expand_territory`: Organization with `territory_pressure < 0.30`, `law_enforcement_heat < 0.30`, adequate cash. Verify decision output: `expand_territory: true` targeting adjacent province with lowest competition.
- `test_quarterly_decision_initiate_conflict`: Organization with `territory_pressure = 0.65`, `cash_level >= 1.0`. Verify decision output: `initiate_conflict: true` targeting rival with highest dominance in shared provinces.
- `test_quarterly_decision_cash_shortage`: Organization with `cash_level = 0.30`. Verify decision output: `reduce_headcount: true, increase_price_pressure: true, expand_territory: false`.
- `test_conflict_stage_machine_escalation`: Two orgs in `economic` conflict stage. Trigger quarterly decision that escalates. Verify conflict advances to `intelligence_harassment` (one stage per cycle).
- `test_expansion_team_transit_and_arrival`: Organization dispatches expansion team to adjacent province. Verify team members enter `in_transit`, consequence item queued for arrival, and `dominance_by_province[target] = 0.05` on arrival.
- `test_expansion_failure_on_interception`: Expansion team of 3 with `min_expansion_team_size = 2`. Two members intercepted. Verify expansion fails (team drops below min), cash refund is applied, and surviving member returns to origin.
- `test_dominance_sum_capped_at_one`: Two organizations in same province with dominance 0.60 and 0.50. Verify total capped at 1.0 and values are proportionally adjusted.
- `test_dormant_org_no_decisions`: Organization with zero active members. Verify no strategic decisions are evaluated and dominance decays.
- `test_personnel_violence_triggers_evidence`: Conflict reaches `personnel_violence` stage. Verify evidence tokens are generated and InvestigatorMeter fill_rate receives multiplier.
- `test_decision_day_offset_spreads_load`: 20 organizations created. Verify `decision_day_offset` values (hash(id) % 90) produce a spread across the 90-tick quarter.
- `test_determinism_across_runs`: Run 100 ticks of criminal operations with same seed. Verify bit-identical organization states across two runs.
