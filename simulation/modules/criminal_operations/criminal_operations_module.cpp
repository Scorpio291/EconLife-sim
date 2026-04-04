#include "modules/criminal_operations/criminal_operations_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility functions
// ---------------------------------------------------------------------------

float CriminalOperationsModule::compute_territory_pressure(
    const CriminalOrganization& org, const std::vector<CriminalOrganization>& all_orgs) {
    float pressure = 0.0f;

    for (const auto& [prov_id, dom] : org.dominance_by_province) {
        for (const auto& other : all_orgs) {
            if (other.id == org.id)
                continue;
            auto it = other.dominance_by_province.find(prov_id);
            if (it != other.dominance_by_province.end()) {
                pressure += it->second;
            }
        }
    }

    return std::clamp(pressure, 0.0f, 10.0f);
}

float CriminalOperationsModule::compute_cash_level(float cash, float monthly_cost,
                                                   float comfortable_months) {
    float target = monthly_cost * comfortable_months;
    if (target <= 0.0f)
        return 1.0f;
    return std::clamp(cash / target, 0.0f, 10.0f);
}

float CriminalOperationsModule::compute_le_heat(const CriminalOrganization& org,
                                                const std::vector<NPC>& npcs) {
    float max_heat = 0.0f;

    for (const auto& [prov_id, dom] : org.dominance_by_province) {
        for (const auto& npc : npcs) {
            if (npc.role == NPCRole::law_enforcement && npc.current_province_id == prov_id &&
                npc.status == NPCStatus::active) {
                float heat_proxy = npc.social_capital / 100.0f;
                heat_proxy = std::clamp(heat_proxy, 0.0f, 1.0f);
                max_heat = std::max(max_heat, heat_proxy);
            }
        }
    }

    return max_heat;
}

CriminalStrategicDecision CriminalOperationsModule::evaluate_decision(float le_heat,
                                                                      float territory_pressure,
                                                                      float cash_level,
                                                                      const CriminalOperationsConfig& cfg) {
    if (le_heat >= cfg.le_heat_threshold) {
        return CriminalStrategicDecision::reduce_activity;
    }
    if (territory_pressure >= cfg.territory_pressure_conflict_threshold &&
        cash_level >= 1.0f) {
        return CriminalStrategicDecision::initiate_conflict;
    }
    if (cash_level < cfg.cash_low_threshold) {
        return CriminalStrategicDecision::reduce_headcount;
    }
    if (territory_pressure < cfg.territory_pressure_expand_threshold &&
        le_heat < cfg.le_heat_expand_threshold) {
        return CriminalStrategicDecision::expand_territory;
    }
    return CriminalStrategicDecision::maintain;
}

uint8_t CriminalOperationsModule::compute_decision_offset(uint32_t org_id, uint32_t quarterly_interval) {
    return static_cast<uint8_t>(org_id % quarterly_interval);
}

TerritorialConflictStage CriminalOperationsModule::advance_conflict_stage(
    TerritorialConflictStage current) {
    switch (current) {
        case TerritorialConflictStage::none:
            return TerritorialConflictStage::economic;
        case TerritorialConflictStage::economic:
            return TerritorialConflictStage::intelligence_harassment;
        case TerritorialConflictStage::intelligence_harassment:
            return TerritorialConflictStage::property_violence;
        case TerritorialConflictStage::property_violence:
            return TerritorialConflictStage::personnel_violence;
        case TerritorialConflictStage::personnel_violence:
            return TerritorialConflictStage::open_warfare;
        case TerritorialConflictStage::open_warfare:
            return TerritorialConflictStage::resolution;
        case TerritorialConflictStage::resolution:
            return TerritorialConflictStage::none;
    }
    return TerritorialConflictStage::none;
}

float CriminalOperationsModule::initial_dominance_seed(float expansion_initial_dominance) {
    return expansion_initial_dominance;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void CriminalOperationsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    std::sort(
        organizations_.begin(), organizations_.end(),
        [](const CriminalOrganization& a, const CriminalOrganization& b) { return a.id < b.id; });

    process_dormant_orgs(state);

    for (auto& org : organizations_) {
        if (org.member_npc_ids.empty())
            continue;

        if (state.current_tick == org.strategic_decision_tick) {
            process_strategic_decision(org, state, delta);
            org.strategic_decision_tick = state.current_tick + cfg_.quarterly_interval;
        }
    }

    process_conflict_states(state, delta);
}

void CriminalOperationsModule::process_strategic_decision(CriminalOrganization& org,
                                                          const WorldState& state,
                                                          DeltaBuffer& delta) {
    float le_heat = compute_le_heat(org, state.significant_npcs);
    float territory_pressure = compute_territory_pressure(org, organizations_);

    float monthly_cost = 0.0f;
    for (uint32_t biz_id : org.income_source_ids) {
        for (const auto& biz : state.npc_businesses) {
            if (biz.id == biz_id) {
                monthly_cost += biz.cost_per_tick * 30.0f;
                break;
            }
        }
    }

    float cash_level =
        compute_cash_level(org.cash, monthly_cost, cfg_.cash_comfortable_months);

    CriminalStrategicDecision decision = evaluate_decision(le_heat, territory_pressure, cash_level, cfg_);

    switch (decision) {
        case CriminalStrategicDecision::reduce_activity: {
            // Reduce revenue for all criminal businesses owned by this org.
            // Process in ascending business id order for deterministic accumulation.
            std::vector<const NPCBusiness*> owned_criminal_bizs;
            for (const auto& biz : state.npc_businesses) {
                if (biz.criminal_sector && biz.owner_id == org.leadership_npc_id) {
                    owned_criminal_bizs.push_back(&biz);
                }
            }
            std::sort(owned_criminal_bizs.begin(), owned_criminal_bizs.end(),
                      [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });
            for (const auto* biz : owned_criminal_bizs) {
                BusinessDelta bd;
                bd.business_id = biz->id;
                bd.revenue_per_tick_update = biz->revenue_per_tick * 0.8f;
                delta.business_deltas.push_back(bd);
            }
            break;
        }

        case CriminalStrategicDecision::expand_territory: {
            // Write criminal dominance increase to the org's primary province.
            // Target is the province where the org has the highest current dominance.
            if (!org.dominance_by_province.empty()) {
                uint32_t target_province_id = org.dominance_by_province.begin()->first;
                float best_dom = org.dominance_by_province.begin()->second;
                for (const auto& [prov_id, dom] : org.dominance_by_province) {
                    if (dom > best_dom) {
                        best_dom = dom;
                        target_province_id = prov_id;
                    }
                }
                RegionDelta rd;
                rd.region_id = target_province_id;
                rd.criminal_dominance_delta = 0.05f;
                delta.region_deltas.push_back(rd);
            }
            break;
        }

        case CriminalStrategicDecision::initiate_conflict:
            if (org.conflict_state == TerritorialConflictStage::none) {
                uint32_t best_rival = 0;
                float best_rival_dom = 0.0f;
                for (const auto& other : organizations_) {
                    if (other.id == org.id)
                        continue;
                    for (const auto& [prov_id, dom] : org.dominance_by_province) {
                        auto it = other.dominance_by_province.find(prov_id);
                        if (it != other.dominance_by_province.end() &&
                            it->second > best_rival_dom) {
                            best_rival_dom = it->second;
                            best_rival = other.id;
                        }
                    }
                }
                if (best_rival != 0) {
                    org.conflict_state = TerritorialConflictStage::economic;
                    org.conflict_rival_org_id = best_rival;
                }
            }
            break;

        case CriminalStrategicDecision::reduce_headcount: {
            // Signal a personnel reduction by queuing a consequence entry
            // keyed to the org's leadership NPC.
            ConsequenceDelta cd;
            cd.new_entry_id = org.leadership_npc_id;
            delta.consequence_deltas.push_back(cd);
            break;
        }

        case CriminalStrategicDecision::maintain:
        default:
            break;
    }
}

void CriminalOperationsModule::process_conflict_states(const WorldState& state,
                                                       DeltaBuffer& delta) {
    for (auto& org : organizations_) {
        if (org.conflict_state == TerritorialConflictStage::none)
            continue;
        if (org.conflict_state == TerritorialConflictStage::resolution) {
            org.conflict_state = TerritorialConflictStage::none;
            org.conflict_rival_org_id = 0;
            continue;
        }

        bool rival_alive = false;
        for (const auto& other : organizations_) {
            if (other.id == org.conflict_rival_org_id && !other.member_npc_ids.empty()) {
                rival_alive = true;
                break;
            }
        }

        if (!rival_alive) {
            org.conflict_state = TerritorialConflictStage::resolution;
            continue;
        }

        // Advance conflict stage by one step each tick the conflict is active.
        org.conflict_state = advance_conflict_stage(org.conflict_state);

        // Personnel violence and above generates evidence
        if (org.conflict_state >= TerritorialConflictStage::personnel_violence) {
            EvidenceDelta ev_delta;
            EvidenceToken token;
            token.id = state.current_tick * 100 + org.id;
            token.type = EvidenceType::physical;
            token.source_npc_id = org.leadership_npc_id;
            token.target_npc_id = org.leadership_npc_id;
            token.actionability = 0.70f;
            token.decay_rate = 0.002f;
            token.created_tick = state.current_tick;
            token.province_id =
                org.dominance_by_province.empty() ? 0 : org.dominance_by_province.begin()->first;
            token.is_active = true;
            ev_delta.new_token = token;
            delta.evidence_deltas.push_back(ev_delta);
        }
    }
}

void CriminalOperationsModule::process_dormant_orgs(const WorldState& state) {
    for (auto& org : organizations_) {
        if (org.member_npc_ids.empty()) {
            for (auto& [prov_id, dom] : org.dominance_by_province) {
                dom -= cfg_.dormant_dominance_decay_rate;
                dom = std::max(0.0f, dom);
            }
        }
    }
}

}  // namespace econlife
