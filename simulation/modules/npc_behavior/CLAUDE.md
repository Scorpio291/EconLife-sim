# npc_behavior — Developer Context

## What This Module Does
Core NPC AI module. Evaluates motivations, makes daily decisions (work,
shop, socialize, migrate), updates emotional state, processes memory
formation and decay, updates relationships. Province-parallel. Typically
the heaviest module per tick.

## Tier: 5

## Key Dependencies
- runs_after: ["financial_distribution"]
- runs_before: [] (end of Pass 1 chain)
- Reads: NPC state, MotivationVector, MemoryEntry list, Relationship graph
- Writes: NPCDelta (stress, satisfaction, health, capital), memory/relationship updates

## Critical Rules
- 8-element MotivationVector drives all NPC decisions
- Max 500 memory entries per NPC; oldest pruned on overflow
- Relationship recovery_ceiling prevents full trust restoration after betrayal
- Migration decisions use cross-province delta buffer (one-tick delay)
- Process NPCs in deterministic order within each province

## Interface Spec
- docs/interfaces/npc_behavior/INTERFACE.md
