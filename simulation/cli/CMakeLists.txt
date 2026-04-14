add_executable(econlife_cli
    cli_main.cpp
    interactive_json.cpp
)

target_link_libraries(econlife_cli PRIVATE
    econlife_modules
    nlohmann_json::nlohmann_json
)

target_include_directories(econlife_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)
