# seasonal_agriculture — Developer Context

## What This Module Does
Manages crop growth cycles: plant → grow → harvest → fallow. Applies
weather and climate effects, determines yields based on soil quality and
farm investment. Province-parallel.

## Tier: 2

## Key Dependencies
- runs_after: ["production"]
- runs_before: ["price_engine"]
- Reads: FarmSeasonState, province ClimateProfile, calendar season
- Writes: MarketDelta (agricultural good supply), farm state updates

## Critical Rules
- Seven crop categories, four season phases
- Agricultural goods have seasonal availability (unlike industrial production)
- Climate zone (Koppen) affects which crops grow in each province
- Weather events from random_events can damage crops mid-season

## Interface Spec
- docs/interfaces/seasonal_agriculture/INTERFACE.md
