add_library(econlife_legal_process STATIC
    legal_process_module.cpp
)
target_link_libraries(econlife_legal_process PUBLIC econlife_investigator_engine)
