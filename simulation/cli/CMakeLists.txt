add_library(econlife_interactive_json STATIC
    interactive_json.cpp
)

target_link_libraries(econlife_interactive_json PUBLIC
    econlife_modules
    nlohmann_json::nlohmann_json
)

target_include_directories(econlife_interactive_json PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(econlife_cli
    cli_main.cpp
)

target_link_libraries(econlife_cli PRIVATE
    econlife_interactive_json
)

target_include_directories(econlife_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)
