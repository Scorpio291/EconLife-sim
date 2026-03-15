# Scenario tests — behavioral assertions from GDD and TDD
add_executable(econlife_scenario_tests
    economy_scenarios.cpp
    npc_scenarios.cpp
    tdd_scenarios.cpp
)

target_link_libraries(econlife_scenario_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_core
)

catch_discover_tests(econlife_scenario_tests)
