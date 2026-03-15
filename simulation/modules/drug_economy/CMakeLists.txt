add_library(econlife_drug_economy STATIC
    drug_economy_module.cpp
)
target_link_libraries(econlife_drug_economy PUBLIC econlife_criminal_operations)
