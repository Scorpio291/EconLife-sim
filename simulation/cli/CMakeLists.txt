add_executable(econlife_cli cli_main.cpp)

target_link_libraries(econlife_cli PRIVATE econlife_modules)

target_include_directories(econlife_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
