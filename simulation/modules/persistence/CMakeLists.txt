add_library(econlife_persistence STATIC
    persistence_module.cpp
)
target_link_libraries(econlife_persistence PUBLIC econlife_core)
target_link_libraries(econlife_persistence PRIVATE lz4_static)
target_include_directories(econlife_persistence PRIVATE ${lz4_SOURCE_DIR}/lib)
