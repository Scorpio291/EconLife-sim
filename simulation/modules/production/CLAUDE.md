# production — Developer Context

## What This Module Does
Processes all NPCBusiness entities each tick: consumes input goods from
inventory based on recipes, produces output goods, applies worker
productivity multipliers. Province-parallel.

## Tier: 1 (depends only on Tier 0)

## Key Dependencies
- runs_after: [] (first domain module)
- runs_before: ["supply_chain"]
- Reads: NPCBusiness inventory, recipe definitions, worker count
- Writes: MarketDelta (supply changes), business inventory updates

## Critical Rules
- Production only runs if business has sufficient input goods
- Worker count affects throughput, not recipe ratios
- Recipes are data-driven from packages/base_game/recipes/
- Process businesses in deterministic order (business_id asc)

## Interface Spec
- docs/interfaces/production/INTERFACE.md
