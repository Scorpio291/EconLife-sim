#pragma once

// Pre-market entry materialization (M7) — the medieval band's finale.
//
// During history-gen the pre-modern economy is COHORT-SCALE aggregates (urban
// population, granaries, proto-capital, lords); per-firm entities would be an
// anachronism at that resolution. At ENTRY — a fresh pre-modern start, or promoting
// a region to full LOD — those aggregates MATERIALIZE into entities: workshops in
// the crafts the catchment supports (founder-funded from the wealthiest residents'
// proto-capital, a conserved transfer), and manors for the lord stratum in manorial
// regimes. Everything derives from the SAME laws the climb uses (subsistence output,
// the ox-cart catchment, wealth-ranked lordship) — one law, two resolutions.
//
// See docs/design/EconLife_Medieval_Band_Expansion_v01.md (§4 scale note, M7).

#include <cstdint>

namespace econlife {

struct WorldState;
struct WorldGeneratorConfig;
class RecipeCatalog;
class FacilityTypeCatalog;
class DeterministicRNG;

class PremarketGenesis {
   public:
    // Materialize workshops + manors from the world's cohort aggregates for `era`.
    // Mutates world directly (entry-time assembly, like the rest of world-gen).
    // Founder endowments are CONSERVED transfers (npc.capital -> business.cash).
    // Returns the number of businesses created.
    static uint32_t materialize(WorldState& world, const RecipeCatalog& recipes,
                                const FacilityTypeCatalog& facility_types,
                                const WorldGeneratorConfig& config, uint8_t era,
                                DeterministicRNG& rng);
};

}  // namespace econlife
