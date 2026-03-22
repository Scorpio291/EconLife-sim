#pragma once

namespace econlife {

class TickOrchestrator;

// Instantiates and registers all 43 base game modules with the orchestrator.
// Call this before finalize_registration().
void register_base_game_modules(TickOrchestrator& orchestrator);

}  // namespace econlife
