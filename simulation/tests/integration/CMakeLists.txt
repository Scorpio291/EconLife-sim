# Integration tests — full-year cross-system interaction tests
add_executable(econlife_integration_tests
    full_year_test.cpp
    world_gen_integration_test.cpp
)

target_link_libraries(econlife_integration_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_modules
)

catch_discover_tests(econlife_integration_tests)
