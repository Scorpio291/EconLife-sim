add_library(econlife_business_lifecycle STATIC
    business_lifecycle_module.cpp
)
target_link_libraries(econlife_business_lifecycle PUBLIC econlife_core)
