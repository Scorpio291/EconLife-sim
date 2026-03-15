# Module: political_cycle

## Purpose
Manages the full election pipeline for all political offices within the player's nation: campaign activation, coalition building, endorsement tracking, campaign spending, vote share calculation, election resolution, office transitions, and legislative vote processing. Politicians respond to public opinion (CommunityState), lobbyist pressure (ObligationNetwork), and demographic approval ratings (PopulationCohort). Elections resolve using the coalition-weighted vote share algorithm specified in TDD Section 14.

This module also processes legislative proposals through the committee-to-vote pipeline. NPC legislators evaluate proposals based on motivation alignment, obligation pressure from sponsors, and constituency demographic lean. Policy effects from enacted legislation are queued as DeferredWorkItems for downstream modules.

## Inputs (from WorldState)
- `nations[]` — Nation structs with `NationPoliticalCycleState`:
  - `current_administration_tick` — when current administration took office
  - `national_approval` (0.0-1.0) — aggregate national approval rating
  - `election_campaign_active` (bool) — whether a campaign is currently underway
  - `next_election_tick` — tick at which the next national election resolves
  - `government_type` — GovernmentType enum (Democracy, Autocracy, Federation, FailedState)
- `political_offices[]` — PoliticalOffice structs:
  - `office_type` — PoliticalOfficeType enum (city_council through head_of_state)
  - `current_holder_id` — NPC currently holding this office
  - `election_due_tick` — tick of next election for this specific office
  - `term_length_ticks` — term duration in ticks
  - `approval_by_demographic` — map of DemographicGroup to approval float (0.0-1.0)
  - `win_threshold` — vote share required to win (default 0.5)
  - `pending_legislation_ids` — active legislative proposal IDs in this body
- `campaigns[]` — Campaign structs:
  - `active_candidate_id` — NPC ID of the running candidate
  - `office_id` — target PoliticalOffice ID
  - `campaign_start_tick`, `election_tick` — campaign timing bounds
  - `coalition_commitments[]` — CoalitionCommitment records with demographic, promise_text, obligation_node_id, delivered flag
  - `endorsements[]` — Endorsement records with endorser_npc_id, primary_demographic, approval_bonus
  - `resource_deployment` — total campaign spending (game currency)
  - `current_approval_by_demographic` — per-demographic approval snapshot
  - `event_modifiers[]` — accumulated event-driven vote share modifiers
- `legislative_proposals[]` — LegislativeProposal structs with status, sponsor_id, npc_legislator_positions, vote_result, policy_effect_id
- `provinces[].community` — CommunityState (grievance_level, institutional_trust, cohesion)
- `provinces[].cohort_stats.cohorts[]` — PopulationCohort with political_lean, size, grievance_contribution
- `significant_npcs[]` — NPC structs for candidates and legislators (motivations, role, status)
- `obligation_network[]` — ObligationNode records for legislative vote pressure
- `current_tick` — for election timing and campaign phase checks
- Config constants from `simulation_config.json -> politics`:
  - `SUPPORT_THRESHOLD` = 0.55
  - `OPPOSE_THRESHOLD` = 0.35
  - `MAJORITY_THRESHOLD` = 0.50 (standard legislation); 0.67 (constitutional, EX in V1)
  - `RESOURCE_SCALE` = 2.0
  - `RESOURCE_MAX_EFFECT` = 0.15

## Outputs (to DeltaBuffer)
- `PoliticalOfficeDelta` — office holder changes on election resolution: new holder_id, updated election_due_tick (advanced by term_length_ticks)
- `CampaignDelta` — campaign state updates: approval shifts per demographic from endorsements and events, resource_deployment changes, campaign completion status
- `NationDelta` — NationPoliticalCycleState updates: next_election_tick, national_approval, election_campaign_active flag
- `NPCDelta[]` — for candidate NPCs: memory entries for election outcome (won/lost, emotional_weight scaled by office importance); for legislators: memory entries for significant votes cast
- `DeferredWorkItem[]` — consequence entries for:
  - Election outcome consequences (political_approval_shift)
  - Policy change consequences from enacted legislation (policy_effect_id activation)
  - Campaign start activation when election_due_tick - campaign_lead_time reached
- `ProvinceDelta[]` — RegionalPoliticalState updates: approval_rating, election_due_tick, governing_office_id
- `LegislativeProposalDelta[]` — status transitions (drafted -> in_committee -> floor_debate -> voted -> enacted/failed), vote tallies

## Preconditions
- `community_response` has completed (CommunityState fields are current for this tick).
- All PoliticalOffice records reference valid NPC holder IDs or 0 (vacant).
- All Campaign records reference valid office IDs with matching election_tick values.
- PopulationCohort data is current (updated monthly via DeferredWorkQueue).
- Recipe registry is loaded with demographic turnout_weight configuration.

## Postconditions
- All offices with `election_due_tick == current_tick` have had elections resolved exactly once.
- Winning candidates installed as office holders with `election_due_tick += term_length_ticks`.
- Losing candidates receive election_outcome memory entries with negative emotional_weight.
- Campaigns that reached `election_tick` are resolved and marked complete.
- NationPoliticalCycleState updated: `next_election_tick` reflects nearest upcoming election, `election_campaign_active` reflects whether any active campaign exists.
- Legislative proposals at `vote_tick == current_tick` have vote results computed and status advanced.
- Enacted proposals have policy_effect_id consequences queued in DeferredWorkQueue.

## Invariants
- `approval_by_demographic` values clamped to [0.0, 1.0] per demographic group.
- `player_vote_share` clamped to [0.0, 1.0] after all modifiers applied.
- `event_modifiers` total clamped to [-0.20, 0.20] per TDD Section 14 Step 4.
- `resource_modifier = clamp(tanh(resource_deployment * RESOURCE_SCALE) * RESOURCE_MAX_EFFECT, -RESOURCE_MAX_EFFECT, RESOURCE_MAX_EFFECT)` — diminishing returns enforced.
- Only Democracy and Federation government types hold elections; Autocracy and FailedState skip election resolution entirely.
- Election resolution uses `DeterministicRNG(world_seed, current_tick, office_id)` for all random draws.
- Vote resolution: `passed = (votes_for / (votes_for + votes_against)) > MAJORITY_THRESHOLD`.
- NPC legislator undecided resolution: `base_support = dot(n.motivations, policy_motivation_alignment_vector) + obligation_bonus + constituency_pressure`.
- NOT province-parallel: elections are national-scope events requiring cross-province demographic aggregation.
- Same seed + same inputs = identical election outcomes regardless of core count.

## Failure Modes
- Nation with `government_type == Autocracy`: skip election resolution; process policy changes via executive decree path only. Log no warning (valid game state).
- PoliticalOffice with no valid candidates at `election_tick`: incumbent retains office, log warning, schedule next election at `current_tick + term_length_ticks`.
- Division by zero in vote share denominator: if `sum(turnout_weight * population_fraction) == 0.0`, set `raw_share` to 0.5 (coin flip), log warning.
- Campaign with empty `coalition_commitments`: valid but weak campaign; `approval_by_demographic` uses default province demographic leans.
- LegislativeProposal with no sponsor NPC (sponsor deceased/fled): proposal status set to `failed`, log warning.
- NaN or negative values in any vote share component: clamp to 0.0, log diagnostic.

## Performance Contract
- NOT province-parallel: runs on main thread (elections are national-scope events).
- Target: < 5ms total for up to 20 political offices across all nations.
- Elections resolve at most once per office per term (typically hundreds of ticks apart); most ticks produce zero deltas.
- Legislative votes: < 1ms per proposal at up to 50 NPC legislators.
- O(offices * demographics) per election resolution; O(proposals * legislators) per legislative vote.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["community_response"]
- runs_before: ["regional_conditions"]

## Test Scenarios
- `test_election_resolves_at_due_tick`: Set office `election_due_tick` to `current_tick` with one candidate and known demographic support. Verify election resolution fires, winner installed, `election_due_tick` advanced by `term_length_ticks`.
- `test_coalition_vote_share_weighted_calculation`: Set known `approval_by_demographic` = {working_class: 0.7, corporate: 0.3} with `population_fraction` = {working_class: 0.6, corporate: 0.4} and equal turnout weights. Verify `raw_share = (0.7*0.6 + 0.3*0.4) / 1.0 = 0.54`.
- `test_resource_deployment_diminishing_returns`: Set `resource_deployment` to 0.5 and 5.0. Verify both produce modifiers within `[-RESOURCE_MAX_EFFECT, RESOURCE_MAX_EFFECT]` and that 5.0 produces less than 2x the effect of 0.5 (diminishing returns via tanh).
- `test_autocracy_skips_election`: Set nation `government_type` to Autocracy with `election_due_tick == current_tick`. Verify zero election deltas produced. Verify policy changes still process via executive decree.
- `test_campaign_endorsement_boosts_demographic`: Add endorsement for `working_class` demographic with `approval_bonus = 0.08`. Verify `coalition_support[working_class]` increased by 0.08, clamped to [0.0, 1.0].
- `test_no_election_this_tick_is_noop`: Set all `election_due_tick` values to `current_tick + 100`. Verify zero deltas produced and module returns in < 0.1ms.
- `test_event_modifiers_capped_at_twenty_percent`: Set `event_modifiers` summing to 0.50. Verify `event_total` clamped to 0.20 in final vote share calculation.
- `test_legislative_vote_resolution_passes`: Create proposal with 60% support among legislators. Verify `passed == true` with `MAJORITY_THRESHOLD = 0.50`.
- `test_legislative_vote_undecided_npc_resolution`: Create undecided NPC legislator with known motivations. Verify vote determined by `base_support + obligation_bonus + constituency_pressure` compared to `SUPPORT_THRESHOLD` and `OPPOSE_THRESHOLD`.
- `test_losing_candidate_memory_entry`: Resolve election where candidate loses. Verify candidate NPC receives memory entry with `type = event`, negative `emotional_weight`, and `is_actionable = true`.
- `test_no_candidates_incumbent_retained`: Set office with `election_due_tick == current_tick` but no registered campaigns. Verify incumbent retains office and next election scheduled.
- `test_deterministic_election_across_runs`: Run identical election scenario twice with same seed. Verify bit-identical vote share and outcome.
