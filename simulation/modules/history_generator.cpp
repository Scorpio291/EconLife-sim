// History generation driver — see history_generator.h.

#include "modules/history_generator.h"

#include <memory>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_state/player.h"
#include "modules/register_base_game_modules.h"

namespace econlife {

WorldState generate_world_with_history(const WorldGeneratorConfig& gen_config,
                                       const PackageConfig& pkg_config, uint32_t history_years,
                                       uint32_t threads) {
    auto generated = WorldGenerator::generate_with_player(gen_config);
    WorldState world = std::move(generated.world);
    world.player = std::make_unique<PlayerCharacter>(std::move(generated.player));

    if (history_years == 0) {
        return world;  // no pre-game history — equivalent to instant-world generation
    }

    // Run the full base-game orchestrator forward for the requested span. This is
    // the same engine that runs gameplay — history is simply it running before the
    // player arrives.
    TickOrchestrator orchestrator;
    register_base_game_modules(orchestrator, pkg_config);
    orchestrator.finalize_registration();
    ThreadPool pool(threads == 0u ? 1u : threads);

    for (uint32_t year = 0; year < history_years; ++year) {
        for (uint32_t day = 0; day < 365u; ++day) {
            orchestrator.execute_tick(world, pool);
        }
    }
    return world;
}

}  // namespace econlife
