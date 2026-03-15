add_library(econlife_banking STATIC
    banking_module.cpp
)
target_link_libraries(econlife_banking PUBLIC econlife_core econlife_financial_distribution)
