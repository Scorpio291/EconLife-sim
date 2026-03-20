#pragma once

// political_cycle module types.
// Module-specific types for the political_cycle module (Tier 10).
// GovernmentType, NationPoliticalCycleState are in geography.h.

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace econlife {

enum class PoliticalOfficeType : uint8_t {
    city_council,
    mayor,
    provincial_legislature,
    governor,
    national_legislature,
    head_of_state
};

enum class LegislativeProposalStatus : uint8_t {
    drafted,
    in_committee,
    floor_debate,
    voted,
    enacted,
    failed
};

struct CoalitionCommitment {
    std::string demographic;
    std::string promise_text;
    uint64_t obligation_node_id = 0;
    bool delivered = false;
};

struct Endorsement {
    uint64_t endorser_npc_id = 0;
    std::string primary_demographic;
    float approval_bonus = 0.0f;
};

struct Campaign {
    uint64_t id = 0;
    uint64_t active_candidate_id = 0;
    uint64_t office_id = 0;
    uint64_t campaign_start_tick = 0;
    uint64_t election_tick = 0;
    std::vector<CoalitionCommitment> coalition_commitments;
    std::vector<Endorsement> endorsements;
    float resource_deployment = 0.0f;
    std::unordered_map<std::string, float> current_approval_by_demographic;
    std::vector<float> event_modifiers;
    bool resolved = false;
};

struct PoliticalOffice {
    uint64_t id = 0;
    PoliticalOfficeType office_type = PoliticalOfficeType::city_council;
    uint64_t current_holder_id = 0;
    uint64_t election_due_tick = 0;
    uint32_t term_length_ticks = 1460;  // ~4 years
    std::unordered_map<std::string, float> approval_by_demographic;
    float win_threshold = 0.5f;
};

struct LegislativeProposal {
    uint64_t id = 0;
    LegislativeProposalStatus status = LegislativeProposalStatus::drafted;
    uint64_t sponsor_id = 0;
    uint64_t vote_tick = 0;
    float votes_for = 0.0f;
    float votes_against = 0.0f;
    std::string policy_effect_id;
};

struct DemographicWeight {
    std::string demographic;
    float population_fraction = 0.0f;
    float turnout_weight = 1.0f;
};

// Module-internal state for political cycle
struct PoliticalCycleModuleState {
    std::vector<PoliticalOffice> offices;
    std::vector<Campaign> campaigns;
    std::vector<LegislativeProposal> proposals;
};

}  // namespace econlife
