add_library(econlife_subsistence STATIC
    subsistence_module.cpp
)
target_link_libraries(econlife_subsistence PUBLIC econlife_core)

econlife_set_warnings(econlife_subsistence)
econlife_maybe_werror(econlife_subsistence)
