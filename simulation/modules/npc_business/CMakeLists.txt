add_library(econlife_npc_business STATIC
    npc_business_module.cpp
)
target_link_libraries(econlife_npc_business PUBLIC econlife_core econlife_price_engine)
