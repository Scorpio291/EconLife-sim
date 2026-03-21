# Integration tests — full-year cross-system interaction tests
add_executable(econlife_integration_tests
    full_year_test.cpp
)

target_link_libraries(econlife_integration_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_core
    econlife_persistence
)

catch_discover_tests(econlife_integration_tests)
