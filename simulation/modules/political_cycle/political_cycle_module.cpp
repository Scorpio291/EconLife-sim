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

void PoliticalCycleModule::apply_endorsement_bonuses(
    std::unordered_map<std::string, float>& approval,
    const std::vector<Endorsement>& endorsements) {
    for (const auto& e : endorsements) {
        float& a = approval[e.primary_demographic];
        a = std::clamp(a + e.approval_bonus, 0.0f, 1.0f);
    }
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

// Poll active politician NPCs on a proposal and tally votes_for/votes_against.
// Documented V1 proxies (no legislature membership / per-proposal policy
// alignment vector exists): motivation_alignment = the legislator's ideological
// motivation weight; obligation_bonus = +0.15 if the legislator owes an active
// obligation to the sponsor; constituency_pressure = 0. Undecided legislators
// (between OPPOSE and SUPPORT thresholds) abstain.
void poll_legislators(const WorldState& state, LegislativeProposal& proposal,
                      const PoliticalCycleConfig& cfg) {
    float votes_for = 0.0f;
    float votes_against = 0.0f;
    for (const auto& npc : state.significant_npcs) {
        if (npc.status != NPCStatus::active || npc.role != NPCRole::politician)
            continue;
        float alignment = npc.motivations.weights[static_cast<size_t>(OutcomeType::ideological)];
        float obligation_bonus = 0.0f;
        for (const auto& o : state.obligation_network) {
            if (o.is_active && o.debtor_npc_id == npc.id &&
                o.creditor_npc_id == proposal.sponsor_id) {
                obligation_bonus = 0.15f;
                break;
            }
        }
        float support =
            PoliticalCycleModule::compute_legislator_support(alignment, obligation_bonus, 0.0f);
        if (support >= cfg.support_threshold)
            votes_for += 1.0f;
        else if (support <= cfg.oppose_threshold)
            votes_against += 1.0f;
    }
    proposal.votes_for = votes_for;
    proposal.votes_against = votes_against;
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

void PoliticalCycleModule::activate_campaigns(const WorldState& state) {
    for (const auto& office : political_state_.offices) {
        if (office.election_due_tick <= state.current_tick)
            continue;
        if (office.election_due_tick - state.current_tick > cfg_.campaign_lead_time_ticks)
            continue;
        bool active = false;
        for (const auto& c : political_state_.campaigns)
            if (c.office_id == office.id && !c.resolved) {
                active = true;
                break;
            }
        if (active)
            continue;

        Campaign camp{};
        camp.id = office.id;  // one active campaign per office at a time
        camp.active_candidate_id = office.current_holder_id;
        camp.office_id = office.id;
        camp.campaign_start_tick = state.current_tick;
        camp.election_tick = office.election_due_tick;
        camp.current_approval_by_demographic = office.approval_by_demographic;
        apply_endorsement_bonuses(camp.current_approval_by_demographic, camp.endorsements);
        political_state_.campaigns.push_back(std::move(camp));
    }
}

float PoliticalCycleModule::compute_legitimacy_target(float institutional_trust, float stability,
                                                      float grievance, float unemployment,
                                                      const PoliticalCycleConfig& cfg) {
    float raw = cfg.legitimacy_trust_weight * institutional_trust +
                cfg.legitimacy_stability_weight * stability -
                cfg.legitimacy_grievance_weight * grievance -
                cfg.legitimacy_unemployment_weight * unemployment;
    if (std::isnan(raw))
        return 0.5f;
    return std::clamp(raw, 0.0f, 1.0f);
}

float PoliticalCycleModule::compute_suppression_net_grievance(float repression_grievance_floor,
                                                              float suppression_immediate) {
    return repression_grievance_floor - suppression_immediate;
}

NationUnrestState& PoliticalCycleModule::unrest_state_for(uint32_t nation_id) {
    for (auto& u : political_state_.nation_unrest)
        if (u.nation_id == nation_id)
            return u;
    NationUnrestState u{};
    u.nation_id = nation_id;
    political_state_.nation_unrest.push_back(u);
    return political_state_.nation_unrest.back();
}

void PoliticalCycleModule::process_national_unrest(const WorldState& state, DeltaBuffer& delta) {
    // Responses (crackdowns, concessions, fragmentation) are discrete monthly
    // events, not per-tick. Legitimacy itself updates every tick.
    const bool monthly = state.current_tick > 0u && (state.current_tick % 30u == 0u);

    for (const auto& nation : state.nations) {
        // Collect this nation's provinces in world.provinces order (deterministic).
        std::vector<const Province*> provs;
        for (const auto& prov : state.provinces)
            if (prov.nation_id == nation.id)
                provs.push_back(&prov);
        if (provs.empty())
            continue;

        // --- Population-weighted legitimacy roll-up, EMA-smoothed ---
        double weight_sum = 0.0, weighted_target = 0.0;
        for (const auto* p : provs) {
            float unemployment = p->cohort_stats ? p->cohort_stats->unemployment_rate : 0.0f;
            double pop =
                p->cohort_stats ? static_cast<double>(p->cohort_stats->total_population) : 0.0;
            double w = pop > 0.0 ? pop : 1.0;
            float tgt = compute_legitimacy_target(p->community.institutional_trust,
                                                  p->conditions.stability_score,
                                                  p->community.grievance_level, unemployment, cfg_);
            weighted_target += w * static_cast<double>(tgt);
            weight_sum += w;
        }
        float target = static_cast<float>(weighted_target / weight_sum);
        float current = nation.political_cycle.national_legitimacy;
        float updated =
            std::clamp(current + cfg_.legitimacy_ema_alpha * (target - current), 0.0f, 1.0f);

        NationDelta nd;
        nd.nation_id = nation.id;
        nd.legitimacy_update = updated;

        // --- Regime-differentiated response, monthly, when in legitimacy crisis ---
        if (monthly && updated < cfg_.legitimacy_crisis_threshold) {
            switch (nation.government_type) {
                case GovernmentType::Democracy:
                case GovernmentType::Federation: {
                    // Accountability: incumbent approval craters (-> turnover at
                    // the next election) and the government concedes — grievance
                    // relief + trust restoration in the worst-off provinces.
                    nd.approval_delta = -cfg_.crisis_approval_hit;
                    std::vector<const Province*> by_grievance = provs;
                    std::sort(by_grievance.begin(), by_grievance.end(),
                              [](const Province* a, const Province* b) {
                                  if (a->community.grievance_level != b->community.grievance_level)
                                      return a->community.grievance_level >
                                             b->community.grievance_level;
                                  return a->id < b->id;  // deterministic tiebreak
                              });
                    uint32_t n = std::min<uint32_t>(cfg_.concession_province_count,
                                                    static_cast<uint32_t>(by_grievance.size()));
                    for (uint32_t i = 0; i < n; ++i) {
                        RegionDelta rd;
                        rd.region_id = by_grievance[i]->id;
                        rd.grievance_delta = -cfg_.concession_grievance_relief;
                        rd.institutional_trust_delta = cfg_.concession_trust_restore;
                        delta.region_deltas.push_back(rd);
                    }
                    break;
                }
                case GovernmentType::Autocracy: {
                    // Suppression: short-term dispersal, but a rising martyr floor
                    // and a legitimacy bleed; sustained crackdowns while fully
                    // illegitimate collapse the regime to a failed state.
                    NationUnrestState& u = unrest_state_for(nation.id);
                    u.repression_count += 1;
                    u.repression_grievance_floor += cfg_.suppression_grievance_floor_rise;
                    float net = compute_suppression_net_grievance(
                        u.repression_grievance_floor, cfg_.suppression_grievance_immediate);
                    for (const auto* p : provs) {
                        RegionDelta rd;
                        rd.region_id = p->id;
                        rd.grievance_delta = net;
                        delta.region_deltas.push_back(rd);
                    }
                    nd.legitimacy_update =
                        std::clamp(updated - cfg_.suppression_legitimacy_hit, 0.0f, 1.0f);
                    if (updated < cfg_.collapse_legitimacy_floor &&
                        u.repression_count >= cfg_.collapse_repression_count) {
                        nd.government_type_update =
                            static_cast<uint8_t>(GovernmentType::FailedState);
                    }
                    break;
                }
                case GovernmentType::FailedState: {
                    // Fragmentation: no valve; the criminal economy fills the void.
                    for (const auto* p : provs) {
                        RegionDelta rd;
                        rd.region_id = p->id;
                        rd.criminal_dominance_delta = cfg_.failed_state_dominance_rise;
                        delta.region_deltas.push_back(rd);
                    }
                    break;
                }
            }
        }
        delta.nation_deltas.push_back(nd);
    }
}

void PoliticalCycleModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Seed the office topology from world-gen data on the first tick so the
    // election pipeline is live in a fresh game.
    form_offices(state);
    // Open campaigns for upcoming elections (within the lead-time window).
    activate_campaigns(state);

    // National legitimacy roll-up + regime-differentiated unrest response.
    process_national_unrest(state, delta);

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
            apply_endorsement_bonuses(camp.current_approval_by_demographic, camp.endorsements);
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

    // Process legislative proposals: stage progression (drafted -> in_committee
    // -> floor_debate -> voted) and resolution at vote_tick.
    for (auto& proposal : political_state_.proposals) {
        if (proposal.status == LegislativeProposalStatus::enacted ||
            proposal.status == LegislativeProposalStatus::failed)
            continue;

        // Sponsor deceased/fled -> proposal fails (valid state, per INTERFACE).
        if (proposal.sponsor_id != 0) {
            bool sponsor_active = false;
            for (const auto& npc : state.significant_npcs)
                if (npc.id == proposal.sponsor_id) {
                    sponsor_active = (npc.status == NPCStatus::active);
                    break;
                }
            if (!sponsor_active) {
                proposal.status = LegislativeProposalStatus::failed;
                continue;
            }
        }

        switch (proposal.status) {
            case LegislativeProposalStatus::drafted:
                proposal.status = LegislativeProposalStatus::in_committee;
                break;
            case LegislativeProposalStatus::in_committee:
                proposal.status = LegislativeProposalStatus::floor_debate;
                break;
            case LegislativeProposalStatus::floor_debate: {
                if (state.current_tick < proposal.vote_tick)
                    break;  // awaiting the scheduled floor vote
                // If no tally was supplied by a producer, poll NPC legislators.
                if (proposal.votes_for + proposal.votes_against <= 0.0f)
                    poll_legislators(state, proposal, cfg_);
                bool passed = compute_vote_passed(proposal.votes_for, proposal.votes_against,
                                                  cfg_.majority_threshold);
                proposal.status =
                    passed ? LegislativeProposalStatus::enacted : LegislativeProposalStatus::failed;
                if (passed) {
                    // Enacted policy effect: queued as a consequence (placeholder
                    // id until the generic consequence system lands -- audit #4).
                    ConsequenceDelta cd;
                    cd.new_consequence = make_consequence(
                        static_cast<uint32_t>(proposal.id),
                        ConsequenceCategory::political_consequence, 0, 0, 0, state.current_tick);
                    delta.consequence_deltas.push_back(cd);
                }
                break;
            }
            default:
                break;
        }
    }
}

}  // namespace econlife
