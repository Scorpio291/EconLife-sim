add_library(econlife_price_engine STATIC
    price_engine_module.cpp
)
target_link_libraries(econlife_price_engine PUBLIC
    econlife_core
    econlife_supply_chain
    econlife_labor_market
    econlife_seasonal_agriculture
)
