cmake_minimum_required(VERSION 3.25)

# Allow older FetchContent dependencies (LZ4 v1.9.4) to configure
# without cmake_minimum_required warnings on newer CMake versions.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for FetchContent deps")

project(EconLife VERSION 0.1.0 LANGUAGES CXX C)

# ── Project warning baseline ─────────────────────────────────────────────────
# Helpers that are explicitly applied to project targets only — never to
# FetchContent dependencies (Catch2, lz4, h3, nlohmann_json). Each project
# library/executable in simulation/** must call econlife_set_warnings() and
# econlife_maybe_werror().
function(econlife_set_warnings target)
    if(MSVC)
        # /permissive- is intentionally NOT enabled here: it surfaces real
        # conformance issues but our existing code has not been audited for
        # MSVC-strict mode (typename in dependent contexts, two-phase lookup,
        # etc.). Add it once the warnings baseline has been triaged and the
        # codebase compiles cleanly under /W4.
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
            -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
            -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
    endif()
endfunction()

option(ECONLIFE_WARN_AS_ERROR "Treat compiler warnings as errors" OFF)
function(econlife_maybe_werror target)
    if(NOT ECONLIFE_WARN_AS_ERROR)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /WX)
    else()
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

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

# Catch2 v3 — test framework.
# SYSTEM on each FetchContent_Declare marks the dep's include dirs as system
# headers, so the project warning baseline (-Wall/-Wextra/...) does not flag
# third-party code we don't own.
if(ECONLIFE_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        SYSTEM
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
    SYSTEM
)
FetchContent_MakeAvailable(lz4)

# H3 — hexagonal spatial indexing (uber/h3 v4.4.1)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_DOCS    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    h3
    GIT_REPOSITORY https://github.com/uber/h3.git
    GIT_TAG        v4.4.1
    SYSTEM
)
FetchContent_MakeAvailable(h3)

# nlohmann/json — JSON config loading (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)

# Threads — required by ThreadPool
find_package(Threads REQUIRED)

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
