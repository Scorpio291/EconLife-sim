#pragma once

// World Class — a single derived rating of how hostile a world is to the
// survival of (baseline) intelligent life, adopting the scale from *The
// Deathworlders* (Jenkinsverse): Garden (1-3), Typical (4-7), Harsh (8-10),
// Deathworld (11-13), Extreme (14+), with Earth = Class 12.
//
// The Class is CALCULATED from the individual world settings you choose (gravity,
// disease, predators, radiation, seasonality, geology, atmosphere) — it is not
// hand-set. Each setting contributes weighted "class points"; the Earth defaults
// are calibrated to sum to ~12. Class measures *survival difficulty* only — Bounty
// (resource abundance / development potential) is deliberately not part of it.
// See docs/design/EconLife_World_Spectrum_and_Evolution_Plan.md.

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace econlife {

// ---------------------------------------------------------------------------
// WorldHazardSettings — the dials you choose. Each is a natural/normalized
// intensity; the Earth defaults compute to ~Class 12. Gravity is in Earth g's;
// the rest are 0..1 "how severe is this factor".
// ---------------------------------------------------------------------------
struct WorldHazardSettings {
    float gravity_g = 1.0f;     // surface gravity in Earth g's (Earth = 1.0)
    float disease = 0.60f;      // endemic disease / parasite load
    float predators = 0.50f;    // large-predator / dangerous-fauna pressure
    float radiation = 0.20f;    // ambient radiation level (Earth low)
    float seasonality = 0.60f;  // seasonal temperature swing / climate extremes
    float geology = 0.60f;      // tectonic activity + storms/floods/wildfires
    float atmosphere = 0.30f;   // atmospheric hostility / toxicity (Earth breathable)
};

// Per-factor weights (class points) the settings are scored against. Defaults are
// calibrated so the Earth settings above total ~12. Tunable — not baked into the
// scoring function.
struct HazardScoringWeights {
    float gravity = 2.0f;       // points per g
    float disease = 3.0f;       // points at setting 1.0
    float predators = 3.0f;
    float radiation = 5.0f;
    float seasonality = 3.333f;
    float geology = 4.0f;
    float atmosphere = 4.333f;
};

enum class WorldClassBand : uint8_t {
    garden = 0,      // 1-3
    typical = 1,     // 4-7
    harsh = 2,       // 8-10
    deathworld = 3,  // 11-13
    extreme = 4,     // 14+
};

// Calculate the World Class from the chosen settings (floored at 1).
inline float world_class(const WorldHazardSettings& s, const HazardScoringWeights& w = {}) {
    const float c = s.gravity_g * w.gravity + s.disease * w.disease + s.predators * w.predators +
                    s.radiation * w.radiation + s.seasonality * w.seasonality +
                    s.geology * w.geology + s.atmosphere * w.atmosphere;
    return std::max(1.0f, c);
}

inline WorldClassBand class_band(float cls) {
    if (cls <= 3.0f)
        return WorldClassBand::garden;
    if (cls <= 7.0f)
        return WorldClassBand::typical;
    if (cls <= 10.0f)
        return WorldClassBand::harsh;
    if (cls <= 13.0f)
        return WorldClassBand::deathworld;
    return WorldClassBand::extreme;
}

inline std::string_view class_band_name(WorldClassBand b) {
    switch (b) {
        case WorldClassBand::garden:
            return "Garden";
        case WorldClassBand::typical:
            return "Typical";
        case WorldClassBand::harsh:
            return "Harsh";
        case WorldClassBand::deathworld:
            return "Deathworld";
        case WorldClassBand::extreme:
            return "Extreme";
    }
    return "Unknown";
}

// --- Reference settings (anchors / presets) ---

// Earth: the calibration anchor, ~Class 12 — a fertile deathworld.
inline WorldHazardSettings earth_hazard() { return WorldHazardSettings{}; }

// A gentle Garden world (~Class 2-3): low on every hazard.
inline WorldHazardSettings garden_hazard() {
    return WorldHazardSettings{/*g*/ 0.7f, /*dis*/ 0.05f, /*pred*/ 0.05f, /*rad*/ 0.02f,
                               /*seas*/ 0.10f, /*geo*/ 0.05f, /*atmo*/ 0.05f};
}

// A world harsher than Earth, still in the Deathworld band (~Class 13).
inline WorldHazardSettings deathworld_hazard() {
    return WorldHazardSettings{/*g*/ 1.1f, /*dis*/ 0.60f, /*pred*/ 0.55f, /*rad*/ 0.25f,
                               /*seas*/ 0.62f, /*geo*/ 0.62f, /*atmo*/ 0.35f};
}

// ---------------------------------------------------------------------------
// WorldArchetype — the two headline dials: Bounty (resource abundance) + the
// Hazard settings (which compute the World Class). Consumed by world-gen +
// the society harness to slide a world from garden to deathworld.
// ---------------------------------------------------------------------------
struct WorldArchetype {
    const char* name = "earthlike";
    float bounty = 1.0f;            // natural-capital multiplier (1.0 = earthlike)
    WorldHazardSettings hazard{};   // -> World Class (computed)
};

// (A Class-derived mortality multiplier used to live here — `hazard_mortality_multiplier`,
// clamped to [0.15, 3.0]. It had no callers anywhere in the repo: the live path is
// hazard_mortality_from_settings() below, which scores the individual hazards rather than
// the single Class number. Deleted 2026-07-26 with the mortality-rail cleanup.)

inline WorldArchetype archetype_garden() { return {"garden", 1.8f, garden_hazard()}; }
inline WorldArchetype archetype_earthlike() { return {"earthlike", 1.0f, earth_hazard()}; }

// A BARREN deathworld: high hazard AND scarce resources. This is the one that
// genuinely struggles — and it struggles because of the low Bounty, not the Class.
inline WorldArchetype archetype_deathworld() { return {"deathworld", 0.4f, deathworld_hazard()}; }

// A FERTILE deathworld: high hazard (deathworld band, ~Class 13) but resource-rich
// (earthlike+ Bounty). Earth itself is a Class-12 fertile deathworld that thrived —
// a hostile world need not stall if it is also bountiful, because the population's
// generational hardiness adapts to the hazard while the Bounty feeds development.
inline WorldArchetype archetype_fertile_deathworld() {
    return {"fertile_deathworld", 1.2f, deathworld_hazard()};
}

// ---------------------------------------------------------------------------
// Per-channel effects of the individual settings (each dial does its own thing).
// ---------------------------------------------------------------------------

// Relative weights of the MORTALITY-causing hazards. Gravity here is fall/accident
// damage (heavier worlds = harder falls) — NOT a food cost. Seasonality is NOT here
// (it acts on food, below). Tunable; not baked into the function.
struct HazardMortalityWeights {
    float disease = 1.0f;
    float predators = 0.8f;
    float radiation = 0.7f;
    float atmosphere = 0.7f;
    float geology = 0.6f;        // quakes/storms/disasters
    float gravity_falls = 0.3f;  // fall/accident lethality from gravity
};

// Cohort mortality multiplier from the world's hazards, normalized so Earth = 1.0.
// Each hazard contributes distinctly; a plagued world raises mortality via disease,
// a heavy world via falls, etc.
//
// The [0.15, 3.0] clamp below bounds a STATIC world-creation normalization (a property of
// the chosen dials, not of anything that evolves in play), and it is inert for every
// shipped preset: garden scores 0.19, Earth 1.0, deathworld 1.08 — all far inside the
// band. It is therefore spectrum calibration on the world-authoring surface rather than a
// mechanism, and is UNDER REVIEW as such; it is not the population-mortality rail (that
// one lived in population_aging and was retired 2026-07-26 in favour of
// p = 1 - exp(-rate), which is where mortality is now physically bounded).
inline float hazard_mortality_from_settings(const WorldHazardSettings& s,
                                            const HazardMortalityWeights& w = {}) {
    auto score = [&](const WorldHazardSettings& x) {
        return x.disease * w.disease + x.predators * w.predators + x.radiation * w.radiation +
               x.atmosphere * w.atmosphere + x.geology * w.geology + x.gravity_g * w.gravity_falls;
    };
    const float earth = score(earth_hazard());
    if (earth <= 0.0f)
        return 1.0f;
    return std::clamp(score(s) / earth, 0.15f, 3.0f);
}

}  // namespace econlife
