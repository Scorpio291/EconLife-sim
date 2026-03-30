#pragma once

#include "core/config/package_config.h"

namespace econlife {

class TickOrchestrator;

// Instantiates and registers all 43 base game modules with the orchestrator.
// Call this before finalize_registration().
// Modules with config structs receive the relevant slice of PackageConfig.
void register_base_game_modules(TickOrchestrator& orchestrator, const PackageConfig& config = {});

}  // namespace econlife
