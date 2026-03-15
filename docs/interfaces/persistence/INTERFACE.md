# Module: persistence

## Purpose
Serializes and deserializes the complete WorldState for save/load functionality. The persistence module converts the entire in-memory simulation state into a compressed binary format suitable for disk storage, and restores it exactly on load. Round-trip determinism is the critical requirement: serialize → deserialize → serialize must produce byte-identical output. This guarantees that save/load does not introduce simulation divergence.

The module uses Protocol Buffers for structured serialization and LZ4 for compression. Save files include a header with version, seed, tick count, and checksum. The persistence module does not run during normal tick execution — it is invoked on explicit save/load requests and at autosave intervals.

## Inputs (from WorldState)
- `world_state` — the complete WorldState structure including all sub-structures:
  - `provinces` — all Province records with demographics, resources, infrastructure, conditions
  - `significant_npcs` — all NPC records with memory logs, knowledge maps, relationships, stats
  - `npc_businesses` — all business records with inventory, financials, workforce
  - `regional_markets` — all market data per (good_id × province_id)
  - `criminal_organizations` — all org records with operations, territory, OPSEC
  - `evidence_pool` — all evidence tokens
  - `obligation_network` — all obligation records
  - `deferred_work_queue` — all pending work items with due_ticks
  - `nations` — all nation records with LOD levels
  - `global_technology_state` — tech tree state
  - `calendar_state` — current date, era, season
  - `rng_state` — complete DeterministicRNG internal state (for reproducibility)
- `config.persistence` — save format version, compression level, autosave interval

## Outputs (to DeltaBuffer)
- This module does not write to DeltaBuffer during normal tick processing.
- On save: produces binary save file at specified path.
- On load: produces complete WorldState ready for tick execution; replaces current in-memory state.

## Preconditions
- For save: all modules have completed the current tick; WorldState is in a consistent post-tick state. DeferredWorkQueue is drained (no mid-processing items). CrossProvinceDeltaBuffer is empty (all cross-province effects applied).
- For load: save file exists at specified path and passes header validation (version, checksum).
- File system has write permission at save path.

## Postconditions
- After save: binary file written containing complete WorldState. File size logged. Checksum stored in header.
- After load: WorldState fully restored. Running a tick from loaded state produces identical results to running the same tick from the original state.
- Round-trip guarantee: `serialize(deserialize(serialize(state))) == serialize(state)` (byte-identical).

## Invariants
- Save file format: [Header (64 bytes)] [Compressed Protobuf Payload] [Footer Checksum (32 bytes)].
- Header contains: magic bytes ("ECON"), format_version (uint32), world_seed (uint64), current_tick (uint64), npc_count (uint32), province_count (uint32), uncompressed_size (uint64), compressed_size (uint64), reserved (remaining bytes zero-filled).
- Compression: LZ4 frame format. Compression level configurable (default: LZ4_HC level 9 for saves, LZ4 fast for autosaves).
- Checksum: XXH64 hash of the uncompressed protobuf payload. Verified on load; mismatch = corrupt save, load rejected.
- Protobuf schema version must match save file format_version. Migration path: older versions loaded by compatibility layer that upgrades schema.
- RNG state serialized completely: internal state array + stream position + fork history. Loaded RNG produces identical sequence from that point.
- Float serialization: IEEE 754 binary representation stored directly (not text). No precision loss.
- DeferredWorkQueue items serialized with absolute tick values (not relative). Loaded queue immediately valid.
- Save files are platform-independent (little-endian byte order enforced).
- All NPC memory logs serialized up to max_capacity per NPC (default 100 entries). Oldest pruned before save if over capacity.

## Failure Modes
- Disk full during save: partial file detected and deleted. Error reported to caller. WorldState unchanged.
- Corrupt save file (checksum mismatch): load rejected with error. No state modification. Player informed.
- Version mismatch (save file newer than code): load rejected. Player informed about version incompatibility.
- Version mismatch (save file older than code): migration attempted. If migration fails, load rejected with detailed error.
- Protobuf parsing error: load rejected. Corrupt or truncated payload.
- Memory allocation failure during deserialization (save too large): load rejected. Error logged.

## Performance Contract
- Save: < 500ms for full WorldState with 2000 NPCs, 6 provinces, 200 businesses.
- Load: < 1000ms for same save file (includes decompression + deserialization + validation).
- Autosave: < 200ms using LZ4 fast compression (lower ratio, faster speed).
- Save file size: < 50MB compressed for typical V1 game state.
- Save/load does not run during tick execution; invoked between ticks only.

## Dependencies
- runs_after: ["world_state"] (conceptual dependency; persistence reads WorldState)
- runs_before: [] (final module; nothing depends on persistence during tick)

## Test Scenarios
- `test_round_trip_determinism`: Create WorldState with known seed, run 100 ticks. Serialize → deserialize → serialize. Verify second serialization byte-identical to first.
- `test_rng_state_preserved`: Save after 50 ticks. Load. Run 50 more ticks. Compare with original continuous 100-tick run. Verify identical results.
- `test_deferred_work_queue_preserved`: Queue has items due at ticks 60, 75, 100. Save at tick 50. Load. Verify items drain at correct ticks.
- `test_npc_memory_preserved`: NPCs with memory logs containing 50 entries each. Save and load. Verify all memories present with correct timestamps and content.
- `test_evidence_pool_preserved`: Evidence pool with 200 tokens of various types. Save and load. Verify all tokens present with correct actionability scores.
- `test_corrupt_save_rejected`: Modify 1 byte in compressed payload. Attempt load. Verify checksum mismatch detected, load rejected.
- `test_version_mismatch_detected`: Create save with format_version = current + 1. Attempt load. Verify version mismatch error.
- `test_lz4_compression_ratio`: Save typical WorldState. Verify compressed size < 60% of uncompressed size.
- `test_float_precision_preserved`: WorldState with specific float values (including subnormals and edge cases). Save and load. Verify bit-exact float preservation.
- `test_autosave_faster_than_full_save`: Time autosave (LZ4 fast) vs full save (LZ4 HC). Verify autosave < 40% of full save time.
- `test_empty_worldstate_round_trip`: Minimal WorldState (no NPCs, no businesses, 1 province). Save and load. Verify round-trip determinism.
- `test_platform_independent_byte_order`: Verify save file uses little-endian byte order for all multi-byte values.
