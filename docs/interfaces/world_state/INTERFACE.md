# Module: world_state

## Purpose
The master simulation state container. Holds all simulation data and provides the read-only view that tick modules consume. Manages DeltaBuffer application between tick steps.

## Inputs (from WorldState)
- N/A — world_state IS the data. It is read by all modules via const reference.

## Outputs (to DeltaBuffer)
- N/A — world_state receives applied deltas between tick steps.

## Preconditions
- Initialized from a save file or fresh world generation before first tick.
- All vector capacities pre-reserved based on NPC count and province count.

## Postconditions
- After DeltaBuffer application: all additive deltas summed and clamped to domain ranges.
- Replacement fields overwritten (last write wins within a tick step).

## Invariants
- WorldState is never modified mid-tick-step. Modules read const reference.
- `current_tick` monotonically increases.
- `world_seed` is immutable after initialization.
- `game_mode` is immutable after game creation.
- `cross_province_delta_buffer` is empty at save time.
- At V1 scale (~2,000 NPCs, 6 provinces, ~50 goods): ~10-15MB. Always pass by reference.

## Failure Modes
- Schema version mismatch on load: return `LoadResult::schema_too_new`.
- Corrupted save: return load error; do not partially initialize.

## Performance Contract
- DeltaBuffer application (merge step): < 5ms at 2,000 NPCs.
- Pre-reserve vectors at initialization to avoid per-tick allocation.

## Dependencies
- Core infrastructure — loaded before all modules.

## Test Scenarios
- `delta_additive_fields_sum_correctly`: Apply two NPCDeltas with capital_delta to same NPC. Verify sum.
- `delta_additive_fields_clamp_to_range`: Apply health_delta that would exceed 1.0. Verify clamped.
- `delta_replacement_last_write_wins`: Apply two MarketDeltas with spot_price_override. Verify last value.
- `worldstate_survives_serialization_roundtrip`: Serialize, deserialize, compare all fields.
- `cross_province_buffer_empty_at_save`: Run tick, save, verify buffer is empty.
