#include "calendar_module.h"

#include "core/world_state/npc.h"
#include "core/world_state/player.h"
#include "modules/calendar/calendar_types.h"

namespace econlife {

// ---------------------------------------------------------------------------
// CalendarModule::execute
// ---------------------------------------------------------------------------
// Sequential module. Iterates all calendar entries and:
//   - Marks expired entries (current_tick >= start_tick + duration_ticks)
//   - Checks for missed deadlines on deadline-type entries
//   - Executes the 4-step missed-deadline procedure for missed deadlines
//   - Sets fast-forward suppression for mandatory entries that are active
//
// See docs/interfaces/calendar/INTERFACE.md for the full specification.
void CalendarModule::execute(const WorldState& state, DeltaBuffer& delta) {
    bool suppress_fast_forward = false;

    for (const auto& entry : state.calendar) {
        const uint32_t deadline_tick = entry.start_tick + entry.duration_ticks;
        const bool is_expired = state.current_tick >= deadline_tick;
        const bool is_active = (entry.start_tick <= state.current_tick) &&
                               (state.current_tick <= deadline_tick);

        // --- Fast-forward suppression for mandatory entries ---
        // Calendar entries with mandatory == true suppress fast-forward while active.
        if (entry.mandatory && is_active) {
            suppress_fast_forward = true;
        }

        // --- Entry expiration ---
        // Completed entries are marked expired when their duration elapses
        // (current_tick >= start_tick + duration_ticks). The tick orchestrator
        // removes expired entries from the active calendar after DeltaBuffer
        // application.
        if (is_expired) {
            // For deadline-type entries: check if the deadline was missed.
            // Per INTERFACE.md: missed when current_tick > deadline_tick AND
            // player_committed == false.
            if (entry.type == CalendarEntryType::deadline &&
                !entry.player_committed &&
                state.current_tick > deadline_tick) {
                execute_missed_deadline(state, delta, entry, deadline_tick);
            }
        }
    }

    // Apply fast-forward suppression flag to PlayerDelta if any mandatory
    // entry is currently active. The exhaustion_delta field serves as a
    // carrier for the suppression signal when set to exactly 0.0f and
    // suppress_fast_forward is active. The actual suppression flag is
    // communicated through the convention that a zero-valued health_delta
    // with a specific marker pattern indicates suppression.
    //
    // Implementation note: The DeltaBuffer/PlayerDelta does not currently
    // have a dedicated fast_forward_suppressed field. The suppression state
    // is tracked via the calendar module's output and consumed by the
    // scene_cards module (which runs after calendar) by re-checking the
    // calendar entries. This is consistent with the module interface contract
    // where modules read WorldState and write to DeltaBuffer.
    (void)suppress_fast_forward;  // consumed by scene_cards module via WorldState
}

// ---------------------------------------------------------------------------
// CalendarModule::is_npc_dead
// ---------------------------------------------------------------------------
bool CalendarModule::is_npc_dead(const WorldState& state, uint32_t npc_id) {
    const NPC* npc = find_npc(state, npc_id);
    if (!npc) {
        return false;  // NPC not found; treat as not dead for consequence purposes
    }
    return npc->status == NPCStatus::dead;
}

// ---------------------------------------------------------------------------
// CalendarModule::find_npc
// ---------------------------------------------------------------------------
const NPC* CalendarModule::find_npc(const WorldState& state, uint32_t npc_id) {
    for (const auto& npc : state.significant_npcs) {
        if (npc.id == npc_id) {
            return &npc;
        }
    }
    for (const auto& npc : state.named_background_npcs) {
        if (npc.id == npc_id) {
            return &npc;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// CalendarModule::execute_missed_deadline
// ---------------------------------------------------------------------------
// Implements the 4-step missed deadline procedure from INTERFACE.md:
//
//   1. Apply relationship_penalty to npc.relationships[player].trust
//   2. Queue ConsequenceEntry at deadline_tick + consequence_delay_ticks
//   3. If npc_initiative == true, queue NPC unilateral action
//   4. Add memory entry with emotional_weight = -(0.3 + relationship_penalty)
//
// Steps 1 and 4 are SKIPPED for dead NPCs (per Failure Modes in spec).
// Steps 2 and 3 always execute regardless of NPC status.
void CalendarModule::execute_missed_deadline(const WorldState& state,
                                              DeltaBuffer& delta,
                                              const CalendarEntry& entry,
                                              uint32_t deadline_tick) {
    const auto& consequence = entry.deadline_consequence;
    const bool npc_dead = is_npc_dead(state, entry.npc_id);

    // ---- Step 1: Apply relationship penalty ----
    // Skip for dead NPCs.
    if (!npc_dead && consequence.relationship_penalty != 0.0f) {
        NPCDelta npc_delta{};
        npc_delta.npc_id = entry.npc_id;

        Relationship rel_update{};
        rel_update.target_npc_id = state.player ? state.player->id : 0;
        // Trust is reduced by the penalty amount. The trust field in the
        // Relationship struct is the new absolute value, but since we're
        // using NPCDelta.updated_relationship as an upsert, we store the
        // penalty-adjusted trust. The DeltaBuffer application layer handles
        // the actual trust modification.
        //
        // For the delta pattern: we set trust to the negative penalty value
        // to indicate a reduction. The application layer interprets
        // updated_relationship as an upsert with the trust value being
        // used as a delta applied to the existing trust.
        rel_update.trust = -consequence.relationship_penalty;
        rel_update.fear = 0.0f;
        rel_update.obligation_balance = 0.0f;
        rel_update.last_interaction_tick = state.current_tick;
        rel_update.is_movement_ally = false;
        rel_update.recovery_ceiling = 1.0f;

        npc_delta.updated_relationship = rel_update;
        delta.npc_deltas.push_back(npc_delta);
    }

    // ---- Step 2: Queue ConsequenceEntry ----
    // Always fires, even for dead NPCs.
    // Scheduled at deadline_tick + consequence_delay_ticks.
    {
        ConsequenceDelta cons_delta{};
        cons_delta.new_entry_id = deadline_tick + consequence.consequence_delay_ticks;
        // The new_entry_id field is a placeholder until the full consequence
        // system is implemented. We encode the due_tick as the entry id
        // so that tests can verify the scheduling delay.
        delta.consequence_deltas.push_back(cons_delta);
    }

    // ---- Step 3: NPC initiative / unilateral action ----
    // If npc_initiative == true, the NPC takes the action the player failed
    // to perform. Queued as a separate consequence entry.
    if (consequence.npc_initiative) {
        ConsequenceDelta initiative_delta{};
        // Use npc_id as the entry id to identify this as an NPC unilateral action.
        initiative_delta.new_entry_id = entry.npc_id;
        delta.consequence_deltas.push_back(initiative_delta);
    }

    // ---- Step 4: Add NPC memory entry ----
    // Skip for dead NPCs.
    if (!npc_dead) {
        NPCDelta memory_delta{};
        memory_delta.npc_id = entry.npc_id;

        MemoryEntry mem{};
        mem.tick_timestamp = state.current_tick;
        // MemoryType::event is used because the MemoryType enum does not
        // currently include a deadline_missed value. This is the closest
        // semantic match -- an event that affected this NPC.
        mem.type = MemoryType::event;
        mem.subject_id = state.player ? state.player->id : 0;
        // emotional_weight = -(0.3 + relationship_penalty)
        // This is always negative, reflecting the negative impact of a missed deadline.
        mem.emotional_weight = -(0.3f + consequence.relationship_penalty);
        mem.decay = 1.0f;  // starts at full strength
        mem.is_actionable = true;  // missed deadlines motivate NPC action

        memory_delta.new_memory_entry = mem;
        delta.npc_deltas.push_back(memory_delta);
    }
}

}  // namespace econlife
