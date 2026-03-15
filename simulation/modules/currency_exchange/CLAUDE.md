# currency_exchange — Developer Context

## What This Module Does
Manages currency exchange rates between nations based on trade balance, interest rates, and market sentiment.

## Tier: 11 | Sequential (global).

## Key Dependencies
- runs_after: [commodity_trading, government_budget]
- Reads: WorldState (see INTERFACE.md for specific fields)
- Writes: DeltaBuffer (see INTERFACE.md for specific deltas)

## Critical Rules
- Read INTERFACE.md before making changes
- All random draws through DeterministicRNG
- Floating-point accumulations in canonical sort order
- Process entities in deterministic order

## Interface Spec
- docs/interfaces/currency_exchange/INTERFACE.md
