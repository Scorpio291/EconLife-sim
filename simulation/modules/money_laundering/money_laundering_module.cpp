#include "money_laundering_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ============================================================================
// Static utility functions
// ============================================================================

float MoneyLaunderingModule::compute_transfer_this_tick(float launder_rate_per_tick,
                                                        float dirty_amount,
                                                        float laundered_so_far) {
    float remaining = dirty_amount - laundered_so_far;
    if (remaining <= 0.0f)
        return 0.0f;
    return std::min(launder_rate_per_tick, remaining);
}

float MoneyLaunderingModule::compute_clean_amount(float transfer_this_tick,
                                                  float conversion_loss_rate) {
    return transfer_this_tick * (1.0f - conversion_loss_rate);
}

bool MoneyLaunderingModule::should_generate_structuring_evidence(
    uint32_t current_tick, uint32_t started_tick, uint32_t structuring_token_interval) {
    if (structuring_token_interval == 0)
        return false;
    uint32_t elapsed = current_tick - started_tick;
    return (elapsed > 0) && (elapsed % structuring_token_interval == 0);
}

bool MoneyLaunderingModule::should_generate_shell_chain_evidence(
    uint32_t current_tick, uint32_t started_tick, uint32_t shell_chain_evidence_interval) {
    if (shell_chain_evidence_interval == 0)
        return false;
    uint32_t elapsed = current_tick - started_tick;
    return (elapsed > 0) && (elapsed % shell_chain_evidence_interval == 0);
}

float MoneyLaunderingModule::compute_crypto_evidence_probability(
    float launder_rate_per_tick, float mixer_traceability, float max_le_skill,
    float crypto_evidence_skill_divisor) {
    if (crypto_evidence_skill_divisor <= 0.0f)
        return 0.0f;
    return launder_rate_per_tick * mixer_traceability * max_le_skill /
           crypto_evidence_skill_divisor;
}

float MoneyLaunderingModule::compute_commingling_capacity(float business_revenue_per_tick,
                                                          float commingle_capacity_fraction,
                                                          float rate_commingle_max) {
    float capacity = business_revenue_per_tick * commingle_capacity_fraction;
    return std::min(capacity, rate_commingle_max);
}

float MoneyLaunderingModule::compute_fiu_structuring_suspicion(
    uint32_t sub_threshold_deposit_count, uint32_t structuring_deposit_count_threshold) {
    if (structuring_deposit_count_threshold == 0)
        return 0.0f;
    if (sub_threshold_deposit_count <= structuring_deposit_count_threshold)
        return 0.0f;
    // Suspicion scales linearly above threshold
    float excess =
        static_cast<float>(sub_threshold_deposit_count - structuring_deposit_count_threshold);
    float suspicion = 0.35f + excess * 0.10f;
    return std::clamp(suspicion, 0.0f, 1.0f);
}

bool MoneyLaunderingModule::is_operation_completed(float laundered_so_far, float dirty_amount) {
    return laundered_so_far >= dirty_amount;
}

// ============================================================================
// Sequential execution
// ============================================================================

void MoneyLaunderingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Drain laundering seeds emitted this tick by criminal_operations (the
    // producer runs earlier in tick order). Each seed opens a shell-company
    // operation for the org's illicit cash; per-method rates come from config.
    // Same-tick queue: cleared after draining via the const_cast carve-out.
    if (!state.pending_laundering_seeds.empty()) {
        for (const auto& seed : state.pending_laundering_seeds) {
            // Dedup: one seeded operation per actor (org leadership).
            bool exists = false;
            for (const auto& op : operations_) {
                if (op.id == seed.actor_id) {
                    exists = true;
                    break;
                }
            }
            if (exists || seed.dirty_amount <= 0.0f)
                continue;

            LaunderingOperation op{};
            op.id = seed.actor_id;  // one seeded op per actor
            op.actor_id = seed.actor_id;
            op.method = LaunderingMethod::shell_company_chain;
            op.dirty_amount = seed.dirty_amount;
            op.laundered_so_far = 0.0f;
            op.launder_rate_per_tick = cfg_.seed_launder_rate_per_tick;
            op.conversion_loss_rate = cfg_.seed_conversion_loss_rate;
            op.started_tick = state.current_tick;
            op.destination_business_id = seed.destination_business_id;
            op.evidence_generated_total = 0.0f;
            op.paused = false;
            op.completed = false;
            operations_.push_back(std::move(op));
        }
        const_cast<std::vector<LaunderingSeedDelta>&>(state.pending_laundering_seeds).clear();
    }

    // Process all active operations sorted by id ascending for determinism
    std::sort(
        operations_.begin(), operations_.end(),
        [](const LaunderingOperation& a, const LaunderingOperation& b) { return a.id < b.id; });

    for (auto& op : operations_) {
        // Skip paused or completed operations
        if (op.paused || op.completed)
            continue;

        // Compute transfer this tick
        float transfer = compute_transfer_this_tick(op.launder_rate_per_tick, op.dirty_amount,
                                                    op.laundered_so_far);

        if (transfer <= 0.0f)
            continue;

        // Compute clean amount after conversion loss
        float clean = compute_clean_amount(transfer, op.conversion_loss_rate);

        // Update operation progress
        op.laundered_so_far += transfer;

        // Debit source (player informal_cash or criminal org cash)
        // For player-initiated operations:
        if (op.destination_business_id == 0) {
            // Direct to player wealth
            delta.player_delta.wealth_delta =
                delta.player_delta.wealth_delta.value_or(0.0f) + clean;
        } else {
            // Three-stage pipeline: placement debit then integration credit to destination business
            // Placement: deduct dirty amount from source criminal business
            BusinessDelta placement;
            placement.business_id = op.destination_business_id;
            placement.cash_delta = -transfer;
            delta.business_deltas.push_back(placement);

            // Integration: credit clean amount to destination business after conversion loss
            BusinessDelta integration;
            integration.business_id = op.destination_business_id;
            integration.cash_delta = clean;
            delta.business_deltas.push_back(integration);
        }

        // RegionDelta: criminal_dominance_delta if laundering volume is high this tick
        // High-volume threshold: transfer > 10% of a typical daily revenue proxy (1000 units)
        constexpr float HIGH_VOLUME_THRESHOLD = 100.0f;
        if (transfer >= HIGH_VOLUME_THRESHOLD) {
            RegionDelta region;
            region.region_id = 0;  // global / province 0; in full impl reads op's province
            region.criminal_dominance_delta = transfer / 10000.0f;  // scale to small delta
            delta.region_deltas.push_back(region);
        }

        // Generate method-specific evidence
        bool generate_evidence = false;
        EvidenceType evidence_type = EvidenceType::financial;

        switch (op.method) {
            case LaunderingMethod::structuring:
                generate_evidence = should_generate_structuring_evidence(
                    state.current_tick, op.started_tick, cfg_.structuring_token_interval);
                evidence_type = EvidenceType::financial;
                break;

            case LaunderingMethod::shell_company_chain:
                // Generate evidence per chain node per interval
                if (!op.shell_chain_business_ids.empty()) {
                    generate_evidence = should_generate_shell_chain_evidence(
                        state.current_tick, op.started_tick, cfg_.shell_chain_evidence_interval);
                    if (generate_evidence) {
                        // Generate one token per chain node
                        for (size_t i = 1; i < op.shell_chain_business_ids.size(); ++i) {
                            EvidenceDelta ev;
                            ev.new_token = EvidenceToken{
                                0,      EvidenceType::documentary, op.actor_id, op.actor_id, 0.25f,
                                0.002f, state.current_tick,        0,           true};
                            delta.evidence_deltas.push_back(ev);
                            op.evidence_generated_total += 1.0f;
                        }
                    }
                }
                evidence_type = EvidenceType::documentary;
                break;

            case LaunderingMethod::real_estate:
                // One evidence token on transaction
                generate_evidence = (op.laundered_so_far == transfer);  // first tick
                evidence_type = EvidenceType::financial;
                break;

            case LaunderingMethod::trade_invoice:
                generate_evidence = (state.current_tick >= op.started_tick) &&
                                    ((state.current_tick - op.started_tick) %
                                         cfg_.trade_invoice_evidence_interval ==
                                     0) &&
                                    ((state.current_tick - op.started_tick) > 0);
                evidence_type = EvidenceType::documentary;
                break;

            case LaunderingMethod::crypto_mixing:
                // Probability-based; simplified: generate every 10 ticks
                generate_evidence = (state.current_tick >= op.started_tick) &&
                                    ((state.current_tick - op.started_tick) % 10 == 0) &&
                                    ((state.current_tick - op.started_tick) > 0);
                evidence_type = EvidenceType::digital;
                break;

            case LaunderingMethod::cash_commingling:
                generate_evidence =
                    (state.current_tick >= op.started_tick) &&
                    ((state.current_tick - op.started_tick) % cfg_.commingling_evidence_interval ==
                     0) &&
                    ((state.current_tick - op.started_tick) > 0);
                evidence_type = EvidenceType::testimonial;
                break;
        }

        if (generate_evidence && op.method != LaunderingMethod::shell_company_chain) {
            EvidenceDelta ev;
            ev.new_token =
                EvidenceToken{0,      evidence_type,      op.actor_id, op.actor_id, 0.30f,
                              0.002f, state.current_tick, 0,           true};
            delta.evidence_deltas.push_back(ev);
            op.evidence_generated_total += 1.0f;
        }

        // Check completion
        if (is_operation_completed(op.laundered_so_far, op.dirty_amount)) {
            op.completed = true;
        }
    }

    // Monthly FIU pattern analysis
    if (state.current_tick > 0 && state.current_tick % cfg_.fiu_monthly_interval == 0) {
        fiu_results_.clear();
        // Simplified FIU: scan operations for structuring patterns
        // Count sub-threshold deposits per actor in last 30 ticks
        std::map<uint32_t, uint32_t> actor_deposit_counts;
        for (const auto& op : operations_) {
            if (op.method == LaunderingMethod::structuring && !op.completed && !op.paused) {
                uint32_t ticks_active = state.current_tick - op.started_tick;
                uint32_t deposit_count = ticks_active;  // simplified: one deposit per tick
                actor_deposit_counts[op.actor_id] += deposit_count;
            }
        }

        for (const auto& [actor_id, count] : actor_deposit_counts) {
            float suspicion =
                compute_fiu_structuring_suspicion(count, cfg_.structuring_deposit_count_threshold);
            if (suspicion > cfg_.fiu_token_threshold) {
                fiu_results_.push_back(
                    FIUPatternResult{actor_id, suspicion, LaunderingMethod::structuring});

                // Generate financial evidence token
                EvidenceDelta ev;
                ev.new_token =
                    EvidenceToken{0,      EvidenceType::financial, 0, actor_id, suspicion,
                                  0.005f, state.current_tick,      0, true};
                delta.evidence_deltas.push_back(ev);
            }
        }
    }
}

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 op_count
//   for each LaunderingOperation:
//     u32 id, u32 actor_id, u8 method
//     f32 dirty_amount, f32 laundered_so_far
//     f32 launder_rate_per_tick, f32 conversion_loss_rate
//     u32 started_tick, u32 destination_business_id
//     u32 shell_chain_count, u32[shell_chain_count]
//     f32 evidence_generated_total, u8 paused, u8 completed
//   u32 fiu_count
//   for each FIUPatternResult:
//     u32 target_actor_id, f32 suspicion_score, u8 inferred_method

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

void MoneyLaunderingModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);
    put_u32(out, static_cast<uint32_t>(operations_.size()));
    for (const auto& op : operations_) {
        put_u32(out, op.id);
        put_u32(out, op.actor_id);
        out.push_back(static_cast<uint8_t>(op.method));
        put_f32(out, op.dirty_amount);
        put_f32(out, op.laundered_so_far);
        put_f32(out, op.launder_rate_per_tick);
        put_f32(out, op.conversion_loss_rate);
        put_u32(out, op.started_tick);
        put_u32(out, op.destination_business_id);
        put_u32(out, static_cast<uint32_t>(op.shell_chain_business_ids.size()));
        for (uint32_t id : op.shell_chain_business_ids)
            put_u32(out, id);
        put_f32(out, op.evidence_generated_total);
        out.push_back(op.paused ? 1u : 0u);
        out.push_back(op.completed ? 1u : 0u);
    }
    put_u32(out, static_cast<uint32_t>(fiu_results_.size()));
    for (const auto& f : fiu_results_) {
        put_u32(out, f.target_actor_id);
        put_f32(out, f.suspicion_score);
        out.push_back(static_cast<uint8_t>(f.inferred_method));
    }
}

bool MoneyLaunderingModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;
    uint32_t op_count = r.u32();
    operations_.clear();
    operations_.reserve(op_count);
    for (uint32_t i = 0; i < op_count; ++i) {
        LaunderingOperation op{};
        op.id = r.u32();
        op.actor_id = r.u32();
        op.method = static_cast<LaunderingMethod>(r.u8());
        op.dirty_amount = r.f32();
        op.laundered_so_far = r.f32();
        op.launder_rate_per_tick = r.f32();
        op.conversion_loss_rate = r.f32();
        op.started_tick = r.u32();
        op.destination_business_id = r.u32();
        uint32_t chain_n = r.u32();
        if (r.error)
            return false;
        op.shell_chain_business_ids.reserve(chain_n);
        for (uint32_t j = 0; j < chain_n; ++j)
            op.shell_chain_business_ids.push_back(r.u32());
        op.evidence_generated_total = r.f32();
        op.paused = (r.u8() != 0);
        op.completed = (r.u8() != 0);
        if (r.error)
            return false;
        operations_.push_back(std::move(op));
    }
    uint32_t fiu_count = r.u32();
    fiu_results_.clear();
    fiu_results_.reserve(fiu_count);
    for (uint32_t i = 0; i < fiu_count; ++i) {
        FIUPatternResult f{};
        f.target_actor_id = r.u32();
        f.suspicion_score = r.f32();
        f.inferred_method = static_cast<LaunderingMethod>(r.u8());
        if (r.error)
            return false;
        fiu_results_.push_back(f);
    }
    return !r.error;
}

}  // namespace econlife
