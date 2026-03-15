# price_engine — Developer Context

## What This Module Does
Recalculates spot prices for all goods in all regional markets based on
current supply and demand. Applies price stickiness, momentum, and
government price floors/ceilings. Province-parallel.

## Tier: 3 (depends on supply_chain)

## Key Dependencies
- runs_after: ["supply_chain", "labor_market", "seasonal_agriculture"]
- runs_before: ["financial_distribution"]
- Reads: RegionalMarket supply/demand, price history, government policy
- Writes: MarketDelta (spot_price_override), price history updates

## Critical Rules
- Process in canonical order: good_id asc within each province
- Float accumulations use canonical sort to prevent IEEE 754 drift
- Price stickiness prevents wild swings — max change per tick is bounded
- Government price controls (floors/ceilings) override market clearing

## Interface Spec
- docs/interfaces/price_engine/INTERFACE.md
