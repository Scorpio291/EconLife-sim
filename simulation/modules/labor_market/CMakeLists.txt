add_library(econlife_labor_market STATIC
    labor_market_module.cpp
)
target_link_libraries(econlife_labor_market PUBLIC econlife_core econlife_production)
