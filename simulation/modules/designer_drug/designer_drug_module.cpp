#include "designer_drug_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/world_state/apply_deltas.h"  // lookup_good_id
#include "core/world_state/delta_buffer.h"
#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

bool DesignerDrugModule::is_detection_triggered(float cumulative_evidence, float threshold) {
    return cumulative_evidence >= threshold;
}

uint32_t DesignerDrugModule::compute_review_duration(uint32_t base_duration,
                                                     float political_delay) {
    return static_cast<uint32_t>(static_cast<float>(base_duration) * political_delay);
}

float DesignerDrugModule::compute_market_margin(SchedulingStage stage, bool has_successor) const {
    switch (stage) {
        case SchedulingStage::unscheduled:
        case SchedulingStage::review_initiated:
            return cfg_.unscheduled_margin;
        case SchedulingStage::scheduled:
            return has_successor ? cfg_.scheduled_margin : cfg_.no_successor_margin;
        default:
            return cfg_.scheduled_margin;
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

    bool monthly_check = should_check_detection(state.current_tick, cfg_.monthly_interval);

    for (auto& compound : compounds_) {
        if (compound.stage == SchedulingStage::scheduled) {
            // Scheduled compounds run no R&D / detection. They keep supplying the
            // informal market via a successor product line; with no successor,
            // supply drops to zero (INTERFACE).
            if (compound.has_successor) {
                emit_grounded_supply(state, compound, delta);
            }
            compound.market_margin_multiplier =
                compute_market_margin(compound.stage, compound.has_successor);
            continue;
        }

        // BusinessDelta: R&D investment cost each tick for active (unscheduled) compounds
        // Find the criminal business owned by creator_actor_id in the compound's province
        for (const auto& biz : state.npc_businesses) {
            if (biz.owner_id == compound.creator_actor_id &&
                biz.province_id == compound.province_id && biz.criminal_sector) {
                // R&D investment rate: 1% of revenue per tick
                constexpr float RD_INVESTMENT_RATE = 0.01f;
                float rd_cost = biz.revenue_per_tick * RD_INVESTMENT_RATE;
                if (rd_cost > 0.0f) {
                    BusinessDelta rd_delta;
                    rd_delta.business_id = biz.id;
                    rd_delta.cash_delta = -rd_cost;
                    delta.business_deltas.push_back(rd_delta);
                }
                break;
            }
        }

        if (monthly_check && compound.stage == SchedulingStage::unscheduled) {
            float evidence_sum = 0.0f;
            for (const auto& token : state.evidence_pool) {
                if (token.is_active && token.target_npc_id == compound.creator_actor_id &&
                    (token.type == EvidenceType::financial ||
                     token.type == EvidenceType::physical)) {
                    evidence_sum += token.actionability;
                }
            }
            compound.cumulative_evidence_weight = evidence_sum;

            if (is_detection_triggered(compound.cumulative_evidence_weight,
                                       compound.detection_threshold)) {
                compound.stage = SchedulingStage::review_initiated;
                compound.review_start_tick = state.current_tick;
            }
        }

        if (compound.stage == SchedulingStage::review_initiated) {
            uint32_t elapsed = state.current_tick - compound.review_start_tick;
            // Slower (more corrupt) political systems take longer to schedule:
            // political_delay = 1 + province corruption_index.
            float political_delay = 1.0f;
            for (const auto& prov : state.provinces) {
                if (prov.id == compound.province_id) {
                    political_delay = 1.0f + prov.political.corruption_index;
                    break;
                }
            }
            if (elapsed >= compute_review_duration(compound.review_duration, political_delay)) {
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

                compound.market_margin_multiplier =
                    compute_market_margin(compound.stage, compound.has_successor);

                // Scheduling enactment fires as regional political fallout in
                // the compound's province. Do NOT use legal/criminal categories
                // here: those seed a legal case against target_id at fire time,
                // and a classification action has no defendant (target 0 would
                // prosecute the player).
                ConsequenceDelta cons;
                cons.new_consequence = make_consequence(
                    compound.compound_id, ConsequenceCategory::political_consequence, 0, 0,
                    compound.province_id, state.current_tick);
                delta.consequence_deltas.push_back(cons);
            }
        }

        // MarketDelta: unscheduled / under-review compounds supply the market each
        // tick (scheduled compounds are handled at the top of the loop).
        if (compound.stage == SchedulingStage::unscheduled ||
            compound.stage == SchedulingStage::review_initiated) {
            emit_grounded_supply(state, compound, delta);
        }

        compound.market_margin_multiplier =
            compute_market_margin(compound.stage, compound.has_successor);
    }
}

void DesignerDrugModule::emit_grounded_supply(const WorldState& state,
                                              const DesignerDrugCompound& compound,
                                              DeltaBuffer& delta) const {
    // Production requires a real operation: the creator's criminal business in the
    // compound's province. No operation -> no supply (a compound cannot make itself).
    const NPCBusiness* creator = nullptr;
    for (const auto& biz : state.npc_businesses) {
        if (biz.owner_id == compound.creator_actor_id &&
            biz.province_id == compound.province_id && biz.criminal_sector) {
            creator = &biz;
            break;
        }
    }
    if (creator == nullptr)
        return;

    // Per-tick capacity, scaled by the market margin (legal status makes the line
    // more or less worth running). Bottlenecked on REAL precursor stock located in
    // the province; the precursors are physically consumed (conservation) — you
    // cannot synthesise a compound from precursors you do not have.
    float capacity = cfg_.base_output_per_tick * compound.market_margin_multiplier;
    if (capacity <= 0.0f)
        return;

    float production = capacity;
    float precursor_consumed = 0.0f;
    uint32_t precursor_gid = 0;
    if (!cfg_.precursor_good.empty() && cfg_.precursor_ratio > 0.0f) {
        precursor_gid = lookup_good_id(state, cfg_.precursor_good);
        float available = 0.0f;
        if (precursor_gid != 0) {
            const uint64_t pkey = (static_cast<uint64_t>(precursor_gid) << 32) |
                                  static_cast<uint64_t>(compound.province_id);
            auto it = state.market_index_by_good_province.find(pkey);
            if (it != state.market_index_by_good_province.end() &&
                it->second < state.regional_markets.size()) {
                available = std::max(0.0f, state.regional_markets[it->second].supply);
            }
        }
        production = std::min(capacity, available / cfg_.precursor_ratio);
        precursor_consumed = production * cfg_.precursor_ratio;
    }
    if (production <= 0.0f)
        return;

    MarketDelta supply_entry;
    supply_entry.good_id = compound.compound_id;
    supply_entry.region_id = compound.province_id;
    supply_entry.supply_delta = production;
    delta.market_deltas.push_back(supply_entry);

    // Conservation: the precursors physically consumed leave the located stock, with
    // a matching demand signal for price formation.
    if (precursor_gid != 0 && precursor_consumed > 0.0f) {
        MarketDelta precursor_md;
        precursor_md.good_id = precursor_gid;
        precursor_md.region_id = compound.province_id;
        precursor_md.supply_delta = -precursor_consumed;
        precursor_md.demand_buffer_delta = precursor_consumed;
        delta.market_deltas.push_back(precursor_md);
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────

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

void put_string(std::vector<uint8_t>& out, const std::string& s) {
    put_u32(out, static_cast<uint32_t>(s.size()));
    for (char c : s)
        out.push_back(static_cast<uint8_t>(c));
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
    std::string str() {
        uint32_t n = u32();
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return s;
    }
};

}  // namespace

void DesignerDrugModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(compounds_.size()));
    for (const auto& c : compounds_) {
        put_u32(out, c.compound_id);
        put_u32(out, c.creator_actor_id);
        put_string(out, c.goods_key);
        out.push_back(static_cast<uint8_t>(c.stage));
        put_f32(out, c.cumulative_evidence_weight);
        put_f32(out, c.detection_threshold);
        put_u32(out, c.review_start_tick);
        put_u32(out, c.review_duration);
        put_f32(out, c.market_margin_multiplier);
        put_u32(out, c.province_id);
        out.push_back(c.has_successor ? 1u : 0u);
    }
}

bool DesignerDrugModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t count = r.u32();
    compounds_.clear();
    compounds_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        DesignerDrugCompound c{};
        c.compound_id = r.u32();
        c.creator_actor_id = r.u32();
        c.goods_key = r.str();
        c.stage = static_cast<SchedulingStage>(r.u8());
        c.cumulative_evidence_weight = r.f32();
        c.detection_threshold = r.f32();
        c.review_start_tick = r.u32();
        c.review_duration = r.u32();
        c.market_margin_multiplier = r.f32();
        c.province_id = r.u32();
        c.has_successor = (r.u8() != 0);
        if (r.error)
            return false;
        compounds_.push_back(std::move(c));
    }
    return !r.error;
}

}  // namespace econlife
