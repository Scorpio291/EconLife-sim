# government_budget — Developer Context

## What This Module Does
Collects taxes, allocates spending across 8 categories, manages deficit
and surplus. Operates at national and city level. NOT province-parallel
(national scope).

## Tier: 5

## Key Dependencies
- runs_after: ["financial_distribution"]
- runs_before: [] (end of Pass 1 chain)
- Reads: GovernmentBudget, tax revenue, GDP indicators, spending requests
- Writes: RegionDelta (service levels), budget state updates

## Critical Rules
- 8 spending categories: healthcare, education, infrastructure, defense,
  welfare, law_enforcement, administration, debt_service
- Tax collection based on income/sales/property/corporate rates
- Budget deficit increases borrowing costs (feedback loop)
- Spending levels directly affect province service quality

## Interface Spec
- docs/interfaces/government_budget/INTERFACE.md
