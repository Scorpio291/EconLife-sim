# labor_market — Developer Context

## What This Module Does
Matches unemployed NPCs to job postings, processes hiring/firing, adjusts
wages based on supply and demand. Uses three hiring channels: formal
postings, network referrals, direct approaches. Province-parallel.

## Tier: 2

## Key Dependencies
- runs_after: ["production"]
- runs_before: ["price_engine"]
- Reads: JobPosting list, NPC employment status, wage data, relationships
- Writes: NPCDelta (employer_business_id changes), business worker counts

## Critical Rules
- Three hiring channels: formal (broad), network (relationship-based), direct
- Wages adjust toward equilibrium based on vacancy rate
- Firing decisions come from business module, processed here
- Network hiring uses NPC relationship graph — deterministic traversal order

## Interface Spec
- docs/interfaces/labor_market/INTERFACE.md
