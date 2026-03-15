add_library(econlife_scene_cards STATIC
    scene_cards_module.cpp
)
target_link_libraries(econlife_scene_cards PUBLIC econlife_core)
