// scene_cards module — evaluates trigger conditions for pending scene cards,
// computes NPC presentation state, validates physical presence, and processes
// player choices into consequence deltas.
//
// Sequential execution (not province-parallel). Runs after "calendar".
// See docs/interfaces/scene_cards/INTERFACE.md for canonical spec.

#include "scene_cards_module.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
#include "core/world_state/player.h"        // PlayerCharacter (complete type for state.player->)
#include "core/world_state/world_state.h"

namespace econlife {

// Configuration constants were moved to SceneCardsConfig (package_config.h).
// The module accesses them via cfg_ member.

// ---------------------------------------------------------------------------
// Free function implementations
// ---------------------------------------------------------------------------

bool is_in_person_setting(SceneSetting setting) {
    switch (setting) {
        case SceneSetting::phone_call:
        case SceneSetting::video_call:
            return false;  // Remote settings -- no province constraint
        default:
            return true;  // All other settings are in-person
    }
}

float compute_presentation_state(float trust, float risk_tolerance, float trust_weight,
                                 float risk_weight) {
    float trust_normalized = (trust + 1.0f) / 2.0f;
    float raw = trust_weight * trust_normalized + risk_weight * risk_tolerance;
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
    return lookup_npc_by_id(state, npc_id);
}

// Choice ids on a calendar-triggered card. Kept as named constants because
// the default outcome refers to one of them.
static constexpr uint32_t CALENDAR_CHOICE_ATTEND = 1;
static constexpr uint32_t CALENDAR_CHOICE_SKIP = 2;

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
    // Phase 2: Queue lifecycle — discard, retire, expire, cap
    // ===================================================================
    run_queue_lifecycle(state, delta);

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

void SceneCardsModule::resolve_player_choices(const WorldState& state, DeltaBuffer& delta) const {
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
                    0.5f,  // moderate emotional weight for scene card interaction
                    1.0f,  // fresh memory, no decay yet
                    true   // actionable
                };
                delta.npc_deltas.push_back(npc_delta);
            }
        }

        // If the choice has a consequence_id, schedule it via
        // the consequence delta for processing by the consequence system.
        if (choice->consequence_id != 0) {
            ConsequenceDelta cons_delta{};
            cons_delta.new_consequence =
                make_consequence(choice->consequence_id, ConsequenceCategory::social_consequence, 0,
                                 0, 0, state.current_tick);
            delta.consequence_deltas.push_back(cons_delta);
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 2: Queue lifecycle
// ---------------------------------------------------------------------------
// pending_scene_cards is append-only at the WorldState layer; this pass is the
// only thing that takes cards back out, and it is what makes the queue bounded.
// Four rules, in the order a card meets them:
//
//   1. The card's NPC died      -> discarded, no consequence (INTERFACE.md
//                                  failure mode + test_dead_npc_card_discarded).
//   2. The card was resolved    -> retired the tick AFTER resolution, so every
//                                  consumer had a full tick to read the choice.
//   3. A timed_optional card    -> its default_choice_id is fired as a real
//      passed its expiry           choice (Rulebook §3: dismissal is a decision
//                                  with a result); the card then retires through
//                                  rule 2 next tick. With no default outcome
//                                  authored there is nothing to fire, so it is
//                                  retired directly rather than left to wedge.
//   4. Ambient cards over cap   -> the oldest surplus is cleared (§1.3).
//
// mandatory cards are deliberately exempt from 3 and 4: they must be engaged.
void SceneCardsModule::run_queue_lifecycle(const WorldState& state, DeltaBuffer& delta) const {
    const uint32_t now = state.current_tick;

    // Live ambient cards, oldest first, for the cap sweep in rule 4.
    std::vector<const SceneCard*> ambient_live;

    for (const auto& card : state.pending_scene_cards) {
        // Rule 1 — the NPC is gone.
        if (card.npc_id != 0) {
            const NPC* npc = find_npc(state, card.npc_id);
            if (!npc || npc->status == NPCStatus::dead) {
                delta.retired_scene_card_ids.push_back(card.id);
                continue;
            }
        }

        // Rule 2 — resolved, and its tick of visibility has passed.
        if (card.resolved_tick != 0) {
            if (now > card.resolved_tick)
                delta.retired_scene_card_ids.push_back(card.id);
            continue;
        }

        // Rule 3 — expiry fires the authored default outcome.
        if (card.card_class == CardClass::timed_optional && card.expires_tick != 0 &&
            now > card.expires_tick) {
            if (find_choice(card, card.default_choice_id) != nullptr) {
                SceneCardChoiceDelta scd{};
                scd.scene_card_id = card.id;
                scd.chosen_choice_id = card.default_choice_id;
                delta.scene_card_choice_deltas.push_back(scd);
            } else {
                delta.retired_scene_card_ids.push_back(card.id);
            }
            continue;
        }

        if (card.card_class == CardClass::ambient)
            ambient_live.push_back(&card);
    }

    // Rule 4 — hold the ambient tier at its cap, oldest cleared first.
    if (ambient_live.size() > cfg_.ambient_queue_cap) {
        std::stable_sort(ambient_live.begin(), ambient_live.end(),
                         [](const SceneCard* a, const SceneCard* b) {
                             if (a->created_tick != b->created_tick)
                                 return a->created_tick < b->created_tick;
                             return a->id < b->id;
                         });
        const std::size_t surplus = ambient_live.size() - cfg_.ambient_queue_cap;
        for (std::size_t i = 0; i < surplus; ++i)
            delta.retired_scene_card_ids.push_back(ambient_live[i]->id);
    }
}

// ---------------------------------------------------------------------------
// Phase 3: Trigger new scene cards from calendar entries
// ---------------------------------------------------------------------------

void SceneCardsModule::trigger_calendar_cards(const WorldState& state, DeltaBuffer& delta,
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
        if (cards_added >= cfg_.max_scene_cards_per_tick) {
            break;
        }

        // Determine the scene card type from the calendar entry type.
        SceneCardType card_type = SceneCardType::meeting;
        SceneSetting card_setting = SceneSetting::private_office;
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

        // Create the scene card. Richer dialogue comes from the authored
        // template catalog; the choices here are the two a committed calendar
        // entry always offers, and they are not decoration — a card with no
        // choices cannot be answered, and finalize_new_cards drops it.
        SceneCard card{};
        card.id = entry.scene_card_id;
        card.type = card_type;
        card.setting = card_setting;
        card.npc_id = entry.npc_id;
        card.npc_presentation_state = 0.5f;  // Default; computed in Phase 5
        card.is_authored = false;            // Calendar-triggered = procedural by default
        card.chosen_choice_id = 0;

        // Rulebook §1.2/§3: an inbound commitment the player never answers is
        // a declined commitment — a real outcome, not a null event. A mandatory
        // calendar entry is a summons and has to be engaged.
        card.card_class =
            entry.mandatory ? CardClass::mandatory : CardClass::timed_optional;
        card.choices.push_back(
            PlayerChoice{CALENDAR_CHOICE_ATTEND, "Attend", "Keep the commitment.", 0});
        card.choices.push_back(
            PlayerChoice{CALENDAR_CHOICE_SKIP, "Skip", "Do not show up.", 0});
        card.default_choice_id = CALENDAR_CHOICE_SKIP;

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
            delta.new_scene_cards.begin(), delta.new_scene_cards.end(), [&](const SceneCard& card) {
                return !card.is_authored && npc_has_authored.count(card.npc_id) > 0;
            });
        delta.new_scene_cards.erase(it, delta.new_scene_cards.end());
    }
}

// ---------------------------------------------------------------------------
// Phase 5: Validate physical presence and compute presentation state
// ---------------------------------------------------------------------------

void SceneCardsModule::finalize_new_cards(const WorldState& state, DeltaBuffer& delta,
                                          uint32_t player_id, uint32_t player_province) const {
    // Count the live timed-optional tier before admitting new cards, so the
    // demotion rule below (Rulebook §2) measures against the real queue.
    uint32_t timed_live = 0;
    for (const auto& card : state.pending_scene_cards) {
        if (card.card_class == CardClass::timed_optional && card.resolved_tick == 0)
            ++timed_live;
    }

    auto it = std::remove_if(
        delta.new_scene_cards.begin(), delta.new_scene_cards.end(), [&](SceneCard& card) -> bool {
            // --- A card the player cannot answer is a card that wedges the
            // queue. This is the invariant that makes the queue provably
            // drainable, so it is enforced here rather than trusted to
            // producers: no choices, no card. ---
            if (card.choices.empty()) {
                return true;  // Discard
            }

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
                    card.npc_presentation_state = compute_presentation_state(
                        trust, npc->risk_tolerance, cfg_.trust_weight, cfg_.risk_weight);
                } else {
                    card.npc_presentation_state = 0.0f;
                }
            } else {
                // No NPC associated (e.g., pure news notification).
                card.npc_presentation_state = 0.0f;
            }

            // --- Lifecycle stamping ---
            card.created_tick = state.current_tick;

            if (card.card_class == CardClass::timed_optional) {
                // Rulebook §2: the tier-2 queue holds 12. The 13th is demoted to
                // ambient — it keeps its content and its choices, it just stops
                // demanding the player's attention on a timer.
                if (timed_live >= cfg_.timed_optional_queue_cap) {
                    card.card_class = CardClass::ambient;
                    card.expires_tick = 0;
                } else {
                    ++timed_live;
                    if (card.expires_tick == 0)
                        card.expires_tick = state.current_tick + cfg_.timed_optional_ttl_ticks;
                }
            } else {
                // mandatory and ambient cards do not expire (§1.1, §1.3).
                card.expires_tick = 0;
            }

            // A default outcome that names a choice the card does not carry
            // would strand the card at expiry; drop the reference so the
            // lifecycle pass retires it instead of waiting forever.
            if (card.default_choice_id != 0 &&
                find_choice(card, card.default_choice_id) == nullptr) {
                card.default_choice_id = 0;
            }

            return false;  // Keep card
        });
    delta.new_scene_cards.erase(it, delta.new_scene_cards.end());

    // Enforce per-tick cap on new cards.
    if (delta.new_scene_cards.size() > cfg_.max_scene_cards_per_tick) {
        delta.new_scene_cards.resize(cfg_.max_scene_cards_per_tick);
    }
}

}  // namespace econlife
