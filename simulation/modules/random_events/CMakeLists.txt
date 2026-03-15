add_library(econlife_random_events STATIC
    random_events_module.cpp
)
target_link_libraries(econlife_random_events PUBLIC econlife_core)
