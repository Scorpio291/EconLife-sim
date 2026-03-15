# healthcare — Developer Context

## What This Module Does
Processes NPC health changes: disease spread, treatment outcomes, hospital
capacity, healthcare access based on province funding. Province-parallel.

## Tier: 5

## Key Dependencies
- runs_after: ["financial_distribution"]
- runs_before: [] (end of Pass 1 chain)
- Reads: HealthcareProfile, NPC health state, province healthcare funding
- Writes: NPCDelta (health_delta, satisfaction_delta), healthcare state updates

## Critical Rules
- Hospital capacity limits treatment availability
- Healthcare quality depends on government_budget spending allocation
- Disease spread uses province density and NPC interaction graph
- Treatment outcomes are probabilistic via DeterministicRNG

## Interface Spec
- docs/interfaces/healthcare/INTERFACE.md
