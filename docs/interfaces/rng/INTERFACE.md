# Module: rng

## Purpose
Deterministic random number generator used by all simulation systems. Guarantees that same seed + same call sequence = same outputs. Supports forking for province-parallel work.

## Inputs (from WorldState)
- `world_seed` — master seed for all RNG streams.

## Outputs (to DeltaBuffer)
- None. RNG is a utility; it produces values consumed by other modules.

## Preconditions
- Seed is set before first use.
- All callers use this RNG — never std::rand, system time, or thread ID.

## Postconditions
- State advances deterministically with each call.
- Forked RNGs produce independent, deterministic sub-streams.

## Invariants
- Same seed + same call sequence = identical outputs on any platform.
- Fork with same context_id always produces same sub-stream.
- No external entropy sources.

## Failure Modes
- N/A — RNG is a pure function of state. No failure modes.

## Performance Contract
- Single call (next_u64, next_double, next_float): < 10ns.
- Fork operation: < 50ns.

## Dependencies
- Core infrastructure — available before all modules.

## Test Scenarios
- `same_seed_same_output`: Create two RNGs with same seed. Verify 1000 calls produce identical results.
- `different_seeds_different_output`: Two different seeds produce different sequences.
- `fork_is_deterministic`: Fork with context_id=5, draw 100 values. Repeat. Verify identical.
- `fork_independence`: Fork with context_id=1 and context_id=2. Verify different streams.
- `distribution_uniformity`: Draw 100,000 values from next_double(). Verify approximately uniform in [0,1).
- `int_range_bounds`: next_int(5, 10) always returns values in [5, 10].
