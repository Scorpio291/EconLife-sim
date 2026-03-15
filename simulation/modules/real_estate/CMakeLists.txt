add_library(econlife_real_estate STATIC
    real_estate_module.cpp
)
target_link_libraries(econlife_real_estate PUBLIC econlife_core econlife_price_engine)
