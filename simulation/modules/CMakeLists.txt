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

# ── Pass 2: Tier 6–12 ────────────────────────────────────────────────────────

# Tier 6: Depends on NPC behavior
add_subdirectory(npc_spending)
add_subdirectory(evidence)
add_subdirectory(obligation_network)
add_subdirectory(community_response)

# Tier 7: Depends on evidence
add_subdirectory(facility_signals)
add_subdirectory(criminal_operations)
add_subdirectory(media_system)
add_subdirectory(antitrust)

# Tier 8: Depends on criminal operations
add_subdirectory(investigator_engine)
add_subdirectory(money_laundering)
add_subdirectory(drug_economy)
add_subdirectory(weapons_trafficking)
add_subdirectory(protection_rackets)

# Tier 9: Depends on investigator engine
add_subdirectory(legal_process)
add_subdirectory(informant_system)
add_subdirectory(alternative_identity)
add_subdirectory(designer_drug)

# Tier 10: Depends on community response
add_subdirectory(political_cycle)
add_subdirectory(influence_network)
add_subdirectory(trust_updates)
add_subdirectory(addiction)

# Tier 11: Mixed dependencies
add_subdirectory(regional_conditions)
add_subdirectory(population_aging)
add_subdirectory(currency_exchange)
add_subdirectory(lod_system)

# Tier 12: Persistence
add_subdirectory(persistence)
