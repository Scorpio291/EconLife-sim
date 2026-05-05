add_library(econlife_calendar STATIC
    calendar_module.cpp
)
target_link_libraries(econlife_calendar PUBLIC econlife_core)

econlife_set_warnings(econlife_calendar)
econlife_maybe_werror(econlife_calendar)
