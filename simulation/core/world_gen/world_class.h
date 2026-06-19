#pragma once

// Deathworld Class — a single derived rating of how hostile a world is to the
// survival of (baseline) intelligent life, adopting the scale from *The
// Deathworlders* (Jenkinsverse): Garden (1-3), Typical (4-7), Harsh (8-10),
// Deathworld (11-13), Extreme (14+), with Earth = Class 12.
//
// Canon leaves the exact formula unspecified, so this is our own: each HAZARD
// factor contributes additive "class points", and the class is their sum. The
// Earth profile is calibrated to total 12. Class measures *survival difficulty*
// only — Bounty (resource abundance / development potential) and Isolation are
// deliberately NOT part of it (a world can be deadly and fertile, or safe and
// barren). See docs/design/EconLife_World_Spectrum_and_Evolution_Plan.md.

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace econlife {

// Hazard factors as class-point contributions. The Earth profile (the builtin
// default below) sums to 12.0. Scale a factor up for a harsher world, down for a
// gentler one. Non-negative.
struct WorldHazardProfile {
    float gravity = 2.0f;      // surface gravity: a pervasive tax on all effort
    float atmosphere = 1.5f;   // breathability / toxicity / pressure
    float radiation = 1.0f;    // ambient radiation load
    float seasonality = 2.0f;  // seasonal temperature swing / climate extremes
    float cataclysm = 2.5f;    // geology (quakes/volcanoes) + storms/floods/wildfires
    float biohazard = 3.0f;    // predators + toxic flora/fauna + disease + competition

    float sum() const { return gravity + atmosphere + radiation + seasonality + cataclysm + biohazard; }
};

enum class WorldClassBand : uint8_t {
    garden = 0,      // 1-3   : extremely safe
    typical = 1,     // 4-7   : most species evolve here
    harsh = 2,       // 8-10  : tougher gravity/climate/ecosystems
    deathworld = 3,  // 11-13 : survival demands constant adaptation
    extreme = 4,     // 14+   : beyond most deathworld standards
};

// The class is the factor sum, floored at 1 (no world is below Class 1).
inline float deathworld_class(const WorldHazardProfile& p) {
    return std::max(1.0f, p.sum());
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

// --- Reference profiles (anchors / presets) ---

// Earth: the builtin default, Class 12 — a fertile deathworld.
inline WorldHazardProfile earth_profile() { return WorldHazardProfile{}; }

// A gentle Garden world (~Class 2): minimal hazards on every axis.
inline WorldHazardProfile garden_profile() {
    return WorldHazardProfile{0.3f, 0.2f, 0.1f, 0.4f, 0.4f, 0.4f};  // sum ~1.8
}

// A world harsher than Earth but still in the Deathworld band (~Class 12.8).
inline WorldHazardProfile deathworld_profile() {
    return WorldHazardProfile{2.4f, 1.6f, 1.4f, 2.2f, 2.7f, 2.5f};  // sum ~12.8
}

}  // namespace econlife
