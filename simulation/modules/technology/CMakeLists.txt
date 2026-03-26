add_library(econlife_technology STATIC
    technology_module.cpp
)
target_link_libraries(econlife_technology PUBLIC econlife_core)
