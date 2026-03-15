add_library(econlife_media_system STATIC
    media_system_module.cpp
)
target_link_libraries(econlife_media_system PUBLIC econlife_evidence)
