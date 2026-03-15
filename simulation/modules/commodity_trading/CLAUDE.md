# commodity_trading — Developer Context

## What This Module Does
Processes speculative commodity positions: opening/closing positions,
margin calls, settlement. Affects price discovery through speculative
activity. NOT province-parallel (global market).

## Tier: 4

## Key Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]
- Reads: CommodityPosition list, spot prices, NPC capital
- Writes: NPCDelta (capital_delta from gains/losses), position state updates

## Critical Rules
- Two position types: long (betting price rises) and short (betting price falls)
- Margin calls triggered when position value drops below maintenance threshold
- Settlement is based on spot prices from price_engine
- Speculative volume feeds back into supply/demand (next tick's price calculation)

## Interface Spec
- docs/interfaces/commodity_trading/INTERFACE.md
