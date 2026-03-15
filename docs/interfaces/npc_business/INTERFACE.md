# Module: npc_business

## Purpose
Executes quarterly strategic decisions for NPC-owned businesses: expansion/contraction, new product lines, hiring target adjustments, pricing strategy changes, R&D investment allocation, and market exit. Uses the business profile (cost_cutter, quality_player, fast_expander, defensive_incumbent) and board composition to determine decision quality. Province-parallel.

## Inputs (from WorldState)
- `npc_businesses[]` — id, sector, profile, cash, revenue_per_tick, cost_per_tick, market_share, strategic_decision_tick, dispatch_day_offset, province_id, criminal_sector, owner_id, regulatory_violation_severity
- `npc_businesses[].board_composition` — independence_score, member_npc_ids (decision quality modifier)
- `npc_businesses[].actor_tech_state` — technology portfolio for R&D investment decisions
- `npc_businesses[].executive_compensation` — mechanism, salary_per_tick (for cash flow planning)
- `npc_businesses[].scale` — BusinessScale determines available strategic options
- `npcs[]` — employees' skill levels and satisfaction (for hiring/firing decisions)
- `regional_markets[]` — spot_price, supply, demand_buffer per good per province (competitive positioning)
- `provinces[]` — economic_stress, infrastructure_rating, formal_employment_rate (environmental context)
- `current_tick` — compared against strategic_decision_tick to determine which businesses decide this tick
- `config.business` — cash_critical_months (2.0), cash_comfortable_months (3.0), cash_surplus_months (5.0), exit_market_threshold (0.05), expansion_return_threshold (0.15)

## Outputs (to DeltaBuffer)
- Business state updates via delta: cash changes (R&D spend, capacity investment, marketing spend), cost_per_tick adjustments (headcount changes, supplier contract renegotiation), revenue_per_tick projections, market_share adjustments, strategic_decision_tick advanced to next quarter
- `NPCDelta.capital_delta` — for workforce changes (severance on layoffs)
- `NPCDelta.new_status` — employment status changes when businesses hire or fire
- `NPCDelta.new_memory_entry` — layoff/hiring memories on affected NPCs
- `MarketDelta.supply_delta` — additive; capacity expansion increases future supply
- `MarketDelta.demand_delta` — additive; new product line entry creates demand signal
- `EvidenceDelta.new_token` — regulatory evidence from lobbying activities (defensive_incumbent profile)
- `ConsequenceDelta.new_entry` — market_exit consequences, acquisition triggers for bankrupt neighbors
- `ObligationNode` — created when fast_expander takes leverage for expansion (debt-financed growth)

## Preconditions
- price_engine has completed (current spot prices and equilibrium prices available).
- `current_tick >= npc_business.strategic_decision_tick` for businesses that will decide this tick.
- dispatch_day_offset ensures at most ~1/30th of businesses decide on any given tick (load-spreading).
- Player-owned businesses (owner_id == player_id) are skipped by this module; they are controlled via player commands.

## Postconditions
- Every NPC business where `current_tick >= strategic_decision_tick` has executed exactly one decision branch from its profile's strategic decision matrix.
- `strategic_decision_tick` advanced by TICKS_PER_QUARTER (90 ticks) for each business that decided.
- Businesses below exit_market_threshold (5% market share) with cost_cutter profile have a 30% chance per quarter of initiating market exit.
- Fast_expander businesses with cash > cash_comfortable_months of operating costs have initiated expansion or adjacent market entry.
- No business has spent more cash than it holds (all investment decisions capped at available cash minus working capital floor).
- R&D investment deltas emitted for quality_player and fast_expander profiles at their specified rates.

## Invariants
- Only one strategic decision per business per quarter. The dispatch_day_offset mechanism spreads load: hash(id) % 30 determines the offset day within each 30-tick month.
- Profile determines decision branch deterministically: same profile + same inputs = same decision.
- BusinessScale constrains available strategic options (micro businesses cannot lobby; large businesses have full option set).
- Board independence_score modifies decision quality: captured boards (< 0.25) rubber-stamp all decisions; independent boards may block risky expansion or excessive spending.
- Criminal sector businesses follow the same strategic matrix but use informal market prices for revenue and competitive calculations.
- Quarterly decisions do not produce immediate state changes — they emit deltas that are applied by the engine between tick steps.

## Failure Modes
- Invalid province_id on a business: log warning, skip that business, continue.
- Missing competitor data (no other businesses in sector/province): default to maintain_current_operations action.
- Circular acquisition attempt (business A tries to acquire B while B tries to acquire A): first-writer wins in delta application order.

## Performance Contract
- Province-parallel execution: < 3ms per province at ~333 businesses/province on a quarterly decision tick.
- Non-decision ticks (no businesses due): < 0.1ms (early exit after dispatch_day_offset check).
- Worst case (all businesses decide simultaneously, should not happen with offset spreading): < 20ms total.

## Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]

## Test Scenarios
- `test_cost_cutter_lays_off_when_cash_critical`: Cost_cutter business with cash < cash_critical_months * monthly_operating_costs. Verify 10% headcount reduction emitted as NPCDelta entries with layoff memory on affected NPCs.
- `test_fast_expander_enters_adjacent_market`: Fast_expander business with cash > cash_comfortable_months * monthly_costs and market_share > 0.30. Verify adjacent market entry action: supply_delta in new good/province, cash reduction for capacity investment.
- `test_defensive_incumbent_lobbies_on_new_entrant`: Defensive_incumbent detects new competitor entering market. Verify lobby_for_regulatory_barriers action emitted, creating a political ObligationNode.
- `test_quality_player_increases_rd_investment`: Quality_player with brand_rating < 0.6 and sufficient cash. Verify R&D spend of 5-10% of cash allocated, actor_tech_state investment delta emitted.
- `test_dispatch_offset_spreads_decisions`: 30 businesses with dispatch_day_offsets 0-29. Run 30 ticks. Verify exactly one business decides per tick (no clustering).
- `test_market_exit_probability`: Cost_cutter business with market_share = 0.03 (below exit threshold 0.05). Run 100 quarterly decisions with different seeds. Verify approximately 30% result in exit_market action.
- `test_player_owned_businesses_skipped`: Business with owner_id = player_id reaches strategic_decision_tick. Verify module does not execute any strategic decision for it; strategic_decision_tick is not advanced.
