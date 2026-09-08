# scene_cards — Developer Context

## What This Module Does
Evaluates trigger conditions for pending scene cards, selects which to
present to the player, processes player choices into consequence deltas.
Primary player interaction mechanism.

## Tier: 1

## Key Dependencies
- runs_after: ["calendar"]
- runs_before: []
- Reads: pending_scene_cards, player state, NPC states, world conditions
- Writes: ConsequenceDelta from player choices, scene card state updates

## Critical Rules
- 27 SceneSetting categories define where scenes take place
- Scene cards have trigger conditions evaluated against world state
- Player choices produce consequences with variable delays
- Maximum one scene card presented per tick (player-facing constraint)
- Card copy is CONTENT, not code: dialogue, class, setting, choices and default
  outcome are authored in `packages/*/scene_cards/*.csv` and loaded into
  `SceneCardCatalog`. Producers emit a `SceneCardSeedDelta` naming a template;
  this module owns ids, template lookup, parameter injection and the caps.
  Never compose card prose at a call site — and never invent copy at runtime
  for a template that does not exist. A missing card is a visible content bug;
  an invented one is not.

## Interface Spec
- docs/interfaces/scene_cards/INTERFACE.md
