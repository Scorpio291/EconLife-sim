# Module: media_system

## Purpose
Simulates independent media outlets, journalist NPC behavior, story creation from evidence tokens, story propagation through cross-outlet amplification and social media networks, and the conversion of damaging stories into public exposure crises. The module implements TDD Section 29 media mechanics: journalists investigate and publish based on their own motivation models, editorial gatekeepers filter stories based on outlet ownership, and stories propagate at tick step 16 through a multi-layer amplification model.

The media system is the primary mechanism by which evidence tokens reach the public. A journalist NPC who holds or is aware of an evidence token may publish a story; the story's `evidence_weight` determines its credibility and amplification potential. Stories with `tone == damaging` and sufficient evidence weight convert exposure tokens into public crises that affect the subject's reputation, trigger community response escalation, and feed into the political system. Outlet ownership by the player or an NPC creates an editorial filter that can suppress or delay damaging stories -- but journalists have their own motivation models and may leave suppressive outlets.

## Inputs (from WorldState)
- `media_outlets` -- all `MediaOutlet` records per province; reads `type` (newspaper, television, digital_outlet, social_platform), `credibility`, `reach`, `editorial_independence`, `owner_npc_id`, `journalist_ids`
- `significant_npcs` -- journalist NPCs (role = `journalist`) with `known_evidence` maps, motivation models, editorial independence; media editor NPCs (role = `media_editor`) as editorial gatekeepers
- `evidence_pool` -- active evidence tokens that may become the basis for stories; read `actionability`, `type`, `subject_npc_id`
- `provinces` -- province demographics (`adult_population`) for direct readership calculation; province media outlet lists
- `pending_stories` -- active `Story` records within `config.media.propagation_window_ticks` (default 90 ticks); iterated for propagation
- `current_tick` -- used for `published_tick` timestamps and propagation window evaluation
- `world_seed` -- used by `DeterministicRNG` for cross-outlet pickup probability rolls and social amplification

## Outputs (to DeltaBuffer)
- New `Story` entries -- created when a journalist publishes; includes `subject_id`, `journalist_id`, `outlet_id`, `tone`, `evidence_weight`, `amplification`, `published_tick`, `evidence_token_ids`
- `Story.amplification` updates -- incremented by cross-outlet pickup and social media amplification each tick during propagation window
- `PlayerDelta.exposure` -- incremented by `story.amplification * config.media.exposure_per_amplification_unit` for damaging stories meeting the crisis evidence threshold
- `NPCDelta.memory_entry` -- memory entries for journalists whose stories are suppressed (type: `editorial_suppression_witnessed`); memory entries for NPCs who become aware of a published story
- `MediaOutletDelta.editorial_independence` -- adjusted when journalists leave suppressive outlets; new outlet gains +0.30 editorial_independence
- Event notifications -- `SimulationEvent::media_story_breaks` emitted when a story is published; `SimulationEvent::media_story_buried` when editorial filter suppresses a story
- `CrossProvinceDeltaBuffer` entries -- stories from outlets with cross-province reach propagate to adjacent provinces with one-tick delay

## Preconditions
- Evidence module has completed for this tick; evidence token actionability values are current.
- NPC behavior module has completed; journalist NPC motivation evaluations and `share_evidence` actions are resolved.
- All `MediaOutlet.journalist_ids` reference valid NPCs with `role == journalist`.
- All `Story.evidence_token_ids` reference valid evidence tokens in `evidence_pool`.
- Goods data file and province demographics are loaded (for readership calculation).

## Postconditions
- Every journalist NPC has been evaluated for story publication potential this tick (based on evidence awareness and motivation).
- Editorial filter applied: stories at player-owned outlets where tone is damaging to player are subject to `publish_decision = journalist.editorial_independence * (1.0 - config.media.owner_suppression_base_rate)`.
- All active stories within the propagation window have had their amplification updated via cross-outlet and social media propagation.
- Exposure conversion applied for damaging stories with `evidence_weight >= config.media.crisis_evidence_threshold` (default 0.40).
- Stories outside the propagation window (published_tick + propagation_window_ticks < current_tick) are marked inactive and no longer propagate.

## Invariants
- Story propagation formula (per active story per tick):
  - Direct readers: `outlet.reach * province.demographics.adult_population`.
  - Cross-outlet pickup: for each other outlet in province, `pickup_probability = story.evidence_weight * other_outlet.credibility * config.media.cross_outlet_pickup_rate` (default 0.15). If pickup occurs: `story.amplification += other_outlet.reach * config.media.cross_outlet_amplification_factor` (default 0.50).
  - Social media amplification: for each social_platform outlet, `social_amplification = story.amplification * social_outlet.reach * config.media.social_amplification_multiplier * (1.0 + story.evidence_weight)` (default multiplier 2.50).
- Exposure conversion: `subject.exposure += story.amplification * config.media.exposure_per_amplification_unit` (default 0.02) when `story.tone == damaging AND story.evidence_weight >= config.media.crisis_evidence_threshold`.
- Owner suppression: `publish_decision = journalist.editorial_independence * (1.0 - config.media.owner_suppression_base_rate)` (default 0.50). Suppressed journalist receives `editorial_suppression_witnessed` memory entry. If journalist subsequently leaves outlet, new outlet's `editorial_independence += 0.30`.
- Story `evidence_weight` is derived from attached evidence tokens: `mean(token.actionability for token in story.evidence_token_ids)`.
- `propagation_window_ticks` (default 90): stories remain active for ~3 in-game months.
- Floating-point accumulations use canonical sort order (`story_id` ascending, then `outlet_id` ascending) for deterministic amplification sums.
- Same seed + same inputs = identical story state and exposure values regardless of core count.
- All random draws go through `DeterministicRNG`.

## Failure Modes
- Journalist NPC is dead or fled: no stories published from that journalist. Outlet continues with remaining journalists.
- Outlet with zero journalists: outlet is dormant; no stories generated but may still participate in cross-outlet pickup for stories published by other outlets.
- Evidence token referenced by a story has been suppressed (actionability = floor): story's `evidence_weight` recomputed; may drop below crisis threshold, halting exposure conversion.
- NaN in amplification calculation: clamp to 0.0, log diagnostic. Story produces no amplification this tick.
- Story references a subject NPC that no longer exists: log warning, skip exposure conversion for that subject. Story remains in pool for cross-outlet propagation.

## Performance Contract
- Sequential execution (tick step 16): media propagation is a separate sub-pass within the step; not province-parallel because cross-outlet pickup involves outlet interactions within the same province.
- Target: < 10ms total for ~50 media outlets, ~100 active stories, and ~30 journalist NPCs across 6 provinces.
- Per-story propagation: < 0.1ms average.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["evidence", "facility_signals"]
- runs_before: ["trust_updates", "political_cycle"]

## Test Scenarios
- `test_journalist_publishes_from_evidence`: Journalist NPC is aware of a high-actionability evidence token against the player. Verify a `Story` is created with correct `subject_id`, `tone == damaging`, and `evidence_weight` derived from token actionability.
- `test_editorial_filter_suppresses_damaging_story`: Player owns a media outlet. Journalist at that outlet has a damaging story about the player. Verify `publish_decision` formula is applied and story is suppressed when `editorial_independence * (1.0 - 0.50)` produces a roll below threshold.
- `test_suppressed_journalist_gets_memory_entry`: Story suppressed by editorial filter. Verify journalist receives `editorial_suppression_witnessed` memory entry with negative emotional_weight.
- `test_cross_outlet_pickup_amplification`: Story published at outlet A with evidence_weight = 0.80. Outlet B in same province with credibility = 0.90. Verify pickup probability = `0.80 * 0.90 * 0.15 = 0.108` and amplification is increased on successful pickup.
- `test_social_media_amplification`: Social platform outlet in province with reach = 0.60. Active story with amplification = 1.0 and evidence_weight = 0.50. Verify social amplification = `1.0 * 0.60 * 2.50 * 1.50 = 2.25`.
- `test_exposure_conversion_damaging_story`: Damaging story with evidence_weight >= 0.40 and amplification = 5.0. Verify subject.exposure increases by `5.0 * 0.02 = 0.10`.
- `test_exposure_not_converted_below_threshold`: Damaging story with evidence_weight = 0.30 (below crisis_evidence_threshold 0.40). Verify no exposure conversion occurs.
- `test_story_inactive_after_propagation_window`: Story published 91 ticks ago (propagation_window_ticks = 90). Verify story no longer propagates and amplification is not updated.
- `test_journalist_leaves_suppressive_outlet`: Journalist suppressed twice (multiple memory entries). Journalist leaves outlet. Verify new outlet's editorial_independence increases by 0.30.
- `test_neutral_story_no_exposure_conversion`: Story with `tone == neutral` published. Verify no exposure conversion regardless of evidence_weight.
- `test_cross_province_story_propagation`: National outlet publishes story. Verify story effects propagate to adjacent provinces via `CrossProvinceDeltaBuffer` with one-tick delay.
- `test_determinism_across_runs`: Run 50 ticks of media propagation with same seed. Verify bit-identical story amplification and exposure values across two runs.
