#pragma once

// designer_drug module types.
// See docs/interfaces/designer_drug/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string>
#include <vector>

namespace econlife {

// ---------------------------------------------------------------------------
// SchedulingStage — lifecycle of a designer drug compound
// ---------------------------------------------------------------------------
enum class SchedulingStage : uint8_t {
    unscheduled = 0,       // legal; prices through formal market with 2.5x margin
    review_initiated = 1,  // regulators detected; 180 tick review period
    scheduled = 2,         // illegal; moves to informal market layer
};

// ---------------------------------------------------------------------------
// DesignerDrugCompound — a single designer drug compound
// ---------------------------------------------------------------------------
struct DesignerDrugCompound {
    uint32_t compound_id;
    uint32_t creator_actor_id;
    std::string goods_key;  // "designer_drug_{id}" in goods.csv
    SchedulingStage stage;
    float cumulative_evidence_weight;  // from financial + physical tokens
    float detection_threshold;         // default 2.5
    uint32_t review_start_tick;        // tick when review_initiated
    uint32_t review_duration;          // default 180 ticks (scaled by political_delay)
    float market_margin_multiplier;    // 2.5 unscheduled, 1.0 scheduled, 0.8 no successor
    uint32_t province_id;
    bool has_successor;  // whether actor has another unscheduled compound
};

}  // namespace econlife
