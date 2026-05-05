add_library(econlife_banking STATIC
    banking_module.cpp
)
target_link_libraries(econlife_banking PUBLIC econlife_core econlife_financial_distribution)

econlife_set_warnings(econlife_banking)
econlife_maybe_werror(econlife_banking)
