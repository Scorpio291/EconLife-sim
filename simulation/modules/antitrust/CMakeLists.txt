add_library(econlife_antitrust STATIC
    antitrust_module.cpp
)
target_link_libraries(econlife_antitrust PUBLIC econlife_evidence)

econlife_set_warnings(econlife_antitrust)
econlife_maybe_werror(econlife_antitrust)
