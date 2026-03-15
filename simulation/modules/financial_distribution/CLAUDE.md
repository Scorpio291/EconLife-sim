# financial_distribution — Developer Context

## What This Module Does
Handles money flows: business revenue collection, wage payments to
employees, profit distribution to owners, dividend payments, tax
withholding. Province-parallel.

## Tier: 4 (depends on price_engine)

## Key Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]
- Reads: NPCBusiness revenue/costs, wage schedules, ownership, tax rates
- Writes: NPCDelta (capital_delta for wages/dividends), business cash updates

## Critical Rules
- Revenue = goods sold × spot price (from price engine results)
- Wages paid before dividends (labor has priority)
- Tax withholding happens at source
- Dividend distribution follows equity stake proportions
- Executive compensation includes equity grants with vesting schedules

## Interface Spec
- docs/interfaces/financial_distribution/INTERFACE.md
