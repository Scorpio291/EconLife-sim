# Determinism tests — verify identical output across runs and core counts
add_executable(econlife_determinism_tests
    determinism_test.cpp
)

target_link_libraries(econlife_determinism_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_modules
)

catch_discover_tests(econlife_determinism_tests
    PROPERTIES LABELS "determinism"
)

# Cross-platform determinism harness (issue #21).
# Standalone CLI: dumps a SHA-256 of the post-N-tick WorldState to a file.
# CI runs this on every (os, build_type) matrix cell and asserts the
# resulting hashes match across platforms — catches divergence that the
# per-platform determinism_test.cpp can't see.
add_executable(econlife_golden_dump
    golden_state_dump.cpp
)

target_link_libraries(econlife_golden_dump PRIVATE
    econlife_modules
)

target_include_directories(econlife_golden_dump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

econlife_set_warnings(econlife_golden_dump)
econlife_maybe_werror(econlife_golden_dump)
