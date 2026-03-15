# Module: lod_system

## Purpose
Manages Level of Detail (LOD) transitions for nations in the simulation. The player's home nation runs at LOD 0 (full simulation with all modules active across 6 provinces). Trade partner nations run at LOD 1 (simplified economic simulation producing price signals and trade volumes). The rest of the world runs at LOD 2 (statistical model generating global commodity prices and macro trends). The LOD system evaluates transition criteria and promotes or demotes nations between LOD levels based on player interaction, trade volume, and narrative significance.

LOD transitions are the key mechanism for keeping global simulation tractable while maintaining meaningful interaction with the wider world. A nation promoted from LOD 1 to LOD 0 triggers full module activation for its provinces; demotion reverses this. The module ensures transitions are smooth, deterministic, and do not cause discontinuities in economic signals.

## Inputs (from WorldState)
- `nations` — all nation records with current LOD level (0, 1, or 2), economic indicators, trade relationships
- `trade_flows` — inter-nation trade volume and balance; high trade volume with player's nation is primary LOD 1→0 promotion criterion
- `regional_conditions` — province-level stability and economic health for LOD 0 nations; feeds demotion evaluation
- `player_state` — player's business interests, diplomatic relationships, and investment in foreign nations; personal involvement triggers promotion consideration
- `provinces` — full province data for LOD 0 nations; simplified province summaries for LOD 1
- `global_market` — LOD 2 nations contribute to global commodity price index
- `current_tick` — LOD transitions evaluated on configurable cadence (default: every 30 ticks)
- `config.lod` — thresholds for promotion and demotion between LOD levels

## Outputs (to DeltaBuffer)
- `NationDelta.lod_level` — LOD level change for nations being promoted or demoted
- `NationDelta.province_activation` — for LOD 1→0 promotion: signal to activate full province simulation
- `NationDelta.province_deactivation` — for LOD 0→1 demotion: signal to deactivate full simulation, compress to summary
- `GlobalMarketDelta.price_index` — updated global commodity prices from LOD 2 statistical model
- `LOD1Delta.simplified_economy` — simplified economic output for LOD 1 nations (production, prices, trade offers)

## Preconditions
- Regional conditions module has completed; province stability indices are current for LOD 0 nations.
- Trade infrastructure module has completed; inter-nation trade volumes are current.
- Political cycle module has completed; diplomatic relationships are current.

## Postconditions
- All nations evaluated for LOD transition if evaluation cadence tick reached.
- At most one LOD transition per nation per evaluation cycle (prevents oscillation).
- LOD 0 nations have full simulation data current.
- LOD 1 nations have simplified economic output generated for trade partner interaction.
- LOD 2 nations have contributed to global price index via statistical model.
- No economic discontinuities from LOD transitions (smoothing applied over transition window).

## Invariants
- V1 scope: Player's home nation always LOD 0. Cannot be demoted.
- V1 scope: Maximum 1 additional nation at LOD 0 (expansion allows more).
- LOD 1→0 promotion criteria: `trade_volume_with_player > promotion_threshold AND (player_investment > investment_threshold OR diplomatic_significance > diplomatic_threshold)`.
- LOD 0→1 demotion criteria: `trade_volume_with_player < demotion_threshold AND player_investment < investment_threshold AND no_active_player_business_in_nation AND ticks_since_promotion > min_lod0_duration (default 365)`.
- Demotion threshold < promotion threshold (hysteresis prevents oscillation). Default: promote at 0.7, demote at 0.3.
- LOD 2 statistical model: `gdp_growth = base_growth + commodity_price_sensitivity * global_price_delta + rng.normal(0, volatility)`. Applied per tick for each LOD 2 nation.
- LOD 1 simplified economy: runs production, price_engine, and trade modules in simplified form (aggregate, not per-business).
- Transition smoothing: economic values interpolated over 30-tick window to prevent discontinuities.
- All random draws through DeterministicRNG.
- Nations processed in deterministic order: nation_id ascending.

## Failure Modes
- Promotion attempted but province data unavailable for target nation: defer promotion, log warning. Retry next evaluation cycle.
- Demotion of nation with active player businesses: blocked. Player must divest first.
- LOD 2 statistical model produces negative GDP: clamp to 0.0, nation enters economic crisis state.
- Too many LOD 0 nations would exceed performance budget: reject promotion, log warning.

## Performance Contract
- Sequential execution (not province-parallel; operates at nation level).
- LOD evaluation: < 5ms per evaluation cycle (every 30 ticks).
- LOD 1 simplified economy: < 10ms per LOD 1 nation per tick.
- LOD 2 statistical update: < 1ms for all LOD 2 nations combined.
- Must not exceed tick budget that would push total tick above 200ms.

## Dependencies
- runs_after: ["regional_conditions"]
- runs_before: ["persistence"]

## Test Scenarios
- `test_home_nation_always_lod0`: Player's home nation. Verify LOD level remains 0 regardless of conditions.
- `test_high_trade_volume_promotes_to_lod0`: Trade partner nation with trade_volume 0.8 (above 0.7 threshold). Verify promotion from LOD 1 to LOD 0.
- `test_low_trade_demotes_to_lod1`: LOD 0 nation with trade_volume dropped to 0.2, no player investment, 400 ticks since promotion. Verify demotion to LOD 1.
- `test_hysteresis_prevents_oscillation`: Nation with trade_volume oscillating between 0.5-0.6. Verify no repeated promotion/demotion cycles.
- `test_transition_smoothing`: Nation promoted from LOD 1 to LOD 0. Verify economic values smoothly interpolated over 30-tick window.
- `test_lod1_produces_trade_offers`: LOD 1 nation runs simplified economy. Verify production output and trade offers generated.
- `test_lod2_contributes_to_global_prices`: LOD 2 nations with commodity production. Verify global price index reflects their output.
- `test_max_lod0_nations_enforced`: Already at max LOD 0 nations. Additional promotion attempted. Verify rejected.
- `test_demotion_blocked_with_player_business`: LOD 0 nation where player owns a business. Verify demotion blocked.
- `test_lod2_negative_gdp_clamped`: LOD 2 nation with severe economic shock. Verify GDP clamped to 0.0, crisis state entered.
