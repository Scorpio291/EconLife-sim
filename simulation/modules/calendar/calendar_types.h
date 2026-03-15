#pragma once

#include <cstdint>
#include <string>

namespace econlife {

// Forward declaration for ConsequenceType defined in evidence/consequence module (Section 6)
enum class ConsequenceType : uint8_t;

// ---------------------------------------------------------------------------
// CalendarEntryType
// ---------------------------------------------------------------------------
// Derived from TDD Section 8 inline comment on CalendarEntry.type:
//   "meeting, event, operation, deadline, personal"
enum class CalendarEntryType : uint8_t {
    meeting   = 0,  // scheduled meeting with an NPC
    event     = 1,  // public or private event
    operation = 2,  // player-initiated operation (criminal or business)
    deadline  = 3,  // obligation or legal deadline; consequence on miss
    personal  = 4   // personal / lifestyle calendar entry
};

// ---------------------------------------------------------------------------
// DeadlineConsequence
// ---------------------------------------------------------------------------
// Complete specification from TDD Section 8.
// When deadline is missed -- 4-step procedure:
//   1. Apply relationship_penalty to npc.relationships[player].trust immediately.
//   2. Queue ConsequenceEntry at (deadline_tick + consequence_delay_ticks):
//        type     = consequence_type
//        severity = consequence_severity
//   3. If npc_initiative == true:
//        NPC takes the action they were waiting for the player to take,
//        without player involvement; evaluated via NPC behavior engine.
//        NPC unilateral action types: file_complaint, contact_rival,
//          report_to_regulator, publish_information, escalate_obligation
//        queue_npc_unilateral_action(npc, action_type: action_npc_was_enabling)
//   4. Add to NPC memory log:
//        type             = deadline_missed
//        subject_id       = player
//        emotional_weight = -(0.3 + relationship_penalty)
//        // missed deadlines are remembered; relationship effect is durable
//        // across future interactions
struct DeadlineConsequence {
    float           relationship_penalty;        // applied immediately on miss to
                                                 // npc.relationships[player].trust; 0.0 if none
    bool            npc_initiative;              // if true, NPC acts unilaterally on deadline
    ConsequenceType consequence_type;            // consequence that fires
    float           consequence_severity;        // 0.0-1.0; input to consequence engine
    uint32_t        consequence_delay_ticks;     // delay after deadline before consequence fires
    std::string     default_outcome_description; // for UI / player information
};

// ---------------------------------------------------------------------------
// CalendarEntry
// ---------------------------------------------------------------------------
// When the player is in a committed calendar entry (start_tick to
// start_tick + duration_ticks), the simulation continues but the player's
// interaction is limited to that scene card's choices. Fast-forward is
// suppressed during mandatory entries.
struct CalendarEntry {
    uint32_t              id;
    uint32_t              start_tick;
    uint32_t              duration_ticks;
    CalendarEntryType     type;                  // meeting, event, operation, deadline, personal
    uint32_t              npc_id;                // for meetings
    bool                  player_committed;      // false = invited but not yet accepted
    bool                  mandatory;             // legal summons, etc.
    DeadlineConsequence   deadline_consequence;  // what happens if the player misses it
    uint32_t              scene_card_id;         // which scene card to render on engagement
};

}  // namespace econlife
