add_library(econlife_lod_system STATIC
    lod_system_module.cpp
)
target_link_libraries(econlife_lod_system PUBLIC econlife_regional_conditions)
