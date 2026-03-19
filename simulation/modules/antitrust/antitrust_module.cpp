#include "modules/antitrust/antitrust_module.h"

#include "core/world_state/world_state.h"
#include "core/world_state/player.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace econlife {

// ---------------------------------------------------------------------------
// Static utility functions
// ---------------------------------------------------------------------------

float AntitrustModule::compute_supply_share(float actor_output,
                                              float total_supply)
{
    if (total_supply <= 0.0f) return 0.0f;
    float share = actor_output / total_supply;
    if (std::isnan(share)) return 0.0f;
    return std::clamp(share, 0.0f, 1.0f);
}

bool AntitrustModule::is_tier1_triggered(float supply_share) {
    return supply_share >= Constants::market_share_threshold;
}

bool AntitrustModule::is_tier2_triggered(float supply_share) {
    return supply_share >= Constants::dominant_price_mover_threshold;
}

float AntitrustModule::compute_meter_fill_increment() {
    return Constants::meter_fill_per_threshold_tick;
}

float AntitrustModule::compute_pressure_increment() {
    return Constants::dominance_proposal_pressure_per_tick;
}

float AntitrustModule::compute_pressure_decay() {
    return Constants::proposal_pressure_decay_rate;
}

bool AntitrustModule::should_generate_proposal(float pressure) {
    return pressure >= Constants::proposal_threshold;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void AntitrustModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Monthly check: only fire when current_tick matches next_check_tick
    if (state.current_tick >= next_check_tick_) {
        run_monthly_check(state, delta);
        next_check_tick_ = state.current_tick + Constants::monthly_interval;
    }
}

void AntitrustModule::run_monthly_check(const WorldState& state,
                                          DeltaBuffer& delta)
{
    // Collect unique (good_id, province_id) pairs from regional_markets
    // sorted for deterministic processing
    struct MarketKey {
        uint32_t good_id;
        uint32_t province_id;
        bool operator<(const MarketKey& o) const {
            if (good_id != o.good_id) return good_id < o.good_id;
            return province_id < o.province_id;
        }
    };

    std::set<MarketKey> market_keys;
    for (const auto& rm : state.regional_markets) {
        market_keys.insert({rm.good_id, rm.province_id});
    }

    // Track which provinces have Tier 2 violations this check
    std::set<uint32_t> provinces_with_tier2;

    for (const auto& mk : market_keys) {
        // Find total supply for this good in this province
        float total_supply = 0.0f;
        for (const auto& rm : state.regional_markets) {
            if (rm.good_id == mk.good_id && rm.province_id == mk.province_id) {
                total_supply = rm.supply;
                break;
            }
        }

        // Skip goods with zero supply (avoid division by zero)
        if (total_supply <= 0.0f) continue;

        // Aggregate output by actor (owner_id) for formal market only
        // key: actor_id (owner or business id if no owner)
        std::map<uint32_t, float> actor_output;

        for (const auto& biz : state.npc_businesses) {
            // Exclude criminal_sector businesses
            if (biz.criminal_sector) continue;
            if (biz.province_id != mk.province_id) continue;

            // V1 simplified: use revenue_per_tick as proxy for output contribution
            // to the good. In full implementation, per-good output would be tracked.
            // Use owner_id as actor; if 0, use biz.id
            uint32_t actor_id = (biz.owner_id != 0) ? biz.owner_id : biz.id;

            // Only count businesses whose sector could produce this good
            // V1 simplified: count all formal businesses as potential producers
            actor_output[actor_id] += biz.revenue_per_tick;
        }

        // Also count player direct output
        if (state.player) {
            for (const auto& biz : state.npc_businesses) {
                if (biz.owner_id == state.player->id &&
                    !biz.criminal_sector &&
                    biz.province_id == mk.province_id)
                {
                    // Already counted above via owner_id match
                }
            }
        }

        // Compute total actor output for normalization
        float total_actor_output = 0.0f;
        for (const auto& [aid, output] : actor_output) {
            total_actor_output += output;
        }
        if (total_actor_output <= 0.0f) continue;

        // Check each actor's share (sorted by actor_id for determinism)
        std::vector<std::pair<uint32_t, float>> sorted_actors(
            actor_output.begin(), actor_output.end());
        std::sort(sorted_actors.begin(), sorted_actors.end());

        for (const auto& [actor_id, output] : sorted_actors) {
            float share = compute_supply_share(output, total_actor_output);

            if (is_tier1_triggered(share)) {
                // Find regulator NPC in this province
                for (const auto& npc : state.significant_npcs) {
                    if (npc.role == NPCRole::regulator &&
                        npc.current_province_id == mk.province_id &&
                        npc.status == NPCStatus::active)
                    {
                        NPCDelta npc_delta;
                        npc_delta.npc_id = npc.id;
                        npc_delta.motivation_delta = compute_meter_fill_increment();
                        delta.npc_deltas.push_back(npc_delta);
                        break;  // one regulator per province
                    }
                }

                // Generate evidence if share is significantly above threshold
                if (share > Constants::market_share_threshold + 0.10f) {
                    EvidenceDelta ev_delta;
                    EvidenceToken token;
                    token.id = state.current_tick * 1000 + actor_id;
                    token.type = EvidenceType::documentary;
                    token.source_npc_id = 0;  // public market records
                    token.target_npc_id = actor_id;
                    token.actionability = share - Constants::market_share_threshold;
                    token.decay_rate = 0.001f;
                    token.created_tick = state.current_tick;
                    token.province_id = mk.province_id;
                    token.is_active = true;
                    ev_delta.new_token = token;
                    delta.evidence_deltas.push_back(ev_delta);
                }
            }

            if (is_tier2_triggered(share)) {
                provinces_with_tier2.insert(mk.province_id);

                // Increment proposal pressure
                proposal_pressure_[mk.province_id] += compute_pressure_increment();
            }
        }
    }

    // Decay proposal pressure for provinces without Tier 2 violations
    for (auto& [prov_id, pressure] : proposal_pressure_) {
        if (provinces_with_tier2.find(prov_id) == provinces_with_tier2.end()) {
            pressure -= compute_pressure_decay();
            pressure = std::max(0.0f, pressure);
        }
    }

    // Check for legislative proposal auto-generation
    for (auto& [prov_id, pressure] : proposal_pressure_) {
        if (should_generate_proposal(pressure)) {
            // Find a legislator NPC in this province
            uint32_t proposer = 0;
            for (const auto& npc : state.significant_npcs) {
                if (npc.role == NPCRole::politician &&
                    npc.current_province_id == prov_id &&
                    npc.status == NPCStatus::active)
                {
                    proposer = npc.id;
                    break;
                }
            }

            LegislativeProposal proposal;
            proposal.id = next_proposal_id_++;
            proposal.province_id = prov_id;
            proposal.proposer_npc_id = proposer;
            proposal.created_tick = state.current_tick;
            proposal.target_market_share_cap = Constants::market_share_threshold;
            proposals_.push_back(proposal);

            // Reset pressure after proposal generation
            pressure = 0.0f;
        }
    }
}

}  // namespace econlife
