add_library(econlife_knowledge STATIC
    knowledge_module.cpp
)
target_link_libraries(econlife_knowledge PUBLIC econlife_core)

econlife_set_warnings(econlife_knowledge)
econlife_maybe_werror(econlife_knowledge)
