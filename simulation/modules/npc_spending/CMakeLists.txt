add_library(econlife_npc_spending STATIC
    npc_spending_module.cpp
)
target_link_libraries(econlife_npc_spending PUBLIC econlife_npc_behavior)
