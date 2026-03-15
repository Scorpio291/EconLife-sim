# banking — Developer Context

## What This Module Does
Processes loan applications, collects loan payments, adjusts credit
scores, handles defaults. Interest rates respond to economic conditions.
Province-parallel.

## Tier: 5

## Key Dependencies
- runs_after: ["financial_distribution"]
- runs_before: [] (end of Pass 1 chain)
- Reads: CreditProfile, LoanRecord list, NPC capital, interest rates
- Writes: NPCDelta (capital_delta for loan proceeds/payments), credit updates

## Critical Rules
- Four loan purposes: business_startup, business_expansion, property, personal
- Credit score derived from payment history and capital ratio
- Default triggers collateral seizure and credit score penalty
- Interest rates float with economic conditions but have floor/ceiling

## Interface Spec
- docs/interfaces/banking/INTERFACE.md
