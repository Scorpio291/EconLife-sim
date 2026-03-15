# npc_business — Developer Context

## What This Module Does
Handles business-level decisions: expansion/contraction, new product
lines, hiring targets, pricing strategy. Uses board composition for
decision quality. Province-parallel.

## Tier: 4

## Key Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]
- Reads: NPCBusiness state, market conditions, BoardComposition
- Writes: Business state updates, hiring targets, expansion flags

## Critical Rules
- Business decisions are influenced by board composition quality
- Four business scales: micro, small, medium, large
- Expansion requires sufficient capital and favorable market conditions
- Board rejection mechanics prevent bad decisions (based on board quality)
- 14 business sectors with different behavior profiles

## Interface Spec
- docs/interfaces/npc_business/INTERFACE.md
