# supply_chain — Developer Context

## What This Module Does
Matches buy orders to sell offers, creates transit shipments along trade
routes, handles local vs inter-province fulfillment. Province-parallel
for local matching; cross-province trade uses CrossProvinceDeltaBuffer.

## Tier: 2 (depends on production)

## Key Dependencies
- runs_after: ["production"]
- runs_before: ["price_engine"]
- Reads: GoodOffer lists, business inventories, trade routes
- Writes: MarketDelta (demand fulfillment), TransitShipment creation

## Critical Rules
- Local fulfillment first, then inter-province
- Inter-province trade goes through CrossProvinceDeltaBuffer (one-tick delay)
- Transit shipments are scheduled via DeferredWorkQueue with arrival time
- Match orders in canonical order for determinism

## Interface Spec
- docs/interfaces/supply_chain/INTERFACE.md
