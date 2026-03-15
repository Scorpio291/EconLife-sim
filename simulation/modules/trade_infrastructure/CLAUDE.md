# trade_infrastructure — Developer Context

## What This Module Does
Manages physical transport routes between provinces, processes transit
shipments (advancing goods in transit), handles route capacity and
bottlenecks, maintains transport cost models. NOT province-parallel.

## Tier: 3

## Key Dependencies
- runs_after: ["supply_chain"]
- runs_before: ["financial_distribution"]
- Reads: RouteProfile list, TransitShipment list, ProvinceLink data
- Writes: TransitShipment state updates, arrival scheduling via DeferredWorkQueue

## Critical Rules
- Four transport modes with different cost/speed profiles
- Route capacity limits can create bottlenecks
- Transit arrivals are DeferredWorkItems with calculated due_tick
- Cross-province shipments use CrossProvinceDeltaBuffer for arrival

## Interface Spec
- docs/interfaces/trade_infrastructure/INTERFACE.md
