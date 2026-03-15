add_library(econlife_healthcare STATIC
    healthcare_module.cpp
)
target_link_libraries(econlife_healthcare PUBLIC econlife_core econlife_financial_distribution)
