// drain_deferred_work — DeferredWorkQueue drain logic.
//
// Pops all items where due_tick <= current_tick and dispatches by WorkType.
// Each handler writes results to the shared DeltaBuffer. Recurring work
// items are rescheduled by pushing new items onto the queue.
//
// Invalid subject_ids are silently skipped (log warning in production).

#include "drain_deferred_work.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/world_state.h"
#include "deferred_work.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Reschedule intervals (ticks)
// ---------------------------------------------------------------------------
static constexpr uint32_t RELATIONSHIP_DECAY_INTERVAL = 30;
static constexpr uint32_t EVIDENCE_DECAY_INTERVAL = 7;

// ---------------------------------------------------------------------------
// Helper: find NPC by id
// ---------------------------------------------------------------------------
static NPC* find_npc(WorldState& world, uint32_t npc_id) {
    for (auto& n : world.significant_npcs) {
        if (n.id == npc_id)
            return &n;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// WorkType handlers
// ---------------------------------------------------------------------------

static void handle_consequence(const DeferredWorkItem& item, WorldState& world,
                               DeltaBuffer& delta) {
    // Consequence execution: look up consequence_id and apply effects.
    // ConsequenceEntry is not yet fully defined (placeholder in delta_buffer.h).
    // For now, this is a no-op handler that will be filled in Session 16
    // when the investigation → legal process pipeline is implemented.
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_transit_arrival(const DeferredWorkItem& item, WorldState& world,
                                   DeltaBuffer& delta) {
    // Transit shipment arriving at destination province.
    // Adds supply to the destination market.
    auto* payload = std::get_if<TransitPayload>(&item.payload);
    if (!payload)
        return;

    // The actual supply amount would come from the shipment record.
    // For now, transit arrival is handled by the supply_chain module
    // which will look up the shipment by id when it gets its real implementation.
    // This handler schedules no delta — the supply_chain module will process
    // arrivals when it runs.
    (void)world;
    (void)delta;
}

static void handle_npc_relationship_decay(const DeferredWorkItem& item, WorldState& world,
                                          DeltaBuffer& delta) {
    // Batch decay for one NPC's relationships.
    // Trust and fear decay toward 0 over time.
    auto* payload = std::get_if<NPCRelationshipDecayPayload>(&item.payload);
    if (!payload)
        return;

    NPC* npc = find_npc(world, payload->npc_id);
    if (!npc)
        return;

    static constexpr float TRUST_DECAY_RATE = 0.02f;  // per batch (30 ticks)
    static constexpr float FEAR_DECAY_RATE = 0.03f;   // fear decays faster

    for (auto& rel : npc->relationships) {
        // Trust decays toward 0
        if (rel.trust > 0.0f) {
            rel.trust = std::max(0.0f, rel.trust - TRUST_DECAY_RATE);
        } else if (rel.trust < 0.0f) {
            rel.trust = std::min(0.0f, rel.trust + TRUST_DECAY_RATE);
        }
        // Fear decays toward 0
        if (rel.fear > 0.0f) {
            rel.fear = std::max(0.0f, rel.fear - FEAR_DECAY_RATE);
        }
    }

    // Reschedule
    world.deferred_work_queue.push({world.current_tick + RELATIONSHIP_DECAY_INTERVAL,
                                    WorkType::npc_relationship_decay, payload->npc_id,
                                    NPCRelationshipDecayPayload{payload->npc_id}});
}

static void handle_evidence_decay(const DeferredWorkItem& item, WorldState& world,
                                  DeltaBuffer& delta) {
    // Decay actionability of one evidence token.
    auto* payload = std::get_if<EvidenceDecayPayload>(&item.payload);
    if (!payload)
        return;

    for (auto& token : world.evidence_pool) {
        if (token.id == payload->evidence_token_id && token.is_active) {
            token.actionability =
                std::max(0.0f, token.actionability -
                                   token.decay_rate * static_cast<float>(EVIDENCE_DECAY_INTERVAL));

            // If actionability drops to near-zero, retire the token
            if (token.actionability < 0.01f) {
                token.is_active = false;
            } else {
                // Reschedule only if still active
                world.deferred_work_queue.push(
                    {world.current_tick + EVIDENCE_DECAY_INTERVAL, WorkType::evidence_decay_batch,
                     payload->evidence_token_id, EvidenceDecayPayload{payload->evidence_token_id}});
            }
            break;
        }
    }
}

static void handle_npc_business_decision(const DeferredWorkItem& item, WorldState& world,
                                         DeltaBuffer& delta) {
    // Quarterly business decision for one NPCBusiness.
    // Will be implemented in Session 4+ (Production gap-fill).
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_market_recompute(const DeferredWorkItem& item, WorldState& world,
                                    DeltaBuffer& delta) {
    // Price recompute for one RegionalMarket.
    // Handled by the price_engine module — this is a trigger to force recompute
    // outside the normal tick cycle (e.g., after large supply shock).
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_investigator_meter_update(const DeferredWorkItem& item, WorldState& world,
                                             DeltaBuffer& delta) {
    // InvestigatorMeter recalc for one law enforcement NPC.
    // Will be implemented in Session 15+ (Criminal pipeline).
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_climate_downstream(const DeferredWorkItem& item, WorldState& world,
                                      DeltaBuffer& delta) {
    // Agricultural + community stress update from climate events.
    // Will be wired when seasonal_agriculture gets real implementation.
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_npc_travel_arrival(const DeferredWorkItem& item, WorldState& world,
                                      DeltaBuffer& delta) {
    // NPC physically arrives at destination province.
    NPC* npc = find_npc(world, item.subject_id);
    if (!npc)
        return;

    // The destination is encoded in the subject's current travel state.
    // For now, mark them as arrived (resident at current_province_id).
    npc->travel_status = NPCTravelStatus::resident;
}

static void handle_player_travel_arrival(const DeferredWorkItem& item, WorldState& world,
                                         DeltaBuffer& delta) {
    // Player arrives at destination province.
    // Will be wired when player travel is implemented.
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_community_stage_check(const DeferredWorkItem& item, WorldState& world,
                                         DeltaBuffer& delta) {
    // Community response stage threshold evaluation.
    // Will be implemented in Session 13 (Community Response).
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_maturation_advance(const DeferredWorkItem& item, WorldState& world,
                                      DeltaBuffer& delta) {
    // Advance one MaturationProject per active project.
    // Will be implemented when R&D/technology system is built.
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_commercialize(const DeferredWorkItem& item, WorldState& world,
                                 DeltaBuffer& delta) {
    // Player command: bring researched tech to market.
    // Will be implemented when R&D/technology system is built.
    (void)item;
    (void)world;
    (void)delta;
}

static void handle_interception_check(const DeferredWorkItem& item, WorldState& world,
                                      DeltaBuffer& delta) {
    // Per-tick criminal shipment exposure check.
    // Will be implemented in Session 15+ (Criminal pipeline).
    (void)item;
    (void)world;
    (void)delta;
}

// ---------------------------------------------------------------------------
// drain_deferred_work — main entry point
// ---------------------------------------------------------------------------
void drain_deferred_work(WorldState& world, DeltaBuffer& delta) {
    auto& queue = world.deferred_work_queue;

    while (!queue.empty() && queue.top().due_tick <= world.current_tick) {
        DeferredWorkItem item = queue.top();
        queue.pop();

        switch (item.type) {
            case WorkType::consequence:
                handle_consequence(item, world, delta);
                break;
            case WorkType::transit_arrival:
                handle_transit_arrival(item, world, delta);
                break;
            case WorkType::interception_check:
                handle_interception_check(item, world, delta);
                break;
            case WorkType::npc_relationship_decay:
                handle_npc_relationship_decay(item, world, delta);
                break;
            case WorkType::evidence_decay_batch:
                handle_evidence_decay(item, world, delta);
                break;
            case WorkType::npc_business_decision:
                handle_npc_business_decision(item, world, delta);
                break;
            case WorkType::market_recompute:
                handle_market_recompute(item, world, delta);
                break;
            case WorkType::investigator_meter_update:
                handle_investigator_meter_update(item, world, delta);
                break;
            case WorkType::climate_downstream_batch:
                handle_climate_downstream(item, world, delta);
                break;
            case WorkType::background_work:
                // Background work is non-urgent; only run if time permits.
                // For now, silently skip (no budget tracking yet).
                break;
            case WorkType::npc_travel_arrival:
                handle_npc_travel_arrival(item, world, delta);
                break;
            case WorkType::player_travel_arrival:
                handle_player_travel_arrival(item, world, delta);
                break;
            case WorkType::community_stage_check:
                handle_community_stage_check(item, world, delta);
                break;
            case WorkType::maturation_project_advance:
                handle_maturation_advance(item, world, delta);
                break;
            case WorkType::commercialize_technology:
                handle_commercialize(item, world, delta);
                break;
        }
    }
}

}  // namespace econlife
