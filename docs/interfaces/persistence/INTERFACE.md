# Module: persistence

## Purpose
Serializes and deserializes the complete WorldState for save/load functionality using Protocol Buffers and LZ4 compression. The persistence module implements three layers: continuous write-ahead logging (WAL) that captures every delta for crash recovery, periodic monthly snapshots that enable fast loading, and the timeline restoration system that allows Standard-mode players to return to previous snapshots with diegetic disruption consequences. Round-trip determinism is the critical invariant: serialize-deserialize-serialize must produce byte-identical output. This guarantees that save/load does not introduce simulation divergence.

The persistence module also manages schema versioning and migration. Expansion packages that add WorldState fields register migration functions. On load, if the saved schema version is older than current, migrations are applied in version order. Saves with schema versions newer than the running code are rejected. The module enforces the consequence-aware restoration constraint: in Standard mode, restoration is only available through `restore_with_consequences()` which applies TimelineDisruptionEvent penalties. In Ironman mode, no restoration API is reachable. Specified in TDD Section 22.

## Inputs (from WorldState)
- `world_state` — the complete WorldState structure (passed as const reference for serialization):
  - `current_tick` (uint32) — absolute tick counter
  - `world_seed` (uint64) — determinism anchor
  - `nations[]` — all Nation records with government_type, political_cycle, province_ids, LOD profiles
  - `provinces[]` — all Province records with demographics, resources, infrastructure, conditions, community, political state
  - `significant_npcs[]` — all NPC records with memory_log (up to 500 entries), knowledge maps, relationships, motivations
  - `named_background_npcs[]` — simplified NPCs
  - `player` — PlayerCharacter with all stats, traits, skills, relationships, InfluenceNetworkHealth
  - `regional_markets[]` — all market data per (good_id x province_id) with supply, demand, spot_price
  - `npc_businesses[]` — all business records with cash, revenue, sector, facility references
  - `evidence_pool[]` — all active EvidenceToken records
  - `deferred_work_queue` — DeferredWorkQueue min-heap with all pending WorkItems
  - `obligation_network[]` — all ObligationNode records
  - `calendar[]` — merged player + NPC calendar entries
  - `pending_scene_cards[]` — scene cards generated but not yet delivered
  - `tariff_schedules[]`, `lod1_trade_offers[]`, `lod2_price_index` — trade infrastructure state
  - `lod1_national_stats` — per-LOD-1-nation statistics
  - `province_route_table` — precomputed route profiles (180 entries at 6 provinces)
  - `current_schema_version` (uint32) — for migration validation
  - `game_mode` — GameMode enum (ironman or standard)
  - `cross_province_delta_buffer` — must be empty at save time
- `config.persistence` — save format version, compression settings, autosave interval

## Outputs (to DeltaBuffer)
- This module does not write to DeltaBuffer during normal tick processing.
- **On save (snapshot)**: produces LZ4-compressed Protocol Buffers binary file at the save path, plus a companion `.summary` file containing SnapshotSummary (~204 bytes).
- **On WAL write**: appends delta entries to the current WAL segment file before they are applied to WorldState.
- **On load**: produces complete WorldState ready for tick execution; replaces current in-memory state. Applies schema migrations if needed.
- **On restore_with_consequences()** (Standard mode only): loads snapshot, increments `restoration_count`, generates TimelineDisruptionEvent, injects disruption consequences (memory entries, trust deltas, health events) via `inject_disruption_consequences()`.

## Preconditions
- **For snapshot save**: all modules have completed the current tick; WorldState is in a consistent post-tick state. `DeferredWorkQueue` has been drained for this tick. `CrossProvinceDeltaBuffer` is empty (all cross-province effects applied).
- **For WAL write**: delta entries are available before WorldState application (write-ahead semantics).
- **For load**: save file exists at specified path and passes header validation (magic bytes, schema version, checksum). File system has read permission.
- **For restore_with_consequences()**: `game_mode == GameMode::standard` (returns `RestoreResult::locked_ironman_mode` otherwise). No concurrent restoration in progress (returns `RestoreResult::already_restoring` on re-entrant call).

## Postconditions
- **After snapshot save**: binary file written containing complete WorldState. Companion `.summary` file written with SnapshotSummary (tick, date, net worth, exposure level, event labels). File size logged.
- **After WAL write**: delta entries persisted to disk before in-memory application. If game crashes mid-tick, WAL enables reconstruction from most recent snapshot.
- **After load**: WorldState fully restored. Running a tick from loaded state produces identical results to running the same tick from the original pre-save state.
- **After restore_with_consequences()**: snapshot loaded, `restoration_count` incremented, TimelineDisruptionEvent consequences injected (memory entries for affected NPCs, trust deltas for Tier 2+, NPC departures for Tier 3+, health events, calendar capacity modifiers). Current WAL segment marked `SUPERSEDED`; new segment opened.
- **Round-trip guarantee**: `serialize(deserialize(serialize(state))) == serialize(state)` (byte-identical output).

## Invariants
- **Round-trip determinism**: serialize-deserialize-serialize produces byte-identical output. This is the single most important invariant of this module and is tested on every CI run.
- **Save file format**: Protocol Buffers binary, LZ4-compressed. Schema version stored in the file for migration compatibility.
- **Snapshot cadence**: full snapshot every 30 ticks (one in-game month). LZ4-compressed. Background thread writes with copy-on-write WorldState; tick does not block on snapshot I/O.
- **WAL architecture**: series of segment files, one per inter-snapshot period (30 ticks). Each segment covers deltas between two snapshots. On restoration, current segment marked `SUPERSEDED`; retained for debugging but never replayed. WAL replay uses only the most recent non-superseded segment.
- **SnapshotSummary**: ~204 bytes per snapshot. 25-year playthrough = 300 months = ~61KB to load the full timeline list. Enables instant Timeline browser open without reading full 10MB snapshots.
- **Schema versioning**: `CURRENT_SCHEMA_VERSION` bumped on every migration-requiring change. Saves with `loaded_schema_version > CURRENT_SCHEMA_VERSION` rejected (`RestoreResult::schema_too_new`). Saves with older versions migrated in version order via `MigrationRegistry`.
- **Migration contract**: migrations are additive only. Add new fields with defaults; never delete data.
- **Consequence-aware restoration**: `restore_raw()` is a private method; CI enforces no external calls. Only `restore_with_consequences()` is callable from outside the module.
- **Disruption consequences determinism**: `restore_with_consequences()` uses `DeterministicRNG(world_seed, current_tick, 0xDEADFACE)` for all random draws within disruption event generation. Same run + same restoration point = same disruption pattern.
- **Ironman mode**: `restore_with_consequences()` returns `locked_ironman_mode`. No restoration API reachable from UI in Ironman mode. Snapshot browser is read-only.
- **Float serialization**: IEEE 754 binary representation stored directly via Protocol Buffers (not text conversion). Zero precision loss.
- **DeferredWorkQueue items**: serialized with absolute tick values (not relative). Loaded queue immediately valid without adjustment.
- **CrossProvinceDeltaBuffer**: must be empty at snapshot time. Assertion failure if non-empty at save.
- Platform-independent byte order (Protocol Buffers handles this natively).

## Failure Modes
- **Disk full during save**: partial file detected and deleted. Error reported to caller. WorldState unchanged. Autosave rescheduled.
- **Corrupt save file** (checksum mismatch or protobuf parsing error): load rejected with `RestoreResult::migration_failed` or equivalent. No state modification. Player informed via UI.
- **Schema version too new**: load rejected with `RestoreResult::schema_too_new`. UI shows: "This save requires a newer version of the game."
- **Schema migration failure**: load rejected with `RestoreResult::migration_failed`. Detailed error logged with source version and target version.
- **Memory allocation failure during deserialization**: load rejected. Error logged. Player informed.
- **WAL corruption**: detected by checksum on WAL segment entries. Corrupt entries skipped; recovery falls back to most recent valid snapshot. Data between snapshot and corruption point is lost (acceptable for crash recovery).
- **Concurrent restoration attempt**: returns `RestoreResult::already_restoring`. No state change. UI prevents double-click but API guard is the enforcement layer.

## Performance Contract
- **Snapshot save**: < 2s for full WorldState at V1 scale (~10MB uncompressed, 2,000 NPCs, 6 provinces). Background thread; tick not blocked.
- **Snapshot load**: < 5s for same save file (includes decompression + deserialization + validation + migration if needed).
- **Autosave (WAL)**: < 5ms per tick for delta append (write-ahead, not full serialization).
- **Snapshot compression**: LZ4 provides fast decompression. Compression ratio target: < 60% of uncompressed size.
- **SnapshotSummary load**: < 1ms for full timeline list (300 summaries at ~204 bytes each = ~61KB).
- Save/load does not run during tick execution; invoked between ticks only. Snapshot writes run on background thread.
- **World State size estimate at V1**: ~10MB (3,000 NPCs x 500 memories = ~6MB; relationship graph = ~2MB; markets + facilities + routes = ~2MB).

## Dependencies
- runs_after: ["world_state"]
- runs_before: []

## Test Scenarios
- `test_round_trip_byte_identical`: Create WorldState with known seed, run 100 ticks. Serialize to bytes. Deserialize. Re-serialize. Verify second serialization is byte-identical to first. This is the single most critical test.
- `test_rng_state_preserved_across_save_load`: Save after 50 ticks. Load. Run 50 more ticks. Compare with original continuous 100-tick run. Verify bit-identical WorldState at tick 100.
- `test_deferred_work_queue_preserved`: Queue items due at ticks 60, 75, 100. Save at tick 50. Load. Run to tick 110. Verify all items drained at correct ticks with correct payloads.
- `test_npc_memory_log_preserved`: NPCs with memory_log containing 500 entries each (at capacity). Save and load. Verify all 500 entries present per NPC with correct timestamps, types, emotional_weights, and decay values.
- `test_evidence_pool_preserved`: Evidence pool with 200 tokens of all EvidenceType variants. Save and load. Verify all tokens present with correct actionability scores and decay states.
- `test_corrupt_save_checksum_rejected`: Save valid file. Modify 1 byte in compressed payload. Attempt load. Verify checksum mismatch detected, `RestoreResult` indicates failure, WorldState unchanged.
- `test_schema_too_new_rejected`: Create save file with `schema_version = CURRENT_SCHEMA_VERSION + 1`. Attempt load. Verify `RestoreResult::schema_too_new` returned.
- `test_schema_migration_applied`: Create save at schema version 1. Register migration v1->v2 that adds a field with default. Load save. Verify migration applied and field has default value.
- `test_ironman_restore_blocked`: Set `game_mode = GameMode::ironman`. Call `restore_with_consequences()`. Verify `RestoreResult::locked_ironman_mode` returned and WorldState unchanged.
- `test_standard_restore_applies_disruption`: Set `game_mode = GameMode::standard`, `restoration_count = 0`. Call `restore_with_consequences()` for a valid snapshot. Verify: `restoration_count` incremented to 1, Tier 1 disruption consequences injected (3-8 affected NPCs with memory entries), health exhaustion spike applied.
- `test_float_precision_bit_exact`: Create WorldState with specific float values including edge cases (very small, very large, negative zero, subnormals). Save and load. Verify bit-exact IEEE 754 preservation via `memcmp`.
- `test_cross_province_delta_buffer_empty_assertion`: Set `CrossProvinceDeltaBuffer` with pending entries. Attempt snapshot save. Verify assertion failure or error (buffer must be empty at save time).
