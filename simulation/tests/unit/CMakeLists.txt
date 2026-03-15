# Unit tests — module existence, individual module tests
add_executable(econlife_unit_tests
    module_exists_test.cpp
)

target_link_libraries(econlife_unit_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_core
)

catch_discover_tests(econlife_unit_tests)
