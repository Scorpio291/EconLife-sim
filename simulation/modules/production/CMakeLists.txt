add_library(econlife_production STATIC
    production_module.cpp
)
target_link_libraries(econlife_production PUBLIC econlife_core)

econlife_set_warnings(econlife_production)
econlife_maybe_werror(econlife_production)
