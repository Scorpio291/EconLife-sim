# Module: commodity_trading

## Purpose
Processes speculative commodity positions for players and NPCs: opening new long/short positions, closing existing positions with P&L settlement, applying market impact from large positions to regional supply/demand, and flagging realized gains for capital gains tax collection. NOT province-parallel (global market scope).

## Inputs (from WorldState)
- `commodity_positions[]` — id, actor_id, good_id, province_id, position_type (long/short), quantity, entry_price, opened_tick, exit_tick, realised_pnl
- `regional_markets[]` — spot_price per good per province (for current valuation and settlement), supply per good per province (for market impact threshold calculation)
- `player_character` — wealth (for position opening cost validation and settlement credits/debits)
- `npcs[]` — capital (for NPC trader position funding)
- `current_tick` — used for exit_tick stamping on position close
- `config.trading` — market_impact_threshold (0.10), market_impact_scale (0.05)

## Outputs (to DeltaBuffer)
- `PlayerDelta.wealth_delta` — additive; debited on position open (cost = quantity * spot_price), credited on position close (long: quantity * exit_price; short: quantity * entry_price - quantity * exit_price)
- `NPCDelta.capital_delta` — additive; same open/close mechanics for NPC traders
- `MarketDelta.demand_buffer_delta` — additive; large long positions (quantity > supply * market_impact_threshold) increase demand: demand *= (1.0 + market_impact_scale * quantity / supply)
- `MarketDelta.supply_delta` — additive; large short positions create equivalent supply-side pressure
- New CommodityPosition records appended to WorldState.commodity_positions on open
- Existing CommodityPosition records updated: exit_tick and realised_pnl set on close
- Capital gains flag: realised_pnl > 0 marks position for collection at next provincial tax step (per SS31)

## Preconditions
- price_engine has completed for this tick (spot prices are current and stable for the tick).
- All open positions reference valid good_id and province_id combinations with existing RegionalMarket entries.
- Actor wealth/capital is non-negative before position processing begins.
- Position open/close commands have been queued (player via player action queue; NPC via behavior engine decisions from prior tick).

## Postconditions
- All queued open_commodity_position commands with sufficient actor funds have created new CommodityPosition entries with entry_price = current spot_price and opened_tick = current_tick.
- All queued close_commodity_position commands have settled: realised_pnl computed, exit_tick set, actor wealth/capital credited or debited.
- Market impact deltas emitted for positions exceeding the market_impact_threshold fraction of regional supply.
- No position has been opened with insufficient funds (rejected commands logged but not executed).
- Insufficient-fund rejections do not alter any state.

## Invariants
- Long position P&L: realised_pnl = (exit_price - entry_price) * quantity.
- Short position P&L: realised_pnl = (entry_price - exit_price) * quantity.
- exit_tick == 0 while position is open; set to current_tick on close.
- realised_pnl == 0.0 while position is open; computed only on close.
- current_value is derived (quantity * spot_price) and never persisted; recomputed at read time.
- V1 simplification: short positions have no borrowing mechanism and no margin call mechanic. Short is a deferred sell at entry_price settled at market exit.
- Deterministic: same seed + same position commands + same spot prices = identical results.
- Market impact is symmetric: large buys increase demand, large sells increase supply pressure.

## Failure Modes
- Position references invalid good_id or province_id: log error, skip position, continue processing.
- Actor wealth goes negative after settlement (should not happen due to precondition check): clamp to 0.0, log warning.
- Spot price is 0.0 or negative for a good: reject all new positions for that good this tick, log error.
- Duplicate position close command (exit_tick already non-zero): skip, log warning.

## Performance Contract
- NOT province-parallel: runs on main thread sequentially.
- Total module budget: < 5ms at full V1 scale (~200 active positions across 2,000 NPCs, most NPCs do not trade commodities).
- Per-position settlement: < 0.02ms.

## Dependencies
- runs_after: ["price_engine"]
- runs_before: ["npc_behavior"]

## Test Scenarios
- `test_long_position_open_and_close_profit`: Open long position: 100 units at spot_price=50. Close when spot_price=70. Verify realised_pnl = (70-50)*100 = 2,000. Player wealth increased by 7,000 (proceeds), net gain 2,000 after subtracting 5,000 cost.
- `test_short_position_profit_on_price_decline`: Open short position: 50 units at spot_price=100. Close when spot_price=80. Verify realised_pnl = (100-80)*50 = 1,000. Player wealth net change = +1,000.
- `test_insufficient_funds_rejects_open`: Player wealth=1,000. Attempt open long: 100 units at spot_price=50 (cost=5,000). Verify position not created, wealth unchanged, rejection logged.
- `test_market_impact_on_large_position`: RegionalMarket supply=1,000 for good_id=5. Open long position: 200 units (20% of supply, above market_impact_threshold=10%). Verify demand_buffer_delta applied: demand *= (1.0 + 0.05 * 200/1000) = demand * 1.01.
- `test_no_market_impact_below_threshold`: Open long position: 50 units with supply=1,000 (5% of supply, below 10% threshold). Verify no demand_buffer_delta emitted.
- `test_capital_gains_flagged_for_tax`: Close profitable long position with realised_pnl=500. Verify position is flagged for capital gains tax collection at next provincial tax step.
- `test_npc_trader_seasonal_arbitrage`: NPC trader opens long position at harvest low (spot_price depressed). Closes at pre-harvest peak. Verify P&L correctly computed using NPC capital_delta, not player wealth_delta.
