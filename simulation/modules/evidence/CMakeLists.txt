add_library(econlife_evidence STATIC
    evidence_module.cpp
)
target_link_libraries(econlife_evidence PUBLIC econlife_npc_behavior)
