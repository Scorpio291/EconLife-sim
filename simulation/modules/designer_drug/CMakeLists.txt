add_library(econlife_designer_drug STATIC
    designer_drug_module.cpp
)
target_link_libraries(econlife_designer_drug PUBLIC econlife_investigator_engine)
