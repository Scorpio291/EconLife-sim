# Core library: world_state, tick orchestrator, RNG
# All simulation modules depend on this target.

add_library(econlife_core STATIC
    # Headers are included via target_include_directories.
    # Source files will be added as implementations are written.
    # For now, a placeholder source to allow the library to link.
    core_placeholder.cpp
)

target_include_directories(econlife_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..        # simulation/ root for cross-module includes
)

target_compile_features(econlife_core PUBLIC cxx_std_20)
