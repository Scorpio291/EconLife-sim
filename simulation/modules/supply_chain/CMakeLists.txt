add_library(econlife_supply_chain STATIC
    supply_chain_module.cpp
)
target_link_libraries(econlife_supply_chain PUBLIC econlife_core econlife_production)
