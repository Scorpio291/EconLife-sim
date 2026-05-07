# Benchmark tests — performance regression detection
add_executable(econlife_benchmarks
    tick_benchmark.cpp
)

target_link_libraries(econlife_benchmarks PRIVATE
    Catch2::Catch2WithMain
    econlife_modules
)

econlife_set_warnings(econlife_benchmarks)
econlife_maybe_werror(econlife_benchmarks)

catch_discover_tests(econlife_benchmarks
    PROPERTIES LABELS "benchmark"
)
