// scene_cards module — evaluates trigger conditions for pending scene cards,
// computes NPC presentation state, validates physical presence, and processes
// player choices into consequence deltas.
//
// Sequential execution (not province-parallel). Runs after "calendar".
// See docs/interfaces/scene_cards/INTERFACE.md for canonical spec.

#include "scene_cards_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"      // PlayerCharacter (complete type for state.player->)

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace econlife {

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

// Maximum scene cards that can be queued for delivery in a single tick.
// Prevents UI overload per the interface spec.
static constexpr uint32_t MAX_SCENE_CARDS_PER_TICK = 5;

// Weights for npc_presentation_state derivation.
// Formula: clamp(trust_weight * trust_normalized + risk_weight * risk_tolerance, 0, 1)
// where trust_normalized = (trust + 1.0) / 2.0 to map [-1,1] -> [0,1].
static constexpr float TRUST_WEIGHT = 0.7f;
static constexpr float RISK_WEIGHT  = 0.3f;

// ---------------------------------------------------------------------------
// Free function implementations
// ---------------------------------------------------------------------------

bool is_in_person_setting(SceneSetting setting) {
    switch (setting) {
        case SceneSetting::phone_call:
        case SceneSetting::video_call:
            return false;  // Remote settings -- no province constraint
        default:
            return true;   // All other settings are in-person
    }
}

float compute_presentation_state(float trust, float risk_tolerance) {
    float trust_normalized = (trust + 1.0f) / 2.0f;
    float raw = TRUST_WEIGHT * trust_normalized + RISK_WEIGHT * risk_tolerance;
    return std::clamp(raw, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Internal helpers (file-scope)
// ---------------------------------------------------------------------------

// Finds the NPC's trust toward the player by scanning the NPC's relationship
// graph for an entry matching player_id. Returns 0.0 if no relationship exists.
static float find_trust_toward_player(const NPC& npc, uint32_t player_id) {
    for (const auto& rel : npc.relationships) {
        if (rel.target_npc_id == player_id) {
            return rel.trust;
        }
    }
    return 0.0f;  // No relationship entry = neutral trust
}

// Finds the NPC by id in the significant_npcs vector. Returns nullptr if not found.
static const NPC* find_npc(const WorldState& state, uint32_t npc_id) {
    for (const auto& npc : state.significant_npcs) {
        if (npc.id == npc_id) {
            return &npc;
        }
    }
    return nullptr;
}

// Finds a PlayerChoice within a SceneCard by choice id. Returns nullptr if not found.
static const PlayerChoice* find_choice(const SceneCard& card, uint32_t choice_id) {
    for (const auto& choice : card.choices) {
        if (choice.id == choice_id) {
            return &choice;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// SceneCardsModule — ITickModule interface
// ---------------------------------------------------------------------------

std::string_view SceneCardsModule::name() const noexcept {
    return "scene_cards";
}

std::string_view SceneCardsModule::package_id() const noexcept {
    return "base_game";
}

ModuleScope SceneCardsModule::scope() const noexcept {
    return ModuleScope::v1;
}

std::vector<std::string_view> SceneCardsModule::runs_after() const {
    return {"calendar"};
}

void SceneCardsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Guard: player must exist.
    if (!state.player) {
        return;
    }

    const uint32_t player_id = state.player->id;
    const uint32_t player_province = state.player->current_province_id;

    // ===================================================================
    // Phase 1: Process pending scene cards with player choices (resolve)
    // ===================================================================
    resolve_player_choices(state, delta);

    // ===================================================================
    // Phase 2: Discard cards for dead NPCs
    // ===================================================================
    discard_dead_npc_cards(state, delta);

    // ===================================================================
    // Phase 3: Trigger new scene cards from calendar entries
    // ===================================================================
    trigger_calendar_cards(state, delta, player_id, player_province);

    // ===================================================================
    // Phase 4: Apply authored priority (deduplicate same NPC + tick)
    // ===================================================================
    apply_authored_priority(state, delta);

    // ===================================================================
    // Phase 5: Validate physical presence and compute presentation state
    // ===================================================================
    finalize_new_cards(state, delta, player_id, player_province);
}

// ---------------------------------------------------------------------------
// Phase 1: Resolve player choices on existing pending scene cards
// ---------------------------------------------------------------------------

void SceneCardsModule::resolve_player_choices(const WorldState& state,
                                              DeltaBuffer& delta) const {
    for (const auto& card : state.pending_scene_cards) {
        if (card.chosen_choice_id == 0) {
            continue;  // Player hasn't chosen yet
        }

        const PlayerChoice* choice = find_choice(card, card.chosen_choice_id);
        if (!choice) {
            continue;  // Invalid choice id; skip
        }

        // Record the interaction in NPC memory if the NPC exists and
        // the card type involves an NPC (not news_notification).
        if (card.type != SceneCardType::news_notification && card.npc_id != 0) {
            const NPC* npc = find_npc(state, card.npc_id);
            if (npc && npc->status != NPCStatus::dead) {
                NPCDelta npc_delta{};
                npc_delta.npc_id = card.npc_id;
                npc_delta.new_memory_entry = MemoryEntry{
                    state.current_tick,
                    MemoryType::interaction,
                    state.player->id,
                    0.5f,   // moderate emotional weight for scene card interaction
                    1.0f,   // fresh memory, no decay yet
                    true    // actionable
                };
                delta.npc_deltas.push_back(npc_delta);
            }
        }

        // If the choice has a consequence_id, schedule it via
        // the consequence delta for processing by the consequence system.
        if (choice->consequence_id != 0) {
            ConsequenceDelta cons_delta{};
            cons_delta.new_entry_id = choice->consequence_id;
            delta.consequence_deltas.push_back(cons_delta);
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 2: Discard scene cards for dead NPCs
// ---------------------------------------------------------------------------

void SceneCardsModule::discard_dead_npc_cards(const WorldState& state,
                                              DeltaBuffer& /* delta */) const {
    // Dead NPC filtering is applied in finalize_new_cards for new cards
    // and implicitly by the WorldState application layer for existing
    // pending cards. No explicit delta needed here.
    (void)state;
}

// ---------------------------------------------------------------------------
// Phase 3: Trigger new scene cards from calendar entries
// ---------------------------------------------------------------------------

void SceneCardsModule::trigger_calendar_cards(const WorldState& state,
                                              DeltaBuffer& delta,
                                              uint32_t /* player_id */,
                                              uint32_t /* player_province */) const {
    uint32_t cards_added = 0;

    for (const auto& entry : state.calendar) {
        if (entry.start_tick != state.current_tick) {
            continue;  // Not triggered this tick
        }
        if (entry.scene_card_id == 0) {
            continue;  // No linked scene card
        }

        // Check if this scene card already exists in pending.
        bool already_pending = false;
        for (const auto& pending : state.pending_scene_cards) {
            if (pending.id == entry.scene_card_id) {
                already_pending = true;
                break;
            }
        }
        // Also check cards already added this tick.
        for (const auto& added : delta.new_scene_cards) {
            if (added.id == entry.scene_card_id) {
                already_pending = true;
                break;
            }
        }

        if (already_pending) {
            continue;
        }

        // Respect per-tick card limit.
        if (cards_added >= MAX_SCENE_CARDS_PER_TICK) {
            break;
        }

        // Determine the scene card type from the calendar entry type.
        SceneCardType card_type = SceneCardType::meeting;
        SceneSetting  card_setting = SceneSetting::private_office;
        if (entry.type == CalendarEntryType::meeting) {
            card_type = SceneCardType::meeting;
            card_setting = SceneSetting::private_office;
        } else if (entry.type == CalendarEntryType::personal) {
            card_type = SceneCardType::personal_event;
            card_setting = SceneSetting::home_dining;
        } else if (entry.type == CalendarEntryType::event) {
            card_type = SceneCardType::news_notification;
            card_setting = SceneSetting::phone_call;
        }

        // Create the scene card. Dialogue and choices will be
        // populated by the procedural generation system or loaded
        // from authored content in the package files.
        SceneCard card{};
        card.id = entry.scene_card_id;
        card.type = card_type;
        card.setting = card_setting;
        card.npc_id = entry.npc_id;
        card.npc_presentation_state = 0.5f;  // Default; computed in Phase 5
        card.is_authored = false;             // Calendar-triggered = procedural by default
        card.chosen_choice_id = 0;

        delta.new_scene_cards.push_back(card);
        cards_added++;
    }
}

// ---------------------------------------------------------------------------
// Phase 4: Authored priority
// ---------------------------------------------------------------------------

void SceneCardsModule::apply_authored_priority(const WorldState& /* state */,
                                               DeltaBuffer& delta) const {
    if (delta.new_scene_cards.size() <= 1) {
        return;
    }

    // Build a set of NPC ids that have authored cards.
    std::unordered_map<uint32_t, bool> npc_has_authored;

    for (const auto& card : delta.new_scene_cards) {
        if (card.is_authored) {
            npc_has_authored[card.npc_id] = true;
        }
    }

    // Remove procedural cards for NPCs that have authored cards.
    if (!npc_has_authored.empty()) {
        auto it = std::remove_if(
            delta.new_scene_cards.begin(),
            delta.new_scene_cards.end(),
            [&](const SceneCard& card) {
                return !card.is_authored &&
                       npc_has_authored.count(card.npc_id) > 0;
            });
        delta.new_scene_cards.erase(it, delta.new_scene_cards.end());
    }
}

// ---------------------------------------------------------------------------
// Phase 5: Validate physical presence and compute presentation state
// ---------------------------------------------------------------------------

void SceneCardsModule::finalize_new_cards(const WorldState& state,
                                          DeltaBuffer& delta,
                                          uint32_t player_id,
                                          uint32_t player_province) const {
    auto it = std::remove_if(
        delta.new_scene_cards.begin(),
        delta.new_scene_cards.end(),
        [&](SceneCard& card) -> bool {
            // --- Dead NPC check ---
            if (card.npc_id != 0) {
                const NPC* npc = find_npc(state, card.npc_id);
                if (!npc || npc->status == NPCStatus::dead) {
                    return true;  // Discard
                }

                // --- Physical presence check for in-person settings ---
                if (is_in_person_setting(card.setting)) {
                    if (player_province != npc->current_province_id) {
                        return true;  // Not delivered; province mismatch
                    }
                }

                // --- Compute npc_presentation_state ---
                // news_notification cards have no NPC portrait interaction;
                // skip presentation state computation.
                if (card.type != SceneCardType::news_notification) {
                    float trust = find_trust_toward_player(*npc, player_id);
                    card.npc_presentation_state =
                        compute_presentation_state(trust, npc->risk_tolerance);
                } else {
                    card.npc_presentation_state = 0.0f;
                }
            } else {
                // No NPC associated (e.g., pure news notification).
                card.npc_presentation_state = 0.0f;
            }

            return false;  // Keep card
        });
    delta.new_scene_cards.erase(it, delta.new_scene_cards.end());

    // Enforce per-tick cap on new cards.
    if (delta.new_scene_cards.size() > MAX_SCENE_CARDS_PER_TICK) {
        delta.new_scene_cards.resize(MAX_SCENE_CARDS_PER_TICK);
    }
}

}  // namespace econlife
