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
