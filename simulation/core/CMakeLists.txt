# Core library: world_state, tick orchestrator, RNG
# All simulation modules depend on this target.

add_library(econlife_core STATIC
    rng/deterministic_rng.cpp
    tick/tick_orchestrator.cpp
    tick/drain_deferred_work.cpp
    world_state/cross_province_delta_buffer.cpp
    world_state/apply_deltas.cpp
    world_gen/goods_catalog.cpp
    world_gen/recipe_catalog.cpp
    world_gen/facility_type_catalog.cpp
    world_gen/technology_catalog.cpp
    world_gen/world_generator.cpp
)

target_include_directories(econlife_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..        # simulation/ root for cross-module includes
)

target_compile_features(econlife_core PUBLIC cxx_std_20)
