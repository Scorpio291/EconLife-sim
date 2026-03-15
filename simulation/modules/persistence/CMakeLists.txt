add_library(econlife_persistence STATIC
    persistence_module.cpp
)
target_link_libraries(econlife_persistence PUBLIC econlife_core)
