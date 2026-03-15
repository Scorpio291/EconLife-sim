# Module: government_budget

## Purpose
Collects taxes at national, provincial, and city levels on a quarterly cycle (every 90 ticks), allocates spending across eight categories, processes intergovernmental transfers, manages deficit/surplus accumulation, and triggers fiscal stress consequences. NOT province-parallel because it operates at national scope across all jurisdictions.

## Inputs (from WorldState)
- `nation.national_budget` — GovernmentBudget at national level: revenue fields (revenue_own_taxes, revenue_transfers_in, revenue_other), spending_allocations and spending_actual maps by SpendingCategory, surplus_deficit, accumulated_debt, cash, debt_to_revenue_ratio, deficit_to_revenue_ratio
- `provinces[].provincial_budget` — GovernmentBudget per province
- `provinces[].city_budgets[]` — CityBudget per city within each province (V1: 1-3 cities per province)
- `provinces[].npc_businesses[]` — for corporate and business tax collection: revenue_per_tick, criminal_sector flag (criminal businesses excluded from formal taxation)
- `provinces[].cohort_stats.cohorts[]` — PopulationCohort data for income tax: median_income, size, employment_rate, group (for cohort_tax_rate_modifier)
- `nation.corporate_tax_rate` — national corporate tax rate
- `nation.income_tax_rate_top_bracket` — top bracket income tax rate
- `provinces[].provincial_business_tax_rate` — per-province business tax rate (set by scenario file or provincial legislation)
- `provinces[].property_tax_revenue_estimate` — placeholder property tax revenue per province until real estate market is fully specced
- `provinces[].infrastructure_rating` — current infrastructure condition (0.0-1.0); driven by spending
- `provinces[].conditions` — stability_score, crime_rate, inequality_index driven by spending consequences
- `current_tick` — for quarterly cycle detection (every 90 ticks)
- `config.fiscal.debt_warning_ratio` — 2.0; debt = 2x revenue triggers fiscal_pressure_warning
- `config.fiscal.debt_crisis_ratio` — 4.0; debt = 4x revenue triggers fiscal_crisis
- `config.fiscal.infrastructure_decay_per_quarter` — 0.01; rating falls per quarter without investment
- `config.fiscal.infrastructure_investment_scale` — 1,000,000; currency units per 0.01 rating improvement
- `config.fiscal.city_revenue_fraction_of_province` — 0.25; city collects ~25% of provincial revenue
- `config.fiscal.national_to_province_distribution_modifier` — default 1.0 (equal split); adjustable by head of state
- `config.fiscal.corruption_evidence_threshold` — 500,000; discretionary diversion above this creates high-actionability evidence
- `config.fiscal.cohort_tax_rate_modifier.*` — working_class: 0.40, professional: 0.85, corporate: 1.00, criminal_adjacent: 0.10

## Outputs (to DeltaBuffer)
- GovernmentBudget field updates for all levels (national, provincial, city):
  - `cash` adjusted by tax revenue inflows and spending outflows
  - `revenue_own_taxes` accumulated from quarterly tax collection
  - `revenue_transfers_in` credited from intergovernmental transfers
  - `total_revenue` recomputed as sum of revenue sources
  - `spending_actual` populated (may be less than spending_allocations if cash constrained)
  - `total_expenditure` recomputed as sum of spending_actual
  - `surplus_deficit` = total_revenue - total_expenditure
  - `accumulated_debt` carried forward from unfunded deficits
  - `debt_to_revenue_ratio` and `deficit_to_revenue_ratio` recomputed
- `region_deltas[]` — RegionDelta with:
  - `stability_delta` (additive) — from public_services underspend/overspend
  - `crime_rate_delta` (additive) — from law_enforcement spending changes
  - `inequality_delta` (additive) — from social_programs spending changes
- `npc_deltas[]` — NPCDelta with new_memory_entry for government employee NPCs when public_sector_wages are cut (employment_negative memories)
- `consequence_deltas[]` — fiscal_pressure_warning, fiscal_crisis, government_insolvency consequences queued when thresholds breached
- `evidence_deltas[]` — financial EvidenceToken when discretionary spending diversion exceeds corruption_evidence_threshold
- Province infrastructure_rating updates: decays 0.01/quarter without investment; increases by spending_actual[infrastructure] / infrastructure_investment_scale

## Preconditions
- financial_distribution has completed (business revenues and NPC incomes are current).
- All GovernmentBudget structs have valid spending_allocations (values >= 0.0 per category).
- Quarterly tax collection is triggered only when current_tick is a multiple of 90 (via DeferredWorkQueue WorkType::tax_collection_quarterly).
- Province.provincial_business_tax_rate and nation.corporate_tax_rate are non-negative.

## Postconditions
- On quarterly ticks: all tax revenue collected and credited to appropriate budget level.
- Intergovernmental transfers processed: national to provincial, provincial to city.
- Spending_actual computed for each category at each level: min(spending_allocations[category], available_cash_share).
- Infrastructure_rating updated for each province (decay applied, investment benefit applied, clamped to [0.0, 1.0]).
- Fiscal health indicators recomputed for all budgets.
- Fiscal stress consequences queued for any budget exceeding debt_warning_ratio or debt_crisis_ratio thresholds.
- Government insolvency consequence queued for any budget with cash < 0.
- All spending consequences propagated to province/city condition fields.

## Invariants
- total_revenue == revenue_own_taxes + revenue_transfers_in + revenue_other at all times.
- total_expenditure == sum(spending_actual) at all times.
- surplus_deficit == total_revenue - total_expenditure.
- debt_to_revenue_ratio == accumulated_debt / total_revenue (when total_revenue > 0).
- GovernmentBudget.cash can go negative (triggers insolvency consequence; not clamped).
- Criminal-sector businesses (criminal_sector == true) are excluded from formal tax collection.
- Intergovernmental transfers sum to the higher-level spending_allocations[intergovernmental] allocation.
- infrastructure_rating clamped to [0.0, 1.0] after all adjustments.
- Same seed + same inputs = identical budget output (deterministic).

## Failure Modes
- Province with zero NPC businesses: tax collection produces zero revenue; spending constraints apply; no crash.
- total_revenue == 0 and accumulated_debt > 0: debt_to_revenue_ratio set to infinity; fiscal_crisis consequence immediately queued.
- Spending_allocations sum exceeding cash: spending_actual pro-rated across categories proportionally; no overspend.
- GovernmentBudget with NaN in any float field: reset to 0.0, log error, flag for investigation.
- Missing city_budgets on a province: skip city-level processing for that province, log warning.

## Performance Contract
- Sequential execution (not province-parallel); national scope requires cross-province data.
- Quarterly processing (every 90 ticks): most ticks this module is a no-op or near-instant.
- On quarterly ticks: target < 10ms. Iterates over all provinces (6), all businesses (~200-500), all cohorts (~4-8 per province).
- Non-quarterly ticks: infrastructure decay and spending consequence checks only; target < 2ms.

## Dependencies
- runs_after: ["financial_distribution"]
- runs_before: [] (end of Pass 1 chain; no current-tick dependents)

## Test Scenarios
- `test_quarterly_national_tax_collection`: Set up 2 provinces with known NPC business revenues and cohort incomes. Run to tick 90. Verify national_budget.revenue_own_taxes equals the sum of corporate_tax (revenue_per_tick * 90 * corporate_tax_rate) plus income_tax (wage_base * income_tax_rate * cohort_modifier) for all businesses and cohorts.
- `test_intergovernmental_transfer_distributes_evenly`: Set national spending_allocations[intergovernmental] to 600,000. With 6 provinces and equal distribution_modifier (1.0), verify each province receives 100,000 in revenue_transfers_in and national cash decreases by 600,000.
- `test_infrastructure_decays_without_investment`: Set province infrastructure_rating to 0.80. Run one quarter with spending_actual[infrastructure] = 0. Verify infrastructure_rating dropped to 0.79 (0.80 - 0.01 decay).
- `test_cash_constrained_spending_prorates`: Set budget with cash = 50,000 and spending_allocations totaling 100,000. Verify spending_actual for each category is approximately half of its allocation (pro-rated) and total_expenditure does not exceed 50,000.
- `test_fiscal_crisis_triggers_at_debt_ratio_threshold`: Set budget with accumulated_debt = 5,000,000 and total_revenue = 1,000,000 (ratio = 5.0, above debt_crisis_ratio 4.0). Run quarterly check. Verify a fiscal_crisis consequence is queued with effects including infrastructure_decay_accelerated and grievance_spike.
- `test_criminal_businesses_excluded_from_tax`: Set up 3 businesses, one with criminal_sector = true. Run quarterly tax collection. Verify only the 2 legitimate businesses contributed to revenue_own_taxes.
- `test_discretionary_diversion_creates_evidence`: Set a regional_governor office holder directing 600,000 (above corruption_evidence_threshold of 500,000) in discretionary spending to a privately-owned contractor. Verify a financial EvidenceToken is created with actionability = 600,000 / 500,000 = 1.2 (clamped to 1.0).
