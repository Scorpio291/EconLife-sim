# technology — Developer Context

## What This Module Does
Manages the R&D and Technology system: era transitions, technology maturation
advancement, domain knowledge tracking, and research project lifecycle.
Sequential (not province-parallel). See EconLife_RnD_and_Technology_v22.md.

## Tier: 1 (runs early, before production)

## Key Dependencies
- runs_after: ["calendar"]
- runs_before: ["production"]
- Reads: NPCBusiness.actor_tech_state, GlobalTechnologyState, TechnologyCatalog
- Writes: Era transitions, maturation updates, domain knowledge decay

## Critical Rules
- Era transitions are irreversible; eras advance forward only
- Maturation ceilings are era-gated — investment can exceed but cannot exceed cap
- Commercialization is irreversible
- All tech data is CSV-driven (technology_nodes.csv, maturation_ceilings.csv)
- Constants are config-driven (rnd_config.json)
- Deterministic: same seed + same inputs = same outputs

## Key Types (in technology_types.h)
- SimulationEra: 10 eras from year 2000 to 2250+
- ResearchDomain: 19 domains (12 V1 + 7 EX)
- TechHolding: per-actor ownership of a technology node
- GlobalTechnologyState: simulation-wide technology tracking
- TechnologyConfig: runtime constants loaded from JSON

## Data Files
- packages/base_game/technology/technology_nodes.csv
- packages/base_game/technology/maturation_ceilings.csv
- packages/base_game/config/rnd_config.json

## Interface Spec
- docs/design/EconLife_RnD_and_Technology_v22.md
