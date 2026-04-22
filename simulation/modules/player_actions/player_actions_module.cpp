// player_actions module — drains the PlayerActionQueue each tick, validates
// actions against current WorldState, and translates them into DeltaBuffer
// writes and DeferredWorkQueue items.
//
// Sequential execution (not province-parallel). Runs before calendar and
// scene_cards so downstream modules see the effects of player input.

#include "player_actions_module.h"

#include <algorithm>

#include "core/tick/deferred_work.h"
#include "core/world_state/player.h"
#include "core/world_state/player_action_queue.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// ITickModule interface
// ---------------------------------------------------------------------------

std::string_view PlayerActionsModule::name() const noexcept {
    return "player_actions";
}

std::string_view PlayerActionsModule::package_id() const noexcept {
    return "base_game";
}

ModuleScope PlayerActionsModule::scope() const noexcept {
    return ModuleScope::core;
}

std::vector<std::string_view> PlayerActionsModule::runs_after() const {
    return {};
}

std::vector<std::string_view> PlayerActionsModule::runs_before() const {
    return {"calendar", "scene_cards"};
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static const NPC* find_npc(const WorldState& state, uint32_t npc_id) {
    for (const auto& npc : state.significant_npcs) {
        if (npc.id == npc_id)
            return &npc;
    }
    return nullptr;
}

static bool province_exists(const WorldState& state, uint32_t province_id) {
    for (const auto& p : state.provinces) {
        if (p.id == province_id)
            return true;
    }
    return false;
}

static uint32_t next_business_id(const WorldState& state) {
    uint32_t max_id = 0;
    for (const auto& b : state.npc_businesses) {
        if (b.id > max_id)
            max_id = b.id;
    }
    return max_id + 1;
}

static uint32_t next_calendar_id(const WorldState& state) {
    uint32_t max_id = 0;
    for (const auto& e : state.calendar) {
        if (e.id > max_id)
            max_id = e.id;
    }
    return max_id + 1;
}

// ---------------------------------------------------------------------------
// Per-action handlers
// ---------------------------------------------------------------------------

static void handle_scene_card_choice(const SceneCardChoiceAction& action, const WorldState& state,
                                     DeltaBuffer& delta) {
    // Validate: card exists in pending_scene_cards with matching choice_id.
    for (const auto& card : state.pending_scene_cards) {
        if (card.id == action.scene_card_id) {
            // Validate choice_id is valid on this card.
            bool valid_choice = false;
            for (const auto& choice : card.choices) {
                if (choice.id == action.choice_id) {
                    valid_choice = true;
                    break;
                }
            }
            if (!valid_choice)
                return;
            // Already chosen? Don't overwrite.
            if (card.chosen_choice_id != 0)
                return;

            SceneCardChoiceDelta scd{};
            scd.scene_card_id = action.scene_card_id;
            scd.chosen_choice_id = action.choice_id;
            delta.scene_card_choice_deltas.push_back(scd);
            return;
        }
    }
    // Card not found — silently drop.
}

static void handle_calendar_commit(const CalendarCommitAction& action, const WorldState& state,
                                   DeltaBuffer& delta) {
    for (const auto& entry : state.calendar) {
        if (entry.id == action.calendar_entry_id) {
            // Don't commit to expired entries.
            if (state.current_tick > entry.start_tick + entry.duration_ticks)
                return;

            CalendarCommitDelta ccd{};
            ccd.calendar_entry_id = action.calendar_entry_id;
            ccd.committed = action.accept;
            delta.calendar_commit_deltas.push_back(ccd);
            return;
        }
    }
    // Entry not found — silently drop.
}

static void handle_calendar_schedule(const CalendarScheduleAction& action, const WorldState& state,
                                     DeltaBuffer& delta) {
    // Validate NPC exists for meeting entries.
    if (action.type == CalendarEntryType::meeting && action.npc_id != 0) {
        const NPC* npc = find_npc(state, action.npc_id);
        if (!npc || npc->status == NPCStatus::dead)
            return;
    }

    CalendarEntry new_entry{};
    new_entry.id = next_calendar_id(state);
    new_entry.start_tick = action.desired_start_tick;
    new_entry.duration_ticks = action.duration_ticks;
    new_entry.type = action.type;
    new_entry.npc_id = action.npc_id;
    new_entry.player_committed = true;  // player-scheduled = auto-committed
    new_entry.mandatory = false;
    new_entry.deadline_consequence = {};
    new_entry.scene_card_id = 0;  // populated by scene_cards module if needed

    delta.new_calendar_entries.push_back(new_entry);
}

static void handle_travel(const TravelAction& action, const WorldState& state, DeltaBuffer& delta) {
    if (!state.player)
        return;

    const auto& player = *state.player;

    // Can't travel if already in transit.
    if (player.travel_status == NPCTravelStatus::in_transit)
        return;

    // Can't travel to current province.
    if (action.destination_province_id == player.current_province_id)
        return;

    // Validate destination exists.
    if (!province_exists(state, action.destination_province_id))
        return;

    // Set player to in_transit via PlayerDelta.
    delta.player_delta.new_travel_status = NPCTravelStatus::in_transit;

    // Schedule arrival via DeferredWorkQueue.
    // Travel time: fixed 3 ticks for V1 (same-nation domestic travel).
    constexpr uint32_t TRAVEL_TIME_TICKS = 3;

    // Note: We need to write to the deferred work queue. Since WorldState
    // is const, we cast away const for the queue push. This is the same
    // pattern used by drain_deferred_work (which takes WorldState& for
    // rescheduling). The player_actions module is special: it needs to
    // push deferred work items. The orchestrator passes const WorldState,
    // but the deferred_work_queue is logically a write-side structure.
    // We use a const_cast here as a pragmatic compromise; the alternative
    // is moving action processing into the orchestrator itself.
    auto& mutable_queue = const_cast<DeferredWorkQueue&>(state.deferred_work_queue);
    mutable_queue.push({state.current_tick + TRAVEL_TIME_TICKS, WorkType::player_travel_arrival,
                        state.player->id, PlayerTravelPayload{action.destination_province_id}});
}

static void handle_start_business(const StartBusinessAction& action, const WorldState& state,
                                  DeltaBuffer& delta) {
    if (!state.player)
        return;

    const auto& player = *state.player;

    // Player must be in the target province.
    if (action.province_id != player.current_province_id)
        return;

    // Player must not be in transit.
    if (player.travel_status == NPCTravelStatus::in_transit)
        return;

    // Minimum startup capital check (10,000 liquid cash).
    constexpr float MIN_STARTUP_CAPITAL = 10000.0f;
    if (player.wealth < MIN_STARTUP_CAPITAL)
        return;

    // Create new business.
    NPCBusiness new_biz{};
    new_biz.id = next_business_id(state);
    new_biz.sector = action.sector;
    new_biz.profile = BusinessProfile::fast_expander;  // player default
    new_biz.cash = MIN_STARTUP_CAPITAL;
    new_biz.revenue_per_tick = 0.0f;
    new_biz.cost_per_tick = 0.0f;
    new_biz.market_share = 0.0f;
    new_biz.strategic_decision_tick = 0;
    new_biz.dispatch_day_offset = 0;  // player-owned: dispatch on command
    new_biz.criminal_sector = (action.sector == BusinessSector::criminal);
    new_biz.province_id = action.province_id;
    new_biz.regulatory_violation_severity = 0.0f;
    new_biz.default_activity_scope =
        new_biz.criminal_sector ? VisibilityScope::concealed : VisibilityScope::institutional;
    new_biz.owner_id = player.id;
    new_biz.output_quality = 0.5f;
    new_biz.deferred_salary_liability = 0.0f;

    NewBusinessDelta nbd{};
    nbd.new_business = new_biz;
    delta.new_businesses.push_back(nbd);

    // Deduct startup capital from player wealth.
    delta.player_delta.wealth_delta =
        delta.player_delta.wealth_delta.value_or(0.0f) - MIN_STARTUP_CAPITAL;
}

static void handle_set_production(const SetProductionAction& action, const WorldState& state,
                                  DeltaBuffer& delta) {
    if (!state.player)
        return;

    // Validate business exists and is player-owned.
    for (const auto& biz : state.npc_businesses) {
        if (biz.id == action.business_id) {
            if (biz.owner_id != state.player->id)
                return;

            // Write a BusinessDelta with the output quality update.
            // target_output_rate maps to output_quality for now.
            BusinessDelta bd{};
            bd.business_id = action.business_id;
            bd.output_quality_update = std::clamp(action.target_output_rate, 0.0f, 1.0f);
            delta.business_deltas.push_back(bd);
            return;
        }
    }
    // Business not found — silently drop.
}

static void handle_delegate(const DelegateAction& action, const WorldState& state,
                            DeltaBuffer& delta) {
    if (!state.player)
        return;

    // Validate business exists and is player-owned.
    bool valid_biz = false;
    for (const auto& biz : state.npc_businesses) {
        if (biz.id == action.business_id && biz.owner_id == state.player->id) {
            valid_biz = true;
            break;
        }
    }
    if (!valid_biz)
        return;

    // Validate NPC exists, alive, and has trust > threshold.
    const NPC* npc = find_npc(state, action.manager_npc_id);
    if (!npc || npc->status == NPCStatus::dead)
        return;

    constexpr float DELEGATION_TRUST_THRESHOLD = 0.3f;
    float trust = 0.0f;
    for (const auto& rel : state.player->relationships) {
        if (rel.target_npc_id == action.manager_npc_id) {
            trust = rel.trust;
            break;
        }
    }
    if (trust < DELEGATION_TRUST_THRESHOLD)
        return;

    // Record the delegation as an NPC memory entry so the NPC knows.
    NPCDelta npc_delta{};
    npc_delta.npc_id = action.manager_npc_id;
    npc_delta.new_memory_entry = MemoryEntry{
        state.current_tick,
        MemoryType::interaction,
        state.player->id,
        0.3f,  // moderate positive emotional weight
        1.0f,  // fresh memory
        true   // actionable
    };
    delta.npc_deltas.push_back(npc_delta);
}

static void handle_commercialize_tech(const CommercializeTechAction& action,
                                      const WorldState& state, DeltaBuffer& /* delta */) {
    if (!state.player)
        return;

    // Validate business exists and is player-owned.
    for (const auto& biz : state.npc_businesses) {
        if (biz.id == action.business_id && biz.owner_id == state.player->id) {
            // Validate tech is researched but not commercialized.
            auto it = biz.actor_tech_state.holdings.find(action.node_key);
            if (it == biz.actor_tech_state.holdings.end())
                return;
            if (it->second.stage == TechStage::commercialized)
                return;

            // Schedule commercialization via DeferredWorkQueue.
            auto& mutable_queue = const_cast<DeferredWorkQueue&>(state.deferred_work_queue);
            mutable_queue.push({state.current_tick + 1, WorkType::commercialize_technology,
                                action.business_id,
                                CommercializePayload{action.business_id, 0, 0}});
            return;
        }
    }
}

static void handle_initiate_contact(const InitiateContactAction& action, const WorldState& state,
                                    DeltaBuffer& delta) {
    if (!state.player)
        return;

    const NPC* npc = find_npc(state, action.target_npc_id);
    if (!npc || npc->status == NPCStatus::dead)
        return;

    // For in-person meetings, player must be in the same province.
    // Phone/remote contact is always available.
    bool in_person = (state.player->current_province_id == npc->current_province_id);

    // Create a calendar entry for the introduction meeting.
    CalendarEntry entry{};
    entry.id = next_calendar_id(state);
    entry.start_tick = state.current_tick + 1;  // next tick
    entry.duration_ticks = 1;
    entry.type = CalendarEntryType::meeting;
    entry.npc_id = action.target_npc_id;
    entry.player_committed = true;
    entry.mandatory = false;
    entry.deadline_consequence = {};
    entry.scene_card_id = 0;  // scene_cards module generates procedurally

    (void)in_person;  // setting derived from location at scene card delivery time
    delta.new_calendar_entries.push_back(entry);
}

// ---------------------------------------------------------------------------
// execute — main entry point
// ---------------------------------------------------------------------------

void PlayerActionsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    if (state.player_action_queue.empty())
        return;

    // Sort by sequence_number for deterministic processing order.
    // The queue should already be sorted by insertion order, but we
    // sort explicitly to guarantee determinism even if actions arrive
    // out of order in future IPC scenarios.
    auto sorted_actions = state.player_action_queue;
    std::sort(sorted_actions.begin(), sorted_actions.end(),
              [](const PlayerAction& a, const PlayerAction& b) {
                  return a.sequence_number < b.sequence_number;
              });

    for (const auto& action : sorted_actions) {
        std::visit(
            [&](const auto& payload) {
                using T = std::decay_t<decltype(payload)>;

                if constexpr (std::is_same_v<T, SceneCardChoiceAction>) {
                    handle_scene_card_choice(payload, state, delta);
                } else if constexpr (std::is_same_v<T, CalendarCommitAction>) {
                    handle_calendar_commit(payload, state, delta);
                } else if constexpr (std::is_same_v<T, CalendarScheduleAction>) {
                    handle_calendar_schedule(payload, state, delta);
                } else if constexpr (std::is_same_v<T, TravelAction>) {
                    handle_travel(payload, state, delta);
                } else if constexpr (std::is_same_v<T, StartBusinessAction>) {
                    handle_start_business(payload, state, delta);
                } else if constexpr (std::is_same_v<T, SetProductionAction>) {
                    handle_set_production(payload, state, delta);
                } else if constexpr (std::is_same_v<T, DelegateAction>) {
                    handle_delegate(payload, state, delta);
                } else if constexpr (std::is_same_v<T, CommercializeTechAction>) {
                    handle_commercialize_tech(payload, state, delta);
                } else if constexpr (std::is_same_v<T, InitiateContactAction>) {
                    handle_initiate_contact(payload, state, delta);
                }
            },
            action.payload);
    }

    // Clear the queue. Same const_cast pattern as travel — the queue is
    // logically a write-side staging area, not simulation state.
    auto& mutable_world = const_cast<WorldState&>(state);
    clear_player_action_queue(mutable_world);
}

}  // namespace econlife
