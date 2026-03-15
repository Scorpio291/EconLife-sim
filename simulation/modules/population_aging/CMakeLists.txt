add_library(econlife_population_aging STATIC
    population_aging_module.cpp
)
target_link_libraries(econlife_population_aging PUBLIC econlife_healthcare)
