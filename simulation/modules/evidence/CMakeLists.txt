add_library(econlife_evidence STATIC
    evidence_module.cpp
)
target_link_libraries(econlife_evidence PUBLIC econlife_npc_behavior)

econlife_set_warnings(econlife_evidence)
econlife_maybe_werror(econlife_evidence)
