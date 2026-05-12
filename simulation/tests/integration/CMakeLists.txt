# Integration tests — full-year cross-system interaction tests
add_executable(econlife_integration_tests
    full_year_test.cpp
    world_gen_integration_test.cpp
)

target_link_libraries(econlife_integration_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_modules
)

econlife_set_warnings(econlife_integration_tests)
econlife_maybe_werror(econlife_integration_tests)

# SKIP_RETURN_CODE 4 tells CTest that exit code 4 (Catch2 SKIP) is not a failure.
# WORKING_DIRECTORY is the project source root so relative paths in tests
# (e.g. "packages/base_game/goods" via find_goods_dir) resolve regardless of
# where ctest was invoked from. Without this, the goods CSV integration test
# previously silently SKIPped in CI because its relative-path search failed.
catch_discover_tests(econlife_integration_tests
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    PROPERTIES
        SKIP_RETURN_CODE 4
        LABELS "integration"
)
