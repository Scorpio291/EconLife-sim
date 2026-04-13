# Simulation modules — one subdirectory per tick module.
# Each module builds a static library that links against econlife_core.
# Build order follows dependency tiers.

# ── Pass 1: Tier 0–5 Core Modules ───────────────────────────────────────────
# Tier 0: Core infrastructure (built as part of econlife_core in simulation/core/)

# Player action processing — runs before all domain modules
add_subdirectory(player_actions)

# Tier 1: No inter-module dependencies
add_subdirectory(business_lifecycle)
add_subdirectory(production)
add_subdirectory(calendar)
add_subdirectory(scene_cards)
add_subdirectory(random_events)
add_subdirectory(technology)

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

# Module registration — links all module libraries into a single target.
add_library(econlife_modules STATIC
    register_base_game_modules.cpp
)

target_include_directories(econlife_modules PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..        # simulation/ root
)

target_link_libraries(econlife_modules PUBLIC
    econlife_core
    econlife_player_actions
    econlife_business_lifecycle
    econlife_calendar
    econlife_production
    econlife_technology
    econlife_scene_cards
    econlife_random_events
    econlife_supply_chain
    econlife_labor_market
    econlife_seasonal_agriculture
    econlife_price_engine
    econlife_trade_infrastructure
    econlife_financial_distribution
    econlife_npc_business
    econlife_commodity_trading
    econlife_real_estate
    econlife_npc_behavior
    econlife_banking
    econlife_government_budget
    econlife_healthcare
    econlife_npc_spending
    econlife_evidence
    econlife_obligation_network
    econlife_community_response
    econlife_facility_signals
    econlife_criminal_operations
    econlife_media_system
    econlife_antitrust
    econlife_investigator_engine
    econlife_money_laundering
    econlife_drug_economy
    econlife_weapons_trafficking
    econlife_protection_rackets
    econlife_legal_process
    econlife_informant_system
    econlife_alternative_identity
    econlife_designer_drug
    econlife_political_cycle
    econlife_influence_network
    econlife_trust_updates
    econlife_addiction
    econlife_regional_conditions
    econlife_population_aging
    econlife_currency_exchange
    econlife_lod_system
    econlife_persistence
)
