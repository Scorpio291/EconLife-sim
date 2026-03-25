#pragma once

// WorldGenerator — creates a complete, game-ready WorldState from seed + CSV data.
//
// This is the V1 world generator. It reads goods definitions from CSV files,
// creates provinces with varied geography/climate/resources, seeds NPC populations
// and businesses, and wires all markets. The output is a WorldState ready for
// tick execution.
//
// The full 11-stage geological pipeline (WorldGen v0.18) is future scope.
// This generator produces economically diverse, playable worlds using
// DeterministicRNG for full reproducibility.

#include <string>

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/goods_catalog.h"
#include "core/world_state/geography.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// WorldGeneratorConfig — tuning knobs for world creation
// ---------------------------------------------------------------------------
struct WorldGeneratorConfig {
    uint64_t seed = 42;
    uint32_t province_count = 6;       // V1 target: 6
    uint32_t npc_count = 2000;         // V1 target: 2,000 significant NPCs
    uint8_t starting_era = 1;          // era 1 = year 2000
    uint8_t max_good_tier = 1;         // tier 0-1 goods available at start
    float resource_richness = 1.0f;    // 0.5-2.0 multiplier on deposit quantities
    float corruption_baseline = 0.2f;  // 0.0-1.0 starting corruption
    float criminal_baseline = 0.05f;   // 0.0-1.0 starting criminal dominance
    std::string goods_directory;       // path to packages/base_game/goods/
};

// ---------------------------------------------------------------------------
// WorldGenerator — produces complete WorldState
// ---------------------------------------------------------------------------
class WorldGenerator {
   public:
    // Generate a complete WorldState from config + CSV data.
    // Returns a fully wired WorldState ready for tick execution.
    // The PlayerCharacter must be wired separately (world.player = &player).
    static WorldState generate(const WorldGeneratorConfig& config);

    // Generate and also create a default PlayerCharacter.
    // Returns {WorldState, PlayerCharacter} pair.
    struct WorldWithPlayer {
        WorldState world;
        PlayerCharacter player;
    };
    static WorldWithPlayer generate_with_player(const WorldGeneratorConfig& config);

   private:
    // Province archetypes for economic diversity.
    enum class ProvinceArchetype : uint8_t {
        industrial_hub = 0,    // manufacturing, technology, high infrastructure
        agricultural = 1,      // farming, food processing, high arable land
        resource_rich = 2,     // mining, energy extraction, geological deposits
        coastal_trade = 3,     // port city, trade hub, maritime connections
        financial_center = 4,  // banking, services, high education
        mixed_economy = 5,     // balanced, no strong specialization
    };

    static void create_nation(WorldState& world, uint32_t province_count);
    static void create_provinces(WorldState& world, DeterministicRNG& rng,
                                 const WorldGeneratorConfig& config);
    static void create_province_links(WorldState& world);
    static void create_markets(WorldState& world, DeterministicRNG& rng,
                               const GoodsCatalog& catalog, const WorldGeneratorConfig& config);
    static void create_npcs(WorldState& world, DeterministicRNG& rng,
                            const WorldGeneratorConfig& config);
    static void create_businesses(WorldState& world, DeterministicRNG& rng,
                                  const WorldGeneratorConfig& config);
    static ProvinceArchetype assign_archetype(DeterministicRNG& rng, uint32_t province_idx,
                                              uint32_t province_count);
    static void apply_archetype(Province& province, ProvinceArchetype archetype,
                                DeterministicRNG& rng, const WorldGeneratorConfig& config);
    static void seed_resource_deposits(Province& province, ProvinceArchetype archetype,
                                       DeterministicRNG& rng, float richness);
};

}  // namespace econlife
