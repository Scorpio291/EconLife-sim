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
#include <unordered_map>
#include <vector>

#include "core/world_state/apply_deltas.h"
#include "core/world_state/world_state.h"
#include "deferred_work.h"
#include "modules/legal_process/legal_process_types.h"  // CaseSeverity

namespace econlife {

// Reschedule intervals and decay rates are passed via DrainConfig.

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
using NPCIndex = std::unordered_map<uint32_t, const NPC*>;

static const NPC* find_npc(const NPCIndex& index, uint32_t npc_id) {
    auto it = index.find(npc_id);
    return it == index.end() ? nullptr : it->second;
}

static NPCIndex build_npc_index(const WorldState& world) {
    NPCIndex index;
    index.reserve(world.significant_npcs.size());
    for (const auto& n : world.significant_npcs) {
        index[n.id] = &n;
    }
    return index;
}

// ---------------------------------------------------------------------------
// WorkType handlers
// ---------------------------------------------------------------------------

static void handle_consequence(const DeferredWorkItem& item, WorldState& world,
                               DeltaBuffer& delta) {
    // Legacy DeferredWorkQueue path. The live GDD §21 consequence system uses
    // WorldState.consequence_queue, fired by process_consequence_queue() at the
    // end of this drain; nothing schedules WorkType::consequence items, so this
    // handler is intentionally inert.
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
                                          DeltaBuffer& delta, const DrainConfig& cfg,
                                          const NPCIndex& npc_index) {
    // Batch decay for one NPC's relationships.
    // Trust and fear decay toward 0 over time.
    // Write relationship updates via DeltaBuffer (not direct mutation).
    auto* payload = std::get_if<NPCRelationshipDecayPayload>(&item.payload);
    if (!payload)
        return;

    const NPC* npc = find_npc(npc_index, payload->npc_id);
    if (!npc)
        return;

    const float TRUST_DECAY_RATE = cfg.trust_decay_rate_per_batch;
    const float FEAR_DECAY_RATE = cfg.fear_decay_rate_per_batch;

    for (const auto& rel : npc->relationships) {
        float trust_change = 0.0f;
        float fear_change = 0.0f;

        // Trust decays toward 0 — compute the additive delta
        if (rel.trust > 0.0f) {
            trust_change = -std::min(TRUST_DECAY_RATE, rel.trust);
        } else if (rel.trust < 0.0f) {
            trust_change = std::min(TRUST_DECAY_RATE, -rel.trust);
        }
        // Fear decays toward 0
        if (rel.fear > 0.0f) {
            fear_change = -std::min(FEAR_DECAY_RATE, rel.fear);
        }

        // Only emit delta if values actually changed
        if (trust_change != 0.0f || fear_change != 0.0f) {
            NPCDelta npc_delta{};
            npc_delta.npc_id = payload->npc_id;
            // apply_deltas treats updated_relationship as additive for trust/fear
            Relationship delta_rel{};
            delta_rel.target_npc_id = rel.target_npc_id;
            delta_rel.trust = trust_change;
            delta_rel.fear = fear_change;
            delta_rel.obligation_balance = 0.0f;
            delta_rel.last_interaction_tick = rel.last_interaction_tick;
            delta_rel.is_movement_ally = false;
            delta_rel.recovery_ceiling = rel.recovery_ceiling;
            npc_delta.updated_relationship = delta_rel;
            delta.npc_deltas.push_back(npc_delta);
        }
    }

    // Reschedule
    world.deferred_work_queue.push({world.current_tick + cfg.relationship_decay_interval,
                                    WorkType::npc_relationship_decay, payload->npc_id,
                                    NPCRelationshipDecayPayload{payload->npc_id}});
}

static void handle_evidence_decay(const DeferredWorkItem& item, WorldState& world,
                                  DeltaBuffer& delta, const DrainConfig& cfg) {
    // Decay actionability of one evidence token via DeltaBuffer.
    auto* payload = std::get_if<EvidenceDecayPayload>(&item.payload);
    if (!payload)
        return;

    for (const auto& token : world.evidence_pool) {
        if (token.id == payload->evidence_token_id && token.is_active) {
            float new_actionability = std::max(
                0.0f, token.actionability -
                          token.decay_rate * static_cast<float>(cfg.evidence_decay_interval));

            EvidenceDelta ed{};
            if (new_actionability < 0.01f) {
                // Retire the token and zero its actionability
                ed.retired_token_id = payload->evidence_token_id;
                ed.updated_token_id = payload->evidence_token_id;
                ed.updated_actionability = 0.0f;
            } else {
                // Update actionability
                ed.updated_token_id = payload->evidence_token_id;
                ed.updated_actionability = new_actionability;
                // Reschedule only if still active
                world.deferred_work_queue.push({world.current_tick + cfg.evidence_decay_interval,
                                                WorkType::evidence_decay_batch,
                                                payload->evidence_token_id,
                                                EvidenceDecayPayload{payload->evidence_token_id}});
            }
            delta.evidence_deltas.push_back(ed);
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

static void handle_npc_travel_arrival(const DeferredWorkItem& item, WorldState& /*world*/,
                                      DeltaBuffer& delta, const NPCIndex& npc_index) {
    // NPC physically arrives at destination province.
    // Write status change via DeltaBuffer (not direct mutation).
    const NPC* npc = find_npc(npc_index, item.subject_id);
    if (!npc)
        return;

    NPCDelta npc_delta{};
    npc_delta.npc_id = item.subject_id;
    npc_delta.new_travel_status = NPCTravelStatus::resident;
    delta.npc_deltas.push_back(npc_delta);
}

static void handle_player_travel_arrival(const DeferredWorkItem& item, WorldState& world,
                                         DeltaBuffer& delta) {
    // Player arrives at destination province.
    if (!world.player)
        return;

    auto* payload = std::get_if<PlayerTravelPayload>(&item.payload);
    if (!payload)
        return;

    // Verify destination province exists.
    bool province_exists = false;
    for (const auto& p : world.provinces) {
        if (p.id == payload->destination_province_id) {
            province_exists = true;
            break;
        }
    }
    if (!province_exists)
        return;

    // Write location change via PlayerDelta (replacement fields).
    delta.player_delta.new_province_id = payload->destination_province_id;
    delta.player_delta.new_travel_status = NPCTravelStatus::resident;
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
// process_consequence_queue — GDD §21 delayed-consequence firing.
// Scans WorldState.consequence_queue for due, un-fired, un-cancelled entries
// and applies a category-specific effect via the DeltaBuffer. Fires regardless
// of whether the source NPC is still alive (the queue is the game's memory).
// Fired/cancelled entries are pruned afterwards.
// ---------------------------------------------------------------------------
static void process_consequence_queue(WorldState& world, DeltaBuffer& delta) {
    bool any_resolved = false;
    for (auto& e : world.consequence_queue) {
        if (e.fired || e.cancelled) {
            any_resolved = true;
            continue;
        }
        if (e.scheduled_tick > world.current_tick)
            continue;

        // A consequence with no province lands nowhere: skip the region-scoped
        // effects rather than defaulting them onto province 0, which is a real
        // province and was silently absorbing every unattributed grievance and
        // trust hit in the simulation. Region-less categories still run below.
        const bool has_province = e.province_id != kConsequenceNoProvince;

        switch (e.category) {
            case ConsequenceCategory::financial_investigation:
            case ConsequenceCategory::criminal_investigation:
            case ConsequenceCategory::legal_proceeding:
            case ConsequenceCategory::whistle_blower_contact: {
                LegalCaseSeedDelta seed{};
                seed.defendant_npc_id = e.target_id;
                seed.lead_investigator_id = e.source_npc_id;
                seed.severity = static_cast<uint8_t>(
                    e.category == ConsequenceCategory::legal_proceeding ? CaseSeverity::major
                    : e.category == ConsequenceCategory::criminal_investigation
                        ? CaseSeverity::serious
                        : CaseSeverity::moderate);
                seed.province_id = e.province_id;
                seed.initial_evidence_weight = 0.30f;
                delta.new_legal_case_seeds.push_back(seed);
                break;
            }
            case ConsequenceCategory::media_exposure:
            case ConsequenceCategory::political_consequence: {
                if (!has_province)
                    break;
                RegionDelta rd;
                rd.region_id = e.province_id;
                rd.institutional_trust_delta = -0.03f;
                delta.region_deltas.push_back(rd);
                break;
            }
            case ConsequenceCategory::social_consequence: {
                if (!has_province)
                    break;
                RegionDelta rd;
                rd.region_id = e.province_id;
                // Genuine social harms (deaths, closures, defaults, interceptions)
                // are rare, discrete events; community_response's EMA relaxes the
                // bump back toward the material-conditions baseline afterward.
                rd.grievance_delta = 0.03f;
                delta.region_deltas.push_back(rd);
                break;
            }
            case ConsequenceCategory::rival_escalation: {
                if (!has_province)
                    break;
                RegionDelta rd;
                rd.region_id = e.province_id;
                rd.criminal_dominance_delta = 0.02f;
                delta.region_deltas.push_back(rd);
                break;
            }
        }
        e.fired = true;
        any_resolved = true;
    }
    if (any_resolved) {
        auto& q = world.consequence_queue;
        q.erase(std::remove_if(q.begin(), q.end(),
                               [](const ConsequenceEntry& e) { return e.fired || e.cancelled; }),
                q.end());
    }
}

// ---------------------------------------------------------------------------
// process_investigator_meters — per-NPC InvestigatorMeter lifecycle.
// Decays each active meter, derives its stage from the level, and opens a legal
// case (LegalCaseSeedDelta) once it reaches raid_imminent. Domain modules
// (facility_signals, weapons embargo, informant disclosure, antitrust) fill the
// meter via NPCDelta; this runs at tick start on the accumulated levels.
// ---------------------------------------------------------------------------
static void process_investigator_meters(WorldState& world, DeltaBuffer& delta) {
    constexpr float DECAY_PER_TICK = 0.01f;
    constexpr float SURVEILLANCE = 0.30f;
    constexpr float FORMAL_INQUIRY = 0.60f;
    constexpr float RAID_IMMINENT = 0.80f;

    for (auto& npc : world.significant_npcs) {
        auto& m = npc.investigator_meter;
        if (m.current_level <= 0.0f && m.status == InvestigatorMeterStatus::inactive)
            continue;

        m.current_level = std::max(0.0f, m.current_level - DECAY_PER_TICK);

        InvestigatorMeterStatus status = InvestigatorMeterStatus::inactive;
        if (m.current_level >= RAID_IMMINENT)
            status = InvestigatorMeterStatus::raid_imminent;
        else if (m.current_level >= FORMAL_INQUIRY)
            status = InvestigatorMeterStatus::formal_inquiry;
        else if (m.current_level >= SURVEILLANCE)
            status = InvestigatorMeterStatus::surveillance;
        m.status = status;

        // Open a case once on reaching raid_imminent.
        if (status == InvestigatorMeterStatus::raid_imminent && !m.case_escalated &&
            m.target_npc_id != 0) {
            LegalCaseSeedDelta seed{};
            seed.defendant_npc_id = m.target_npc_id;
            seed.lead_investigator_id = npc.id;
            seed.severity = static_cast<uint8_t>(CaseSeverity::serious);
            seed.province_id = npc.current_province_id;
            seed.initial_evidence_weight = m.current_level;
            delta.new_legal_case_seeds.push_back(seed);
            m.case_escalated = true;
        }

        // Reset when fully decayed.
        if (m.current_level <= 0.0f) {
            m.status = InvestigatorMeterStatus::inactive;
            m.target_npc_id = 0;
            m.case_escalated = false;
            m.opened_tick = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// drain_deferred_work — main entry point
// ---------------------------------------------------------------------------
void drain_deferred_work(WorldState& world, DeltaBuffer& delta, const DrainConfig& cfg) {
    auto& queue = world.deferred_work_queue;

    // Build the NPC lookup index once for the whole drain pass; the NPC vector
    // is not mutated during the drain (mutations go through DeltaBuffer and
    // apply_deltas runs after).
    const NPCIndex npc_index = build_npc_index(world);

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
                handle_npc_relationship_decay(item, world, delta, cfg, npc_index);
                break;
            case WorkType::evidence_decay_batch:
                handle_evidence_decay(item, world, delta, cfg);
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
                handle_npc_travel_arrival(item, world, delta, npc_index);
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

    // Fire any consequences scheduled for this tick (GDD §21).
    process_consequence_queue(world, delta);

    // Per-NPC investigation meters (decay, stage, escalation to a legal case).
    process_investigator_meters(world, delta);
}

}  // namespace econlife
