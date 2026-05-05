add_library(econlife_technology STATIC
    technology_module.cpp
)
target_link_libraries(econlife_technology PUBLIC econlife_core)

econlife_set_warnings(econlife_technology)
econlife_maybe_werror(econlife_technology)
