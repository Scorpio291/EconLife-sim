# real_estate — Developer Context

## What This Module Does
Manages property markets: rent collection, property sales, development
permits, housing supply/demand per province. Province-parallel.

## Tier: 4

## Key Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]
- Reads: PropertyListing list, province housing data, NPC capital
- Writes: NPCDelta (capital_delta from rent), property state updates

## Critical Rules
- Three property types with different market dynamics
- Rent collection happens every tick for occupied properties
- Property values correlate with province economic health
- Development permits gate new construction (supply response to demand)

## Interface Spec
- docs/interfaces/real_estate/INTERFACE.md
