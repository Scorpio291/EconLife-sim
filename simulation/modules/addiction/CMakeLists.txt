add_library(econlife_addiction STATIC
    addiction_module.cpp
)
target_link_libraries(econlife_addiction PUBLIC econlife_community_response)
