add_library(econlife_player_actions STATIC
    player_actions_module.cpp
)
target_link_libraries(econlife_player_actions PUBLIC econlife_core)
