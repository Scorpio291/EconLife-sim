add_library(econlife_energy_base STATIC
    energy_base_module.cpp
)
target_link_libraries(econlife_energy_base PUBLIC econlife_core)

econlife_set_warnings(econlife_energy_base)
econlife_maybe_werror(econlife_energy_base)
