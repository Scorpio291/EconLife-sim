#include "political_cycle_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
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

    // Process elections for offices whose election is DUE — due tick reached or
    // already passed. The `>=` (not `==`) is what makes the cycle self-healing:
    // a past-due tick catches up on the next tick instead of dead-locking.
    //
    // An exact-equality trigger deadlocked permanently whenever an office
    // carried a due tick in the past, because the only two reschedule sites live
    // inside this matched branch and activate_campaigns() also skips offices
    // whose due tick is <= current_tick. Past-due ticks are not exotic: they
    // arrive from world-gen data that is written once and never advanced
    // (Province::political.election_due_tick = 1460), from a save taken after
    // that tick, and from any mod or hand-built world that ships an office with
    // a stale tick. Every fired office is rescheduled below (both branches), so
    // the catch-up costs exactly one election and then resumes the normal term.
    for (auto& office : political_state_.offices) {
        if (office.election_due_tick > state.current_tick)
            continue;

        const Province* prov = find_province(office.province_id);
        if (!nation_holds_elections(prov)) {
            // Autocracy / FailedState: no election (valid state); reschedule.
            // Anchored on current_tick, not on the old due tick, so a stale tick
            // resolves to one term from now rather than replaying the whole
            // backlog of missed terms one tick at a time.
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

        // Always reschedule, anchored on current_tick (see the due-tick comment
        // above): a fired office must never be left at or behind the current
        // tick, or it would re-run its election every tick from here on.
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
            // O(1) through the id index rather than a linear scan of every NPC per
            // proposal per tick — the same helper ~10 other modules already use.
            const NPC* sponsor = lookup_npc_by_id(state, static_cast<uint32_t>(proposal.sponsor_id));
            const bool sponsor_active = sponsor != nullptr && sponsor->status == NPCStatus::active;
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

// ═════════════════════════════════════════════════════════════════════════════
// Module-private state persistence (persistence schema v25)
// ═════════════════════════════════════════════════════════════════════════════
// Encoding follows the convention the other opt-in modules use (see
// criminal_operations / banking): little-endian scalars, a u32 format version
// first so the blob can evolve independently of the save schema, and
// length-prefixed strings. The unordered_map members are written in sorted key
// order so the blob is byte-identical for identical state (round-trip
// determinism is the persistence module's central invariant).

namespace {

constexpr uint32_t kPoliticalStateBlobVersion = 1u;

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put_u64(std::vector<uint8_t>& out, uint64_t v) {
    put_u32(out, static_cast<uint32_t>(v & 0xFFFFFFFFull));
    put_u32(out, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFull));
}

void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
}

void put_string(std::vector<uint8_t>& out, const std::string& s) {
    put_u32(out, static_cast<uint32_t>(s.size()));
    for (char c : s)
        out.push_back(static_cast<uint8_t>(c));
}

// Approval maps are unordered_map: write them in ascending key order so equal
// state always produces equal bytes.
void put_approval_map(std::vector<uint8_t>& out,
                      const std::unordered_map<std::string, float>& approval) {
    std::vector<const std::pair<const std::string, float>*> sorted;
    sorted.reserve(approval.size());
    for (const auto& kv : approval)
        sorted.push_back(&kv);
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<const std::string, float>* a,
                 const std::pair<const std::string, float>* b) { return a->first < b->first; });
    put_u32(out, static_cast<uint32_t>(sorted.size()));
    for (const auto* kv : sorted) {
        put_string(out, kv->first);
        put_f32(out, kv->second);
    }
}

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool error = false;
    bool need(size_t n) {
        if (error || pos + n > size) {
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
    uint64_t u64() {
        uint32_t lo = u32();
        uint32_t hi = u32();
        return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
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
    std::string str() {
        uint32_t len = u32();
        // Bound the declared length against what is actually left before
        // allocating: the length is untrusted input.
        if (!need(len))
            return {};
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }
    // A count is only plausible if the remaining bytes could hold that many
    // one-byte elements. Cheap guard against a corrupt count reaching reserve().
    bool count_fits(uint32_t count) const { return static_cast<size_t>(count) <= size - pos; }
};

bool read_approval_map(Reader& r, std::unordered_map<std::string, float>& out) {
    uint32_t count = r.u32();
    if (r.error || !r.count_fits(count))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        std::string key = r.str();
        float value = r.f32();
        if (r.error)
            return false;
        out[key] = value;
    }
    return true;
}

}  // namespace

void PoliticalCycleModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, kPoliticalStateBlobVersion);
    out.push_back(formed_ ? 1u : 0u);

    put_u32(out, static_cast<uint32_t>(political_state_.offices.size()));
    for (const auto& o : political_state_.offices) {
        put_u64(out, o.id);
        put_u32(out, o.province_id);
        out.push_back(static_cast<uint8_t>(o.office_type));
        put_u64(out, o.current_holder_id);
        put_u64(out, o.election_due_tick);
        put_u32(out, o.term_length_ticks);
        put_f32(out, o.win_threshold);
        put_approval_map(out, o.approval_by_demographic);
    }

    put_u32(out, static_cast<uint32_t>(political_state_.campaigns.size()));
    for (const auto& c : political_state_.campaigns) {
        put_u64(out, c.id);
        put_u64(out, c.active_candidate_id);
        put_u64(out, c.office_id);
        put_u64(out, c.campaign_start_tick);
        put_u64(out, c.election_tick);
        put_u32(out, static_cast<uint32_t>(c.coalition_commitments.size()));
        for (const auto& cc : c.coalition_commitments) {
            put_string(out, cc.demographic);
            put_string(out, cc.promise_text);
            put_u64(out, cc.obligation_node_id);
            out.push_back(cc.delivered ? 1u : 0u);
        }
        put_u32(out, static_cast<uint32_t>(c.endorsements.size()));
        for (const auto& e : c.endorsements) {
            put_u64(out, e.endorser_npc_id);
            put_string(out, e.primary_demographic);
            put_f32(out, e.approval_bonus);
        }
        put_f32(out, c.resource_deployment);
        put_approval_map(out, c.current_approval_by_demographic);
        put_u32(out, static_cast<uint32_t>(c.event_modifiers.size()));
        for (float m : c.event_modifiers)
            put_f32(out, m);
        out.push_back(c.resolved ? 1u : 0u);
    }

    put_u32(out, static_cast<uint32_t>(political_state_.proposals.size()));
    for (const auto& p : political_state_.proposals) {
        put_u64(out, p.id);
        out.push_back(static_cast<uint8_t>(p.status));
        put_u64(out, p.sponsor_id);
        put_u64(out, p.vote_tick);
        put_f32(out, p.votes_for);
        put_f32(out, p.votes_against);
        put_string(out, p.policy_effect_id);
    }

    // The repression ratchet. Losing repression_count restarted the
    // FailedState collapse clock (8 fresh monthly crackdowns), and losing
    // repression_grievance_floor flipped suppression back to grievance-REDUCING
    // — the martyr backlash a sustained crackdown has already earned.
    put_u32(out, static_cast<uint32_t>(political_state_.nation_unrest.size()));
    for (const auto& u : political_state_.nation_unrest) {
        put_u32(out, u.nation_id);
        put_u32(out, u.repression_count);
        put_f32(out, u.repression_grievance_floor);
    }
}

bool PoliticalCycleModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != kPoliticalStateBlobVersion)
        return false;

    // Everything is parsed into locals and only moved into the members once the
    // whole blob has read cleanly: a truncated or corrupt block must leave the
    // module exactly as it was, never half-wiped.
    const bool formed = r.u8() != 0u;
    std::vector<PoliticalOffice> offices;
    std::vector<Campaign> campaigns;
    std::vector<LegislativeProposal> proposals;
    std::vector<NationUnrestState> nation_unrest;

    uint32_t office_count = r.u32();
    if (r.error || !r.count_fits(office_count))
        return false;
    offices.reserve(office_count);
    for (uint32_t i = 0; i < office_count; ++i) {
        PoliticalOffice o{};
        o.id = r.u64();
        o.province_id = r.u32();
        o.office_type = static_cast<PoliticalOfficeType>(r.u8());
        o.current_holder_id = r.u64();
        o.election_due_tick = r.u64();
        o.term_length_ticks = r.u32();
        o.win_threshold = r.f32();
        if (r.error || !read_approval_map(r, o.approval_by_demographic))
            return false;
        offices.push_back(std::move(o));
    }

    uint32_t campaign_count = r.u32();
    if (r.error || !r.count_fits(campaign_count))
        return false;
    campaigns.reserve(campaign_count);
    for (uint32_t i = 0; i < campaign_count; ++i) {
        Campaign c{};
        c.id = r.u64();
        c.active_candidate_id = r.u64();
        c.office_id = r.u64();
        c.campaign_start_tick = r.u64();
        c.election_tick = r.u64();
        uint32_t commitment_count = r.u32();
        if (r.error || !r.count_fits(commitment_count))
            return false;
        c.coalition_commitments.reserve(commitment_count);
        for (uint32_t j = 0; j < commitment_count; ++j) {
            CoalitionCommitment cc{};
            cc.demographic = r.str();
            cc.promise_text = r.str();
            cc.obligation_node_id = r.u64();
            cc.delivered = r.u8() != 0u;
            if (r.error)
                return false;
            c.coalition_commitments.push_back(std::move(cc));
        }
        uint32_t endorsement_count = r.u32();
        if (r.error || !r.count_fits(endorsement_count))
            return false;
        c.endorsements.reserve(endorsement_count);
        for (uint32_t j = 0; j < endorsement_count; ++j) {
            Endorsement e{};
            e.endorser_npc_id = r.u64();
            e.primary_demographic = r.str();
            e.approval_bonus = r.f32();
            if (r.error)
                return false;
            c.endorsements.push_back(std::move(e));
        }
        c.resource_deployment = r.f32();
        if (r.error || !read_approval_map(r, c.current_approval_by_demographic))
            return false;
        uint32_t modifier_count = r.u32();
        if (r.error || !r.count_fits(modifier_count))
            return false;
        c.event_modifiers.reserve(modifier_count);
        for (uint32_t j = 0; j < modifier_count; ++j)
            c.event_modifiers.push_back(r.f32());
        c.resolved = r.u8() != 0u;
        if (r.error)
            return false;
        campaigns.push_back(std::move(c));
    }

    uint32_t proposal_count = r.u32();
    if (r.error || !r.count_fits(proposal_count))
        return false;
    proposals.reserve(proposal_count);
    for (uint32_t i = 0; i < proposal_count; ++i) {
        LegislativeProposal p{};
        p.id = r.u64();
        p.status = static_cast<LegislativeProposalStatus>(r.u8());
        p.sponsor_id = r.u64();
        p.vote_tick = r.u64();
        p.votes_for = r.f32();
        p.votes_against = r.f32();
        p.policy_effect_id = r.str();
        if (r.error)
            return false;
        proposals.push_back(std::move(p));
    }

    uint32_t unrest_count = r.u32();
    if (r.error || !r.count_fits(unrest_count))
        return false;
    nation_unrest.reserve(unrest_count);
    for (uint32_t i = 0; i < unrest_count; ++i) {
        NationUnrestState u{};
        u.nation_id = r.u32();
        u.repression_count = r.u32();
        u.repression_grievance_floor = r.f32();
        if (r.error)
            return false;
        nation_unrest.push_back(u);
    }

    if (r.error)
        return false;

    political_state_.offices = std::move(offices);
    political_state_.campaigns = std::move(campaigns);
    political_state_.proposals = std::move(proposals);
    political_state_.nation_unrest = std::move(nation_unrest);
    formed_ = formed;
    return true;
}

}  // namespace econlife
