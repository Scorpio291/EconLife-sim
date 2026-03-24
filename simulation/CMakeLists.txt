add_subdirectory(core)
add_subdirectory(modules)
add_subdirectory(cli)

if(ECONLIFE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
