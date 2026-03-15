add_library(econlife_trust_updates STATIC
    trust_updates_module.cpp
)
target_link_libraries(econlife_trust_updates PUBLIC econlife_community_response)
