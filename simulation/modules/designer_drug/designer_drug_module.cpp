#include "designer_drug_module.h"
#include "core/world_state/world_state.h"
#include "core/world_state/player.h"
#include <algorithm>
#include <cmath>

namespace econlife {

bool DesignerDrugModule::is_detection_triggered(float cumulative_evidence, float threshold) {
    return cumulative_evidence >= threshold;
}

uint32_t DesignerDrugModule::compute_review_duration(uint32_t base_duration, float political_delay) {
    return static_cast<uint32_t>(static_cast<float>(base_duration) * political_delay);
}

float DesignerDrugModule::compute_market_margin(SchedulingStage stage, bool has_successor) {
    switch (stage) {
        case SchedulingStage::unscheduled:
        case SchedulingStage::review_initiated:
            return UNSCHEDULED_MARGIN;
        case SchedulingStage::scheduled:
            return has_successor ? SCHEDULED_MARGIN : NO_SUCCESSOR_MARGIN;
        default:
            return SCHEDULED_MARGIN;
    }
}

bool DesignerDrugModule::should_check_detection(uint32_t current_tick, uint32_t monthly_interval) {
    return current_tick > 0 && (current_tick % monthly_interval == 0);
}

float DesignerDrugModule::accumulate_evidence_weight(float current, float new_weight) {
    return current + std::max(0.0f, new_weight);
}

void DesignerDrugModule::execute(const WorldState& state, DeltaBuffer& delta) {
    std::sort(compounds_.begin(), compounds_.end(),
              [](const DesignerDrugCompound& a, const DesignerDrugCompound& b) {
                  return a.compound_id < b.compound_id;
              });

    bool monthly_check = should_check_detection(state.current_tick, MONTHLY_INTERVAL);

    for (auto& compound : compounds_) {
        if (compound.stage == SchedulingStage::scheduled) continue;

        // BusinessDelta: R&D investment cost each tick for active (unscheduled) compounds
        // Find the criminal business owned by creator_actor_id in the compound's province
        for (const auto& biz : state.npc_businesses) {
            if (biz.owner_id == compound.creator_actor_id &&
                biz.province_id == compound.province_id &&
                biz.criminal_sector) {
                // R&D investment rate: 1% of revenue per tick
                constexpr float RD_INVESTMENT_RATE = 0.01f;
                float rd_cost = biz.revenue_per_tick * RD_INVESTMENT_RATE;
                if (rd_cost > 0.0f) {
                    BusinessDelta rd_delta;
                    rd_delta.business_id = biz.id;
                    rd_delta.cash_delta  = -rd_cost;
                    delta.business_deltas.push_back(rd_delta);
                }
                break;
            }
        }

        if (monthly_check && compound.stage == SchedulingStage::unscheduled) {
            float evidence_sum = 0.0f;
            for (const auto& token : state.evidence_pool) {
                if (token.is_active && token.target_npc_id == compound.creator_actor_id &&
                    (token.type == EvidenceType::financial || token.type == EvidenceType::physical)) {
                    evidence_sum += token.actionability;
                }
            }
            compound.cumulative_evidence_weight = evidence_sum;

            if (is_detection_triggered(compound.cumulative_evidence_weight, compound.detection_threshold)) {
                compound.stage = SchedulingStage::review_initiated;
                compound.review_start_tick = state.current_tick;
            }
        }

        if (compound.stage == SchedulingStage::review_initiated) {
            uint32_t elapsed = state.current_tick - compound.review_start_tick;
            if (elapsed >= compound.review_duration) {
                compound.stage = SchedulingStage::scheduled;

                compound.has_successor = false;
                for (const auto& other : compounds_) {
                    if (other.compound_id != compound.compound_id &&
                        other.creator_actor_id == compound.creator_actor_id &&
                        other.stage == SchedulingStage::unscheduled) {
                        compound.has_successor = true;
                        break;
                    }
                }

                compound.market_margin_multiplier = compute_market_margin(
                    compound.stage, compound.has_successor);

                ConsequenceDelta cons;
                cons.new_entry_id = compound.compound_id;
                delta.consequence_deltas.push_back(cons);
            }
        }

        // MarketDelta: compound enters/remains in informal market with initial supply
        // Unscheduled compounds supply the formal market; scheduled ones the informal market.
        // Emit a supply delta each tick to represent ongoing production availability.
        if (compound.stage == SchedulingStage::unscheduled ||
            compound.stage == SchedulingStage::review_initiated) {
            // Use compound_id as the goods key proxy (maps to "designer_drug_{id}" in goods.csv)
            constexpr float BASE_SUPPLY_PER_TICK = 10.0f;  // units per tick at baseline
            MarketDelta supply_entry;
            supply_entry.good_id      = compound.compound_id;
            supply_entry.region_id    = compound.province_id;
            supply_entry.supply_delta = BASE_SUPPLY_PER_TICK * compound.market_margin_multiplier;
            delta.market_deltas.push_back(supply_entry);
        }

        compound.market_margin_multiplier = compute_market_margin(
            compound.stage, compound.has_successor);
    }
}

}  // namespace econlife
