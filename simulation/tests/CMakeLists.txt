# Test targets — unit, integration, determinism, scenarios, benchmarks

add_subdirectory(unit)
add_subdirectory(determinism)
add_subdirectory(scenarios)
add_subdirectory(integration)

if(ECONLIFE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
