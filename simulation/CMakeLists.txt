add_subdirectory(core)
add_subdirectory(modules)

if(ECONLIFE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
