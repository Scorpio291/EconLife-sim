# world_state — Developer Context

## What This Module Does
The master simulation state container. Holds all data that the tick
modules read. Never modified mid-tick — modules write to DeltaBuffer,
which is applied between tick steps.

## Key Files
- `world_state.h` — WorldState struct with all simulation data
- `delta_buffer.h` — DeltaBuffer, NPCDelta, MarketDelta, etc.
- `npc.h` — NPC struct, NPCRole, MotivationVector, MemoryEntry
- `player.h` — PlayerCharacter, Trait enum, SkillDomain
- `geography.h` — Province, Nation, Region, ProvinceLink

## Critical Rules
- WorldState is ALWAYS passed as const reference to modules
- DeltaBuffer additive fields are summed; replacement fields use last-write-wins
- All additive fields are clamped to domain ranges after application
- `current_tick` monotonically increases; `world_seed` is immutable after init
- `cross_province_delta_buffer` must be empty at save time
- At V1 scale (~2,000 NPCs, 6 provinces): ~10-15MB. Always pass by reference.

## Dependency Direction
This is core infrastructure. Everything reads from WorldState.

## Interface Spec
- docs/interfaces/world_state/INTERFACE.md
