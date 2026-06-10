# Module: facility_signals

> **Design note (2026-06-08):** This module is a *signal source only* — it
> computes per-facility signal composites and does **not** fill any NPC meter.
> The InvestigatorMeter (criminal — terminates in raid/arrest) and the regulator
> scrutiny meter (civil) are owned and advanced by `investigator_engine` (which
> `runs_after` this module). An earlier implementation wrote the per-tick fill
> rates into law-enforcement and regulator NPCs' `NPCDelta.motivation_delta`
> (the financial-gain behavior-weight slot) as a meter stand-in — that polluted
> those NPCs' motivations and filled nothing the rest of the system reads, so it
> was removed. The meter/evidence/consequence outputs below describe
> `investigator_engine`'s responsibility, not this module's.

## Purpose
Computes observable signal composites for all facilities each tick, combining four physical signal dimensions (power consumption anomaly, chemical waste signature, foot traffic visibility, olfactory signature) into a weighted `base_signal_composite`, and applying scrutiny mitigation to produce `net_signal`. These signals are the input that `investigator_engine` consumes to advance investigator/regulator meters. This module implements TDD Section 16: Facility Signals and Scrutiny System.

Facility signals are universal -- both criminal and legitimate facilities have signal profiles. The distinction is in who reads them: law enforcement filters to `criminal_sector == true` facilities; regulators read all facilities. Signal weights are per-facility-type data loaded from `facility_types.csv`, making the system fully moddable without engine code changes. Scrutiny mitigation for criminal facilities is derived from corrupted law enforcement authority coverage; for legitimate facilities, it comes from compliance investment.

## Inputs (from WorldState)
- `npc_businesses` -- all business records; each facility attached to a business carries a `FacilitySignals` struct with the four signal dimensions and scrutiny_mitigation; iterated per province
- `provinces` -- province list for province-parallel dispatch; `Province.has_karst` flag determines `karst_mitigation_bonus` eligibility
- `significant_npcs` -- law enforcement NPCs (role = `law_enforcement`) with `InvestigatorMeter`; regulator NPCs (role = `regulator`) with `RegulatorScrutinyMeter`; iterated per province for meter integration
- `regional_markets` -- not directly read; signals are independent of market state
- `current_tick` -- used for meter `opened_tick` recording and consequence scheduling
- Facility types data file (loaded at startup) -- `FacilityTypeSignalWeights` per facility type: `w_power_consumption`, `w_chemical_waste`, `w_foot_traffic`, `w_olfactory` (must sum to 1.0)

## Outputs (to DeltaBuffer)
- Per-facility `base_signal_composite` -- weighted sum of four signal dimensions, clamped to [0.0, 1.0] -- written to the module's `FacilitySignals` working state.
- `BusinessDelta.net_signal_update` -- `max(0.0, base_signal_composite - scrutiny_mitigation)` per facility, published onto `NPCBusiness.net_signal`. This is the channel `investigator_engine` (Tier 8) reads to feed the InvestigatorMeter. Because the orchestrator applies each module's deltas immediately, this is visible to investigator_engine within the **same tick**. Emitted only when the value changed since the last publish (apply keeps `NPCBusiness.net_signal` equal to the last published value), so steady-state delta volume is zero. `net_signal` is derived per-tick state and is **not serialized** — facility_signals recomputes it before any consumer reads it.
- **Meter advancement, status transitions, evidence-token emission (`surveillance`), and `investigation_opens`/raid consequences are owned by `investigator_engine`** (see its INTERFACE) — listed here previously by mistake.

### Bootstrap signal derivation
Until facility-type signal profiles (`facility_types.csv`) populate the four physical dimensions, businesses carry all-zero dimensions. When the computed composite is zero and `regulatory_violation_severity > 0`, the composite is derived from `regulatory_violation_severity` so the detection pipeline stays live. This is the value investigator_engine previously borrowed *raw*; routing it through this module means it is now attenuated by scrutiny mitigation (corruption/karst concealment) before reaching investigators. (Corruption coverage is applied downstream by investigator_engine's fill-rate modifier — this module applies only the signal-side mitigations, currently `scrutiny_mitigation` + karst bonus.)

## Preconditions
- Evidence module has completed for this tick; new evidence tokens from other sources are available.
- All facility `FacilitySignals` dimension values (power_consumption_anomaly, chemical_waste_signature, foot_traffic_visibility, olfactory_signature) are in [0.0, 1.0].
- `FacilityTypeSignalWeights` for each facility type are loaded and valid (four weights sum to 1.0; validated at load time).
- All law enforcement and regulator NPCs have valid `InvestigatorMeter` or `RegulatorScrutinyMeter` records.

## Postconditions
- Every facility in every province has had its `base_signal_composite` and `net_signal` recomputed exactly once this tick.
- Every law enforcement NPC has had its `InvestigatorMeter` updated: `fill_rate` recomputed from aggregate criminal facility signals in their province, `current_level` incremented, `status` derived from thresholds.
- Every regulator NPC has had its `RegulatorScrutinyMeter` updated for each facility with `net_signal > 0.0` in their province.
- Corruption modifies LE fill_rate: `fill_rate *= (1.0 - le_npc.corruption_susceptibility * regional_corruption_coverage)`.
- Karst mitigation bonus applied: facilities in provinces with `has_karst == true` receive `config.opsec.karst_mitigation_bonus` (default 0.10) added to `scrutiny_mitigation`.
- When `net_signal` drops to 0.0, meter decays at `decay_rate`. Formally opened investigations do not close on signal drop alone.

## Invariants
- Signal composite formula: `base_signal_composite = clamp(w.w_power_consumption * power_consumption_anomaly + w.w_chemical_waste * chemical_waste_signature + w.w_foot_traffic * foot_traffic_visibility + w.w_olfactory * olfactory_signature, 0.0, 1.0)`.
- Net signal: `net_signal = max(0.0, base_signal_composite - scrutiny_mitigation)`.
- LE aggregate regional signal: `regional_signal = sum(facility.signals.net_signal for all facilities in region where criminal_sector == true) / config.opsec.facility_count_normalizer` (default normalizer: 5.0).
- LE fill rate: `clamp(regional_signal * config.opsec.detection_to_fill_rate_scale, 0.0, config.opsec.fill_rate_max)` (default scale: 0.005, max: 0.01).
- InvestigatorMeter thresholds: `surveillance_threshold` (0.30), `formal_inquiry_threshold` (0.60), `raid_threshold` (0.80).
- RegulatorScrutinyMeter thresholds: `notice_threshold` (0.25), `audit_threshold` (0.50), `enforcement_threshold` (0.75).
- Regulator reads chemical_waste and foot_traffic dimensions only (not power or olfactory).
- Personnel violence stage multiplier: during `TerritorialConflictStage::personnel_violence`, LE fill_rate is multiplied by `config.opsec.personnel_violence_multiplier` (default 3.0).
- Meter decay rate: `config.investigator.decay_rate` (default 0.001/tick) when net_signal is zero.
- LE target resolution: `target_id = argmax(known_actor, estimated_signal_contribution)` from available regional signal. Actors below LE awareness threshold are not targetable until an EvidenceToken creates awareness.
- Floating-point accumulations use canonical sort order (`facility_id` ascending within each province) for deterministic signal aggregation.
- Same seed + same inputs = identical signal and meter states regardless of core count.
- All random draws (raid delay scheduling) go through `DeterministicRNG`.

## Failure Modes
- Facility type missing from `facility_types.csv` (no signal weights): log warning, use default weights (0.25 each). Facility still produces signals.
- Province with zero criminal facilities: LE `regional_signal` = 0.0; meter decays at `decay_rate`. No error.
- Province with zero LE or regulator NPCs: signals are computed but no meter is updated. No error.
- NaN in signal composite: clamp to 0.0, log diagnostic. Facility produces zero signal this tick.
- InvestigatorMeter or RegulatorScrutinyMeter references an NPC that is dead or fled: meter is frozen; no updates applied. Investigation pauses until a replacement NPC is assigned.

## Performance Contract
- Province-parallel execution for signal computation (tick step 12): each province's facilities processed independently.
- Meter integration (tick step 13) is also province-parallel.
- Target: < 15ms total for signal computation and meter integration across ~500 facilities and ~50 LE/regulator NPCs in 6 provinces on 6 cores.
- Per-facility signal computation: < 0.01ms average.
- Per-meter update: < 0.02ms average.
- Must not exceed tick budget share that would push total tick above 200ms target.

## Dependencies
- runs_after: ["evidence"]
- runs_before: ["investigator_engine"]

## Test Scenarios
- `test_signal_composite_weighted_sum`: A drug_lab facility with power=0.8, chemical=0.6, traffic=0.3, olfactory=0.9. Verify `base_signal_composite` equals the weighted sum using drug_lab signal weights from `facility_types.csv`.
- `test_net_signal_reduced_by_mitigation`: A facility with `base_signal_composite = 0.70` and `scrutiny_mitigation = 0.40`. Verify `net_signal = 0.30`.
- `test_karst_mitigation_bonus`: A facility in a province with `has_karst == true`. Verify `scrutiny_mitigation` is increased by `karst_mitigation_bonus` (0.10).
- `test_le_fill_rate_from_criminal_facilities`: Province with 3 criminal facilities with net_signals 0.5, 0.3, 0.2. Verify LE fill_rate = `clamp((1.0 / 5.0) * 0.005, 0.0, 0.01)`.
- `test_investigator_meter_threshold_surveillance`: LE NPC meter level crosses 0.30. Verify status transitions to `surveillance` and a physical evidence token is created.
- `test_investigator_meter_threshold_formal_inquiry`: LE NPC meter level crosses 0.60. Verify status transitions to `formal_inquiry` and an `investigation_opens` consequence is queued.
- `test_investigator_meter_threshold_raid`: LE NPC meter level crosses 0.80. Verify status transitions to `raid_imminent` and raid consequence is queued with 7-30 tick delay.
- `test_corruption_reduces_fill_rate`: LE NPC with `corruption_susceptibility = 0.50` in province with `regional_corruption_coverage = 0.60`. Verify fill_rate is multiplied by `(1.0 - 0.50 * 0.60)` = 0.70.
- `test_regulator_reads_chemical_and_traffic_only`: A facility with high power anomaly but zero chemical_waste and foot_traffic. Verify regulator signal is near zero despite high base_signal_composite.
- `test_meter_decay_when_signal_drops`: LE NPC had fill_rate > 0 but all criminal facilities are removed. Verify meter decays at `config.investigator.decay_rate` per tick.
- `test_personnel_violence_multiplier`: Province in `TerritorialConflictStage::personnel_violence`. Verify LE fill_rate is multiplied by 3.0.
- `test_province_parallel_determinism`: Run 50 ticks of facility signal computation with 6 provinces on 1 core and 6 cores. Verify bit-identical signal composites and meter states.
