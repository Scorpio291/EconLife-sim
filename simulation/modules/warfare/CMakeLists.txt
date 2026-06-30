add_library(econlife_warfare STATIC
    warfare_module.cpp
)
target_link_libraries(econlife_warfare PUBLIC econlife_core)

econlife_set_warnings(econlife_warfare)
econlife_maybe_werror(econlife_warfare)
