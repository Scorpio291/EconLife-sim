#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// =============================================================================
// Random Event Types — TDD §19
// =============================================================================

// --- §19.5 — EventCategory ---
// Replaces RandomEventType for clearer naming alignment with INTERFACE.md.
// Retained as a separate enum from the old RandomEventType for clarity.

enum class EventCategory : uint8_t {
    natural   = 0,   // earthquakes, floods, droughts, pandemics
    accident  = 1,   // industrial incidents, transport failures, lab explosions
    economic  = 2,   // market shocks, commodity spikes, currency crises, drug market collapse
    human     = 3,   // key NPC deaths, political crises, competitor breakthroughs, strikes
};

// Legacy alias for compatibility with any code referencing the old name.
using RandomEventType = EventCategory;

// --- §19.5 — RandomEventTemplate ---
// Schema for /data/events/event_templates.json -- one entry per named template.
// Engine reads into std::vector<RandomEventTemplate> at startup.

struct RandomEventTemplate {
    std::string       id;                        // e.g. "drought_mild", "industrial_fire"
    // Alias for backward compat; kept in sync with id
    std::string       template_key;              // same as id; legacy accessor
    EventCategory     category;
    std::string       name;                      // human-readable display name
    float             base_weight;               // base selection weight within its type category
    float             severity_min;              // 0.0-1.0 range for this template
    float             severity_max;
    uint32_t          duration_ticks_min;        // event persists for this many ticks (minimum)
    uint32_t          duration_ticks_max;        // event persists for this many ticks (maximum)
    // Condition modifiers: engine multiplies base_weight by these factors at selection time
    float             climate_stress_weight_scale;  // > 1.0 = more likely under climate stress
    float             instability_weight_scale;     // > 1.0 = more likely in unstable provinces
    float             infrastructure_weight_scale;  // < 1.0 = less likely in high-infra provinces
    bool              generates_evidence_token;     // if true, EvidenceToken fired at severity >=
                                                    // config.events.evidence_severity_threshold
};

// --- §19.5 — ActiveRandomEvent ---
// Runtime tracking of an event currently in progress.

struct ActiveRandomEvent {
    uint32_t          id;
    std::string       template_id;               // references RandomEventTemplate.id
    // Legacy alias
    std::string       template_key;              // same as template_id; legacy accessor
    uint32_t          province_id;
    EventCategory     category;
    float             severity;                  // 0.0-1.0; set at event fire time
    uint32_t          started_tick;
    uint32_t          end_tick;                  // started_tick + duration; 0 = resolved/expired
    bool              evidence_generated;        // true once EvidenceToken has been pushed
    bool              effects_applied_this_tick; // reset each tick; tracks per-tick effect application
    // WorldState holds: std::vector<ActiveRandomEvent> active_random_events;
    // Events expire when current_tick >= end_tick; effects applied per-tick while active.
};

}  // namespace econlife
