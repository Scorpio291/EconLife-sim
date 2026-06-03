#include "political_cycle_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

PoliticalCycleModule::PoliticalCycleModule(const PoliticalCycleConfig& cfg) : cfg_(cfg) {}

float PoliticalCycleModule::compute_raw_vote_share(
    const std::unordered_map<std::string, float>& approval_by_demographic,
    const std::vector<DemographicWeight>& demographics) {
    float weighted_sum = 0.0f;
    float weight_total = 0.0f;

    // Process demographics in sorted order for determinism
    std::vector<const DemographicWeight*> sorted;
    sorted.reserve(demographics.size());
    for (const auto& d : demographics) {
        sorted.push_back(&d);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const DemographicWeight* a, const DemographicWeight* b) {
                  return a->demographic < b->demographic;
              });

    for (const auto* d : sorted) {
        float approval = 0.5f;  // default
        auto it = approval_by_demographic.find(d->demographic);
        if (it != approval_by_demographic.end()) {
            approval = it->second;
        }
        float w = d->population_fraction * d->turnout_weight;
        weighted_sum += approval * w;
        weight_total += w;
    }

    if (weight_total <= 0.0f) {
        return 0.5f;  // coin flip fallback per spec
    }

    return weighted_sum / weight_total;
}

float PoliticalCycleModule::compute_resource_modifier(float resource_deployment,
                                                      float resource_scale,
                                                      float resource_max_effect) {
    float raw = std::tanh(resource_deployment * resource_scale) * resource_max_effect;
    return std::clamp(raw, -resource_max_effect, resource_max_effect);
}

float PoliticalCycleModule::compute_event_modifier_total(const std::vector<float>& event_modifiers,
                                                         float event_modifier_cap) {
    float total = 0.0f;
    for (float m : event_modifiers) {
        total += m;
    }
    return std::clamp(total, -event_modifier_cap, event_modifier_cap);
}

float PoliticalCycleModule::compute_final_vote_share(float raw_share, float resource_modifier,
                                                     float event_total) {
    return std::clamp(raw_share + resource_modifier + event_total, 0.0f, 1.0f);
}

float PoliticalCycleModule::compute_legislator_support(float motivation_alignment,
                                                       float obligation_bonus,
                                                       float constituency_pressure) {
    return motivation_alignment + obligation_bonus + constituency_pressure;
}

bool PoliticalCycleModule::compute_vote_passed(float votes_for, float votes_against,
                                               float majority_threshold) {
    float total = votes_for + votes_against;
    if (total <= 0.0f)
        return false;
    return (votes_for / total) > majority_threshold;
}

namespace {

const char* group_name(DemographicGroup g) {
    switch (g) {
        case DemographicGroup::youth_urban:
            return "youth_urban";
        case DemographicGroup::youth_rural:
            return "youth_rural";
        case DemographicGroup::working_urban_low:
            return "working_urban_low";
        case DemographicGroup::working_urban_mid:
            return "working_urban_mid";
        case DemographicGroup::working_urban_high:
            return "working_urban_high";
        case DemographicGroup::working_rural_low:
            return "working_rural_low";
        case DemographicGroup::working_rural_mid:
            return "working_rural_mid";
        case DemographicGroup::working_rural_high:
            return "working_rural_high";
        case DemographicGroup::retiree_urban:
            return "retiree_urban";
        case DemographicGroup::retiree_rural:
            return "retiree_rural";
        case DemographicGroup::student:
            return "student";
        case DemographicGroup::unemployed:
            return "unemployed";
    }
    return "unknown";
}

}  // namespace

void PoliticalCycleModule::form_offices(const WorldState& state) {
    if (formed_)
        return;
    formed_ = true;
    if (!political_state_.offices.empty())
        return;  // test- or save-seeded; do not duplicate

    for (const auto& prov : state.provinces) {
        PoliticalOffice office{};
        office.id = static_cast<uint64_t>(prov.id) + 1;  // 0 reserved
        office.province_id = prov.id;
        office.office_type = PoliticalOfficeType::governor;
        office.term_length_ticks = 1460;
        office.win_threshold = 0.5f;
        office.election_due_tick = prov.political.election_due_tick != 0
                                       ? prov.political.election_due_tick
                                       : office.term_length_ticks;

        // Holder: lowest-id active politician in the province, else community_leader.
        uint32_t politician = 0;
        uint32_t leader = 0;
        for (const auto& npc : state.significant_npcs) {
            if (npc.status != NPCStatus::active || npc.current_province_id != prov.id)
                continue;
            if (npc.role == NPCRole::politician && (politician == 0 || npc.id < politician))
                politician = npc.id;
            else if (npc.role == NPCRole::community_leader && (leader == 0 || npc.id < leader))
                leader = npc.id;
        }
        office.current_holder_id = politician != 0 ? politician : leader;

        // Baseline approval per demographic from cohort political_lean (-1..1).
        if (prov.cohort_stats) {
            for (const auto& [g, c] : prov.cohort_stats->cohorts) {
                office.approval_by_demographic[group_name(g)] =
                    std::clamp(0.5f + 0.5f * c.political_lean, 0.0f, 1.0f);
            }
        }
        political_state_.offices.push_back(std::move(office));
    }
}

void PoliticalCycleModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Seed the office topology from world-gen data on the first tick so the
    // election pipeline is live in a fresh game.
    form_offices(state);

    auto find_province = [&](uint32_t pid) -> const Province* {
        for (const auto& p : state.provinces)
            if (p.id == pid)
                return &p;
        return nullptr;
    };
    auto nation_holds_elections = [&](const Province* p) -> bool {
        if (!p)
            return true;
        for (const auto& n : state.nations)
            if (n.id == p->nation_id)
                return n.government_type == GovernmentType::Democracy ||
                       n.government_type == GovernmentType::Federation;
        return true;
    };

    // Process elections for offices due this tick.
    for (auto& office : political_state_.offices) {
        if (office.election_due_tick != state.current_tick)
            continue;

        const Province* prov = find_province(office.province_id);
        if (!nation_holds_elections(prov)) {
            // Autocracy / FailedState: no election (valid state); reschedule.
            office.election_due_tick = state.current_tick + office.term_length_ticks;
            continue;
        }

        // Coalition-weighted demographic turnout from the province cohorts.
        std::vector<DemographicWeight> weights;
        if (prov && prov->cohort_stats) {
            uint64_t total = 0;
            for (const auto& [g, c] : prov->cohort_stats->cohorts) {
                (void)g;
                total += c.size;
            }
            for (const auto& [g, c] : prov->cohort_stats->cohorts) {
                DemographicWeight w;
                w.demographic = group_name(g);
                w.population_fraction =
                    total > 0 ? static_cast<float>(c.size) / static_cast<float>(total) : 0.0f;
                w.turnout_weight = 1.0f;
                weights.push_back(w);
            }
        }

        // An active campaign for this office overrides the baseline approval and
        // contributes resource/event modifiers (campaign system: later slice).
        const std::unordered_map<std::string, float>* approval = &office.approval_by_demographic;
        float resource_mod = 0.0f;
        float event_total = 0.0f;
        for (auto& camp : political_state_.campaigns) {
            if (camp.office_id != office.id || camp.resolved)
                continue;
            if (!camp.current_approval_by_demographic.empty())
                approval = &camp.current_approval_by_demographic;
            resource_mod = compute_resource_modifier(camp.resource_deployment, cfg_.resource_scale,
                                                     cfg_.resource_max_effect);
            event_total =
                compute_event_modifier_total(camp.event_modifiers, cfg_.event_modifier_cap);
            camp.resolved = true;
            break;
        }

        float raw = compute_raw_vote_share(*approval, weights);
        float final_share = compute_final_vote_share(raw, resource_mod, event_total);
        bool won = final_share >= static_cast<float>(office.win_threshold);
        auto incumbent = static_cast<uint32_t>(office.current_holder_id);

        if (won) {
            if (incumbent != 0) {
                NPCDelta nd;
                nd.npc_id = incumbent;
                nd.new_memory_entry = MemoryEntry{state.current_tick,
                                                  MemoryType::event,
                                                  static_cast<uint32_t>(office.id),
                                                  0.8f,
                                                  0.01f,
                                                  true};
                nd.motivation_delta = 0.05f;
                delta.npc_deltas.push_back(nd);
            }
        } else {
            // Incumbent lost: install the lowest-id active politician challenger
            // in the province (other than the incumbent), else retain incumbent.
            uint32_t challenger = 0;
            if (prov) {
                for (const auto& npc : state.significant_npcs) {
                    if (npc.status != NPCStatus::active || npc.current_province_id != prov->id)
                        continue;
                    if (npc.role != NPCRole::politician || npc.id == incumbent)
                        continue;
                    if (challenger == 0 || npc.id < challenger)
                        challenger = npc.id;
                }
            }
            if (incumbent != 0) {
                NPCDelta nd;
                nd.npc_id = incumbent;
                nd.new_memory_entry = MemoryEntry{state.current_tick,
                                                  MemoryType::event,
                                                  static_cast<uint32_t>(office.id),
                                                  -0.6f,
                                                  0.01f,
                                                  true};
                delta.npc_deltas.push_back(nd);
            }
            if (challenger != 0) {
                office.current_holder_id = challenger;
                NPCDelta nd;
                nd.npc_id = challenger;
                nd.new_memory_entry = MemoryEntry{state.current_tick,
                                                  MemoryType::event,
                                                  static_cast<uint32_t>(office.id),
                                                  0.8f,
                                                  0.01f,
                                                  true};
                nd.motivation_delta = 0.05f;
                delta.npc_deltas.push_back(nd);
            }
        }

        RegionDelta rd;
        rd.region_id = office.province_id;
        rd.institutional_trust_delta = won ? 0.02f : -0.01f;
        delta.region_deltas.push_back(rd);

        office.election_due_tick = state.current_tick + office.term_length_ticks;
    }

    // Process legislative proposals due this tick
    for (auto& proposal : political_state_.proposals) {
        if (proposal.status != LegislativeProposalStatus::floor_debate)
            continue;
        if (proposal.vote_tick != state.current_tick)
            continue;

        bool passed = compute_vote_passed(proposal.votes_for, proposal.votes_against,
                                          cfg_.majority_threshold);
        proposal.status =
            passed ? LegislativeProposalStatus::enacted : LegislativeProposalStatus::failed;
    }
}

}  // namespace econlife
