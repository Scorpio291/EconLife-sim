# Unit tests — module existence, individual module tests
add_executable(econlife_unit_tests
    module_exists_test.cpp
    rng_test.cpp
    deferred_work_queue_test.cpp
    tick_orchestrator_test.cpp
    calendar_test.cpp
    scene_cards_test.cpp
    random_events_test.cpp
)

target_link_libraries(econlife_unit_tests PRIVATE
    Catch2::Catch2WithMain
    econlife_core
    econlife_calendar
    econlife_scene_cards
    econlife_random_events
)

catch_discover_tests(econlife_unit_tests)
