# Simulation modules — one subdirectory per tick module.
# Each module builds a static library that links against econlife_core.
# Build order follows dependency tiers.

# ── Pass 1: Tier 0–5 Core Modules ───────────────────────────────────────────
# Tier 0: Core infrastructure (built as part of econlife_core in simulation/core/)

# Tier 1: No inter-module dependencies
add_subdirectory(production)
add_subdirectory(calendar)
add_subdirectory(scene_cards)
add_subdirectory(random_events)

# Tier 2: Depends on production
add_subdirectory(supply_chain)
add_subdirectory(labor_market)
add_subdirectory(seasonal_agriculture)

# Tier 3: Depends on supply chain
add_subdirectory(price_engine)
add_subdirectory(trade_infrastructure)

# Tier 4: Depends on price engine
add_subdirectory(financial_distribution)
add_subdirectory(npc_business)
add_subdirectory(commodity_trading)
add_subdirectory(real_estate)

# Tier 5: Depends on financial distribution
add_subdirectory(npc_behavior)
add_subdirectory(banking)
add_subdirectory(government_budget)
add_subdirectory(healthcare)

# ── Pass 2: Tier 6–12 (added later) ─────────────────────────────────────────
