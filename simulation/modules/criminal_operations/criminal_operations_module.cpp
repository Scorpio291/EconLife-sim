#include "modules/criminal_operations/criminal_operations_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <vector>

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

CriminalStrategicDecision CriminalOperationsModule::evaluate_decision(
    float le_heat, float territory_pressure, float cash_level,
    const CriminalOperationsConfig& cfg) {
    if (le_heat >= cfg.le_heat_threshold) {
        return CriminalStrategicDecision::reduce_activity;
    }
    if (territory_pressure >= cfg.territory_pressure_conflict_threshold && cash_level >= 1.0f) {
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

uint8_t CriminalOperationsModule::compute_decision_offset(uint32_t org_id,
                                                          uint32_t quarterly_interval) {
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

void CriminalOperationsModule::form_organizations(const WorldState& state) {
    if (formed_)
        return;
    formed_ = true;

    // Provinces already covered by an organization. On a loaded save the orgs
    // arrive via deserialize_state with their province dominance populated, so
    // this makes formation a no-op for them (idempotent).
    std::set<uint32_t> covered;
    for (const auto& org : organizations_) {
        for (const auto& [prov_id, dom] : org.dominance_by_province) {
            (void)dom;
            covered.insert(prov_id);
        }
    }

    auto is_criminal_role = [](NPCRole r) {
        return r == NPCRole::criminal_operator || r == NPCRole::criminal_enforcer ||
               r == NPCRole::fixer;
    };

    // Bucket active criminal NPCs by current province; track the lowest-id
    // criminal_operator per province for leadership.
    std::map<uint32_t, std::vector<uint32_t>> criminal_npcs_by_province;
    std::map<uint32_t, uint32_t> leader_by_province;
    for (const auto& npc : state.significant_npcs) {
        if (npc.status != NPCStatus::active || !is_criminal_role(npc.role))
            continue;
        criminal_npcs_by_province[npc.current_province_id].push_back(npc.id);
        if (npc.role == NPCRole::criminal_operator) {
            auto it = leader_by_province.find(npc.current_province_id);
            if (it == leader_by_province.end() || npc.id < it->second)
                leader_by_province[npc.current_province_id] = npc.id;
        }
    }

    // Criminal-sector businesses become the org's income sources.
    std::map<uint32_t, std::vector<uint32_t>> criminal_biz_by_province;
    for (const auto& biz : state.npc_businesses) {
        if (biz.criminal_sector)
            criminal_biz_by_province[biz.province_id].push_back(biz.id);
    }

    // std::map iterates in ascending key order, so provinces (and the npc/biz
    // id vectors after sorting) are processed deterministically.
    for (auto& [prov_id, npc_ids] : criminal_npcs_by_province) {
        if (covered.count(prov_id) || npc_ids.empty())
            continue;
        std::sort(npc_ids.begin(), npc_ids.end());

        CriminalOrganization org{};
        org.id = prov_id + 1;  // stable per province; 0 is reserved for "none"
        auto lit = leader_by_province.find(prov_id);
        org.leadership_npc_id = (lit != leader_by_province.end()) ? lit->second : npc_ids.front();
        org.member_npc_ids = npc_ids;

        auto bit = criminal_biz_by_province.find(prov_id);
        if (bit != criminal_biz_by_province.end()) {
            org.income_source_ids = bit->second;
            std::sort(org.income_source_ids.begin(), org.income_source_ids.end());
        }

        org.cash = cfg_.initial_org_cash;

        // Seed per-province dominance from the world-gen criminal baseline if
        // present, else the config default.
        float baseline = cfg_.expansion_initial_dominance;
        if (prov_id < state.provinces.size() && state.provinces[prov_id].cohort_stats) {
            float seed = state.provinces[prov_id].cohort_stats->criminal_dominance_index;
            if (seed > 0.0f)
                baseline = seed;
        }
        org.dominance_by_province[prov_id] = baseline;

        org.decision_day_offset = compute_decision_offset(org.id, cfg_.quarterly_interval);
        org.strategic_decision_tick = state.current_tick + org.decision_day_offset;
        org.conflict_state = TerritorialConflictStage::none;
        org.conflict_rival_org_id = 0;

        organizations_.push_back(std::move(org));
    }
}

void CriminalOperationsModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Bootstrap the org topology from world-gen criminal NPCs/businesses on the
    // first tick (or first tick after a load), so the criminal subsystem is
    // live in a fresh game instead of only ever loading orgs from a save.
    form_organizations(state);

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

    float cash_level = compute_cash_level(org.cash, monthly_cost, cfg_.cash_comfortable_months);

    CriminalStrategicDecision decision =
        evaluate_decision(le_heat, territory_pressure, cash_level, cfg_);

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

void CriminalOperationsModule::process_dormant_orgs(const WorldState& /*state*/) {
    for (auto& org : organizations_) {
        if (org.member_npc_ids.empty()) {
            for (auto& [prov_id, dom] : org.dominance_by_province) {
                dom -= cfg_.dormant_dominance_decay_rate;
                dom = std::max(0.0f, dom);
            }
        }
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 org_count
//   for each CriminalOrganization:
//     u32 id, u32 leadership_npc_id
//     u32 member_count, u32[member_count]
//     u32 income_count, u32[income_count]
//     f32 cash, u32 strategic_decision_tick, u8 decision_day_offset
//     u32 dominance_count, for each: u32 province_id, f32 dominance
//     u8 conflict_state, u32 conflict_rival_org_id
//   u32 expansion_count
//   for each ExpansionTeam:
//     u32 org_id, u32 target_province_id
//     u32 member_count, u32[member_count]
//     f32 investment, u32 arrival_tick
//
// std::map iterates in key order so dominance_by_province serializes
// deterministically without explicit sorting.

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
}

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool error = false;
    bool need(size_t n) {
        if (pos + n > size) {
            error = true;
            return false;
        }
        return true;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t v = data[pos] | (uint32_t(data[pos + 1]) << 8) | (uint32_t(data[pos + 2]) << 16) |
                     (uint32_t(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint8_t u8() {
        if (!need(1))
            return 0;
        return data[pos++];
    }
    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
};

}  // namespace

void CriminalOperationsModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(organizations_.size()));
    for (const auto& o : organizations_) {
        put_u32(out, o.id);
        put_u32(out, o.leadership_npc_id);
        put_u32(out, static_cast<uint32_t>(o.member_npc_ids.size()));
        for (uint32_t m : o.member_npc_ids)
            put_u32(out, m);
        put_u32(out, static_cast<uint32_t>(o.income_source_ids.size()));
        for (uint32_t is : o.income_source_ids)
            put_u32(out, is);
        put_f32(out, o.cash);
        put_u32(out, o.strategic_decision_tick);
        out.push_back(o.decision_day_offset);
        put_u32(out, static_cast<uint32_t>(o.dominance_by_province.size()));
        for (const auto& [prov, dom] : o.dominance_by_province) {
            put_u32(out, prov);
            put_f32(out, dom);
        }
        out.push_back(static_cast<uint8_t>(o.conflict_state));
        put_u32(out, o.conflict_rival_org_id);
    }
    put_u32(out, static_cast<uint32_t>(active_expansions_.size()));
    for (const auto& e : active_expansions_) {
        put_u32(out, e.org_id);
        put_u32(out, e.target_province_id);
        put_u32(out, static_cast<uint32_t>(e.member_npc_ids.size()));
        for (uint32_t m : e.member_npc_ids)
            put_u32(out, m);
        put_f32(out, e.investment);
        put_u32(out, e.arrival_tick);
    }
}

bool CriminalOperationsModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t org_count = r.u32();
    organizations_.clear();
    organizations_.reserve(org_count);
    for (uint32_t i = 0; i < org_count; ++i) {
        CriminalOrganization o{};
        o.id = r.u32();
        o.leadership_npc_id = r.u32();
        uint32_t mn = r.u32();
        if (r.error)
            return false;
        o.member_npc_ids.reserve(mn);
        for (uint32_t j = 0; j < mn; ++j)
            o.member_npc_ids.push_back(r.u32());
        uint32_t isn = r.u32();
        if (r.error)
            return false;
        o.income_source_ids.reserve(isn);
        for (uint32_t j = 0; j < isn; ++j)
            o.income_source_ids.push_back(r.u32());
        o.cash = r.f32();
        o.strategic_decision_tick = r.u32();
        o.decision_day_offset = r.u8();
        uint32_t dn = r.u32();
        if (r.error)
            return false;
        for (uint32_t j = 0; j < dn; ++j) {
            uint32_t prov = r.u32();
            float dom = r.f32();
            o.dominance_by_province[prov] = dom;
        }
        o.conflict_state = static_cast<TerritorialConflictStage>(r.u8());
        o.conflict_rival_org_id = r.u32();
        if (r.error)
            return false;
        organizations_.push_back(std::move(o));
    }
    uint32_t exp_count = r.u32();
    active_expansions_.clear();
    active_expansions_.reserve(exp_count);
    for (uint32_t i = 0; i < exp_count; ++i) {
        ExpansionTeam e{};
        e.org_id = r.u32();
        e.target_province_id = r.u32();
        uint32_t mn = r.u32();
        if (r.error)
            return false;
        e.member_npc_ids.reserve(mn);
        for (uint32_t j = 0; j < mn; ++j)
            e.member_npc_ids.push_back(r.u32());
        e.investment = r.f32();
        e.arrival_tick = r.u32();
        if (r.error)
            return false;
        active_expansions_.push_back(std::move(e));
    }
    return !r.error;
}

}  // namespace econlife
