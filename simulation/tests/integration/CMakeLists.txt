# Integration tests — full-year cross-system interaction tests
add_executable(econlife_integration_tests
    full_year_test.cpp
    world_gen_integration_test.cpp
    criminal_subsystem_integration_test.cpp
    warfare_integration_test.cpp
    premarket_genesis_integration_test.cpp
    history_gen_integration_test.cpp
    society_evolution_test.cpp
)

target_link_libraries(econlife_integration_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_modules
)

econlife_set_warnings(econlife_integration_tests)
econlife_maybe_werror(econlife_integration_tests)
econlife_relax_test_warnings(econlife_integration_tests)

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

# ---------------------------------------------------------------------------
# Emergence / behavioral suite — OPT-IN, label "emergence".
#
# Long-horizon (multi-year) runs through the full orchestrator that assert the
# simulation produces the emergent behavior the GDD promises (and ratchet the
# known-broken feedback loops via Catch [!shouldfail]). A single orchestrated
# multi-year run is ~tens of seconds, so this is NOT part of the fast per-commit
# gate. Run it explicitly:
#     ctest -L emergence                       (the assertion suite, one process)
#     ./econlife_emergence_tests "[.emergence-observe]"   (10-year human dump)
# The fast gate excludes it:  ctest -LE emergence
# ---------------------------------------------------------------------------
add_executable(econlife_emergence_tests
    emergence_observe.cpp
    emergence_test.cpp
    society_evolution_observe.cpp
)

target_link_libraries(econlife_emergence_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_modules
)

econlife_set_warnings(econlife_emergence_tests)
econlife_maybe_werror(econlife_emergence_tests)
econlife_relax_test_warnings(econlife_emergence_tests)

# Register as a SINGLE ctest entry running the whole [emergence] tag in one
# process, so the memoized shared world-run is reused across assertions (one run
# for all guards + ratchets, plus one independent run for the determinism check)
# instead of re-running per test case.
add_test(NAME emergence_behavioral_suite
    COMMAND econlife_emergence_tests "[emergence]"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(emergence_behavioral_suite PROPERTIES
    LABELS "emergence"
    TIMEOUT 600
)
