add_library(econlife_facility_signals STATIC
    facility_signals_module.cpp
)
target_link_libraries(econlife_facility_signals PUBLIC econlife_evidence)
