cmake_minimum_required(VERSION 3.25)

# Allow older FetchContent dependencies (LZ4 v1.9.4) to configure
# without cmake_minimum_required warnings on newer CMake versions.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for FetchContent deps")

project(EconLife VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(ECONLIFE_BUILD_TESTS "Build test targets" ON)
option(ECONLIFE_BUILD_BENCHMARKS "Build benchmark targets" ON)
option(ECONLIFE_BUILD_TOOLS "Build developer tools" ON)

# ── Dependencies via FetchContent ────────────────────────────────────────────
include(FetchContent)

# Catch2 v3 — test framework
if(ECONLIFE_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
endif()

# LZ4 — snapshot compression
FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.9.4
    SOURCE_SUBDIR  build/cmake
)
FetchContent_MakeAvailable(lz4)

# Protocol Buffers — serialization
find_package(Protobuf QUIET)
if(NOT Protobuf_FOUND)
    message(STATUS "Protobuf not found on system; will be configured later")
endif()

# ── Dependency Direction Enforcement ─────────────────────────────────────────
# Custom rule: simulation/ targets must never depend on ui/ targets.
# This is enforced by the CI linter (.github/workflows/ci.yml) and by
# convention in CMake target_link_libraries declarations.

# ── Subdirectories ───────────────────────────────────────────────────────────
add_subdirectory(simulation)

if(ECONLIFE_BUILD_TOOLS)
    # add_subdirectory(tools)  # TODO: Enable when tool skeletons are ready
endif()

# UI is a separate Electron/TypeScript project — not built by CMake.
# See ui/README.md for build instructions.
