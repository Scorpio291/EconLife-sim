// apply_deltas — mutates WorldState by applying accumulated DeltaBuffer changes.
//
// Rules:
// - Additive optional<float> fields: add value, clamp to domain range
// - Replacement optional fields: overwrite target
// - Append vectors: push_back new entries
// - NaN protection: any NaN delta is treated as 0.0

#include "apply_deltas.h"

#include <algorithm>
#include <cmath>

#include "player.h"
#include "world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
static float clamp_neg1_1(float v) {
    return std::clamp(v, -1.0f, 1.0f);
}
static float safe_add(float base, float delta) {
    return std::isnan(delta) ? base : base + delta;
}

// Magnitude ceilings: prevent float overflow from runaway accumulation.
// These are safety clamps, not game mechanics — if values hit these,
// the economic loop has a balance issue to investigate.
static constexpr float BUSINESS_CASH_CEILING = 1.0e10f;
static constexpr float NPC_CAPITAL_CEILING = 1.0e9f;

// ---------------------------------------------------------------------------
// apply_npc_deltas
// ---------------------------------------------------------------------------
static void apply_npc_deltas(WorldState& world, const std::vector<NPCDelta>& deltas) {
    for (const auto& d : deltas) {
        // Find NPC by id (linear scan; acceptable at <2000 NPCs)
        NPC* npc = nullptr;
        for (auto& n : world.significant_npcs) {
            if (n.id == d.npc_id) {
                npc = &n;
                break;
            }
        }
        if (!npc)
            continue;

        // capital_delta: additive, floor at 0, ceiling at NPC_CAPITAL_CEILING
        if (d.capital_delta.has_value()) {
            npc->capital = safe_add(npc->capital, *d.capital_delta);
            if (std::isinf(npc->capital) || std::isnan(npc->capital)) {
                npc->capital = NPC_CAPITAL_CEILING;
            }
            npc->capital = std::clamp(npc->capital, 0.0f, NPC_CAPITAL_CEILING);
        }

        // new_status: replacement
        if (d.new_status.has_value()) {
            npc->status = *d.new_status;
        }

        // new_memory_entry: append (with overflow protection)
        if (d.new_memory_entry.has_value()) {
            if (npc->memory_log.size() >= MAX_MEMORY_ENTRIES) {
                // Evict lowest-decay entry
                auto weakest = std::min_element(
                    npc->memory_log.begin(), npc->memory_log.end(),
                    [](const MemoryEntry& a, const MemoryEntry& b) { return a.decay < b.decay; });
                if (weakest != npc->memory_log.end()) {
                    npc->memory_log.erase(weakest);
                }
            }
            npc->memory_log.push_back(*d.new_memory_entry);
        }

        // updated_relationship: upsert by target_npc_id
        if (d.updated_relationship.has_value()) {
            const auto& rel = *d.updated_relationship;
            bool found = false;
            for (auto& existing : npc->relationships) {
                if (existing.target_npc_id == rel.target_npc_id) {
                    // Update: merge trust/fear additively, clamp
                    existing.trust = clamp_neg1_1(existing.trust + rel.trust);
                    existing.fear = clamp01(existing.fear + rel.fear);
                    existing.obligation_balance += rel.obligation_balance;
                    existing.last_interaction_tick = rel.last_interaction_tick;
                    if (rel.is_movement_ally)
                        existing.is_movement_ally = true;
                    // Enforce recovery ceiling
                    if (existing.trust > existing.recovery_ceiling) {
                        existing.trust = existing.recovery_ceiling;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Insert new relationship
                Relationship new_rel = rel;
                new_rel.trust = clamp_neg1_1(new_rel.trust);
                new_rel.fear = clamp01(new_rel.fear);
                if (new_rel.recovery_ceiling < 0.15f)
                    new_rel.recovery_ceiling = 0.15f;
                npc->relationships.push_back(new_rel);
            }
        }

        // motivation_delta: additive (applied to first non-zero weight, then renormalize)
        // In practice, modules should write specific motivation shifts via a more
        // detailed mechanism. For now, apply as a uniform shift to financial_gain slot.
        if (d.motivation_delta.has_value() && !std::isnan(*d.motivation_delta)) {
            npc->motivations.weights[0] += *d.motivation_delta;
            // Renormalize to sum to 1.0
            float sum = 0.0f;
            for (float w : npc->motivations.weights)
                sum += std::max(0.0f, w);
            if (sum > 0.0f) {
                for (float& w : npc->motivations.weights) {
                    w = std::max(0.0f, w) / sum;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_player_delta
// ---------------------------------------------------------------------------
static void apply_player_delta(WorldState& world, const PlayerDelta& d) {
    if (!world.player)
        return;
    PlayerCharacter& p = *world.player;

    if (d.health_delta.has_value()) {
        p.health.current_health = clamp01(safe_add(p.health.current_health, *d.health_delta));
    }
    if (d.wealth_delta.has_value()) {
        p.wealth = safe_add(p.wealth, *d.wealth_delta);
    }
    if (d.exhaustion_delta.has_value()) {
        p.health.exhaustion_accumulator =
            clamp01(safe_add(p.health.exhaustion_accumulator, *d.exhaustion_delta));
    }
    if (d.skill_delta.has_value()) {
        const auto& sd = *d.skill_delta;
        for (auto& skill : p.skills) {
            if (skill.domain == static_cast<SkillDomain>(sd.skill_id)) {
                skill.level = std::clamp(safe_add(skill.level, sd.value), SKILL_DOMAIN_FLOOR, 1.0f);
                break;
            }
        }
    }
    if (d.new_evidence_awareness.has_value()) {
        EvidenceAwarenessEntry entry{};
        entry.token_id = *d.new_evidence_awareness;
        entry.discovery_tick = world.current_tick;
        entry.source_npc_id = 0;
        p.evidence_awareness_map.push_back(entry);
    }
    if (d.relationship_delta.has_value()) {
        const auto& rd = *d.relationship_delta;
        bool found = false;
        for (auto& rel : p.relationships) {
            if (rel.target_npc_id == rd.target_npc_id) {
                rel.trust = clamp_neg1_1(rel.trust + rd.trust_delta);
                if (rel.trust > rel.recovery_ceiling)
                    rel.trust = rel.recovery_ceiling;
                found = true;
                break;
            }
        }
        if (!found) {
            Relationship new_rel{};
            new_rel.target_npc_id = rd.target_npc_id;
            new_rel.trust = clamp_neg1_1(rd.trust_delta);
            new_rel.fear = 0.0f;
            new_rel.obligation_balance = 0.0f;
            new_rel.last_interaction_tick = world.current_tick;
            new_rel.is_movement_ally = false;
            new_rel.recovery_ceiling = 1.0f;
            p.relationships.push_back(new_rel);
        }
    }
}

// ---------------------------------------------------------------------------
// apply_business_deltas
// ---------------------------------------------------------------------------
static void apply_business_deltas(WorldState& world, const std::vector<BusinessDelta>& deltas) {
    for (const auto& d : deltas) {
        for (auto& biz : world.npc_businesses) {
            if (biz.id == d.business_id) {
                if (d.cash_delta.has_value()) {
                    biz.cash = safe_add(biz.cash, *d.cash_delta);
                    // Business cash can go negative (indicates insolvency).
                    // Clamp magnitude to prevent float overflow/Inf/NaN.
                    if (std::isinf(biz.cash) || std::isnan(biz.cash)) {
                        biz.cash = (biz.cash > 0.0f || std::isnan(biz.cash))
                                       ? BUSINESS_CASH_CEILING
                                       : -BUSINESS_CASH_CEILING;
                    }
                    biz.cash = std::clamp(biz.cash, -BUSINESS_CASH_CEILING, BUSINESS_CASH_CEILING);
                }
                if (d.revenue_per_tick_update.has_value()) {
                    biz.revenue_per_tick = std::max(0.0f, *d.revenue_per_tick_update);
                }
                if (d.cost_per_tick_update.has_value()) {
                    biz.cost_per_tick = std::max(0.0f, *d.cost_per_tick_update);
                }
                if (d.output_quality_update.has_value()) {
                    biz.output_quality = clamp01(*d.output_quality_update);
                }
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_market_deltas
// ---------------------------------------------------------------------------
static constexpr float MARKET_SUPPLY_CEILING = 1.0e8f;
static constexpr float MARKET_PRICE_CEILING = 1.0e6f;

static void apply_market_deltas(WorldState& world, const std::vector<MarketDelta>& deltas) {
    for (const auto& d : deltas) {
        for (auto& m : world.regional_markets) {
            if (m.good_id == d.good_id && m.province_id == d.region_id) {
                if (d.supply_delta.has_value()) {
                    m.supply = std::clamp(safe_add(m.supply, *d.supply_delta), 0.0f,
                                          MARKET_SUPPLY_CEILING);
                }
                if (d.demand_buffer_delta.has_value()) {
                    m.demand_buffer = std::clamp(safe_add(m.demand_buffer, *d.demand_buffer_delta),
                                                 0.0f, MARKET_SUPPLY_CEILING);
                }
                if (d.spot_price_override.has_value()) {
                    float price = *d.spot_price_override;
                    m.spot_price = std::clamp(std::isnan(price) ? m.spot_price : price, 0.001f,
                                              MARKET_PRICE_CEILING);
                }
                if (d.equilibrium_price_override.has_value()) {
                    float price = *d.equilibrium_price_override;
                    m.equilibrium_price =
                        std::clamp(std::isnan(price) ? m.equilibrium_price : price, 0.001f,
                                   MARKET_PRICE_CEILING);
                }
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_evidence_deltas
// ---------------------------------------------------------------------------
static void apply_evidence_deltas(WorldState& world, const std::vector<EvidenceDelta>& deltas) {
    for (const auto& d : deltas) {
        if (d.new_token.has_value()) {
            EvidenceToken token = *d.new_token;
            // Assign id if not set
            if (token.id == 0) {
                uint32_t max_id = 0;
                for (const auto& t : world.evidence_pool) {
                    if (t.id > max_id)
                        max_id = t.id;
                }
                token.id = max_id + 1;
            }
            world.evidence_pool.push_back(token);
        }
        if (d.retired_token_id.has_value()) {
            for (auto& t : world.evidence_pool) {
                if (t.id == *d.retired_token_id) {
                    t.is_active = false;
                    break;
                }
            }
        }
        if (d.updated_token_id.has_value() && d.updated_actionability.has_value()) {
            for (auto& t : world.evidence_pool) {
                if (t.id == *d.updated_token_id) {
                    t.actionability = *d.updated_actionability;
                    break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_region_deltas
// ---------------------------------------------------------------------------
static void apply_region_deltas(WorldState& world, const std::vector<RegionDelta>& deltas) {
    for (const auto& d : deltas) {
        for (auto& prov : world.provinces) {
            if (prov.region_id == d.region_id) {
                auto& c = prov.conditions;
                if (d.stability_delta.has_value()) {
                    c.stability_score = clamp01(safe_add(c.stability_score, *d.stability_delta));
                }
                if (d.inequality_delta.has_value()) {
                    c.inequality_index = clamp01(safe_add(c.inequality_index, *d.inequality_delta));
                }
                if (d.crime_rate_delta.has_value()) {
                    c.crime_rate = clamp01(safe_add(c.crime_rate, *d.crime_rate_delta));
                }
                if (d.addiction_rate_delta.has_value()) {
                    c.addiction_rate = clamp01(safe_add(c.addiction_rate, *d.addiction_rate_delta));
                }
                if (d.criminal_dominance_delta.has_value()) {
                    c.criminal_dominance_index =
                        clamp01(safe_add(c.criminal_dominance_index, *d.criminal_dominance_delta));
                }
                if (d.cohesion_delta.has_value()) {
                    prov.community.cohesion =
                        clamp01(safe_add(prov.community.cohesion, *d.cohesion_delta));
                }
                if (d.grievance_delta.has_value()) {
                    prov.community.grievance_level =
                        clamp01(safe_add(prov.community.grievance_level, *d.grievance_delta));
                }
                if (d.institutional_trust_delta.has_value()) {
                    prov.community.institutional_trust = clamp01(
                        safe_add(prov.community.institutional_trust, *d.institutional_trust_delta));
                }
                if (d.resource_access_delta.has_value()) {
                    prov.community.resource_access =
                        clamp01(safe_add(prov.community.resource_access, *d.resource_access_delta));
                }
                if (d.response_stage_replacement.has_value()) {
                    prov.community.response_stage =
                        std::min(*d.response_stage_replacement, static_cast<uint8_t>(6));
                }
                if (d.infrastructure_rating_delta.has_value()) {
                    prov.infrastructure_rating = clamp01(
                        safe_add(prov.infrastructure_rating, *d.infrastructure_rating_delta));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_currency_deltas
// ---------------------------------------------------------------------------
static void apply_currency_deltas(WorldState& world, const std::vector<CurrencyDelta>& deltas) {
    for (const auto& d : deltas) {
        for (auto& cur : world.currencies) {
            if (cur.nation_id == d.nation_id) {
                if (d.usd_rate_update.has_value()) {
                    cur.usd_rate = std::max(0.001f, *d.usd_rate_update);
                }
                if (d.pegged_update.has_value()) {
                    cur.pegged = *d.pegged_update;
                }
                if (d.foreign_reserves_delta.has_value()) {
                    cur.foreign_reserves =
                        clamp01(safe_add(cur.foreign_reserves, *d.foreign_reserves_delta));
                }
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_append_deltas — calendar entries, scene cards, obligations
// ---------------------------------------------------------------------------
static void apply_append_deltas(WorldState& world, DeltaBuffer& delta) {
    for (auto& entry : delta.new_calendar_entries) {
        world.calendar.push_back(std::move(entry));
    }
    for (auto& card : delta.new_scene_cards) {
        world.pending_scene_cards.push_back(std::move(card));
    }
    for (auto& node : delta.new_obligation_nodes) {
        if (node.id == 0) {
            uint32_t max_id = 0;
            for (const auto& n : world.obligation_network) {
                if (n.id > max_id)
                    max_id = n.id;
            }
            node.id = max_id + 1;
        }
        world.obligation_network.push_back(std::move(node));
    }
}

// ---------------------------------------------------------------------------
// apply_deltas — main entry point
// ---------------------------------------------------------------------------
void apply_deltas(WorldState& world, DeltaBuffer& delta) {
    apply_npc_deltas(world, delta.npc_deltas);
    apply_player_delta(world, delta.player_delta);
    apply_business_deltas(world, delta.business_deltas);
    apply_market_deltas(world, delta.market_deltas);
    apply_evidence_deltas(world, delta.evidence_deltas);
    apply_region_deltas(world, delta.region_deltas);
    apply_currency_deltas(world, delta.currency_deltas);
    apply_append_deltas(world, delta);

    // Clear the delta buffer for next step
    delta.npc_deltas.clear();
    delta.player_delta = PlayerDelta{};
    delta.market_deltas.clear();
    delta.evidence_deltas.clear();
    delta.consequence_deltas.clear();
    delta.business_deltas.clear();
    delta.region_deltas.clear();
    delta.currency_deltas.clear();
    delta.new_calendar_entries.clear();
    delta.new_scene_cards.clear();
    delta.new_obligation_nodes.clear();
}

// ---------------------------------------------------------------------------
// apply_cross_province_deltas
// ---------------------------------------------------------------------------
void apply_cross_province_deltas(WorldState& world) {
    auto& cpd = world.cross_province_delta_buffer;

    for (const auto& entry : cpd.entries) {
        if (entry.due_tick > world.current_tick)
            continue;

        if (entry.npc_delta.has_value()) {
            // Apply NPC delta to the target province's NPC
            std::vector<NPCDelta> single = {*entry.npc_delta};
            apply_npc_deltas(world, single);
        }
        if (entry.market_delta.has_value()) {
            std::vector<MarketDelta> single = {*entry.market_delta};
            apply_market_deltas(world, single);
        }
    }

    cpd.entries.clear();
}

}  // namespace econlife
