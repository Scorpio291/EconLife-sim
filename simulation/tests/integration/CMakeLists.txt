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
#
# TIMEOUT is set here rather than per-lane on the ctest command line, because
# the same test has to fit under every toolchain. "Criminal justice loop
# closes: detection to imprisonment" runs ~207 s in an optimized local build;
# in the CI Debug cell it overran the lane's 300 s ceiling, and under
# ASan+UBSan it overran 600 s — both reported as failures of a test that was
# working, just slowly. A per-test property overrides the lane default, so one
# number covers Debug, Release, Windows and both sanitizer lanes. It is a
# hang-catcher, not a performance budget: the whole integration suite runs in
# ~580 s in Debug, so nothing legitimate comes close to it.
catch_discover_tests(econlife_integration_tests
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    PROPERTIES
        SKIP_RETURN_CODE 4
        LABELS "integration"
        TIMEOUT 1800
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
    # Raised from 600. Staffing decisions now reach Facility.worker_count, so
    # plants that used to sit half-manned run at capacity and the production
    # layer has correspondingly more to do: the suite went from ~7m45 to ~11m25
    # on the same machine. That is the cost of the mechanism working, not a
    # regression to tune away, but the gate has to allow for it.
    TIMEOUT 1800
)

# ---------------------------------------------------------------------------
# Player-loop suite — the player-facing counterpart to the emergence suite.
#
# Registered as a SINGLE ctest entry running the whole [player_loop] tag in one
# process, so the memoized play session is simulated once and shared across
# every ratchet rather than re-run per assertion.
#
#     ctest -L player_loop                                   (the ratchets)
#     ./econlife_player_loop_tests "[.player-loop-observe]"  (the human trace)
#
# It runs in the fast per-commit gate: these assertions are the ones that catch
# "the world is fine but the player cannot play", which is exactly the class of
# defect the module suites cannot see.
# ---------------------------------------------------------------------------
add_executable(econlife_player_loop_tests
    player_loop_test.cpp
    player_loop_observe.cpp
)

target_link_libraries(econlife_player_loop_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_modules
)

econlife_set_warnings(econlife_player_loop_tests)
econlife_maybe_werror(econlife_player_loop_tests)
econlife_relax_test_warnings(econlife_player_loop_tests)

add_test(NAME player_loop_suite
    COMMAND econlife_player_loop_tests "[player_loop]"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(player_loop_suite PROPERTIES
    LABELS "integration;player_loop"
    TIMEOUT 900
)
