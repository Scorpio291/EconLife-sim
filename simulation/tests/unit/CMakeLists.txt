# Unit tests — module existence, individual module tests
add_executable(econlife_unit_tests
    module_exists_test.cpp
    rng_test.cpp
    deferred_work_queue_test.cpp
    tick_orchestrator_test.cpp
    production_test.cpp
    calendar_test.cpp
    scene_cards_test.cpp
    random_events_test.cpp
    supply_chain_test.cpp
    seasonal_agriculture_test.cpp
    labor_market_test.cpp
    price_engine_test.cpp
    trade_infrastructure_test.cpp
    financial_distribution_test.cpp
    npc_business_test.cpp
    commodity_trading_test.cpp
    real_estate_test.cpp
)

target_link_libraries(econlife_unit_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_core
    econlife_production
    econlife_calendar
    econlife_scene_cards
    econlife_random_events
    econlife_supply_chain
    econlife_seasonal_agriculture
    econlife_labor_market
    econlife_price_engine
    econlife_trade_infrastructure
    econlife_financial_distribution
    econlife_npc_business
    econlife_commodity_trading
    econlife_real_estate
)

catch_discover_tests(econlife_unit_tests)
