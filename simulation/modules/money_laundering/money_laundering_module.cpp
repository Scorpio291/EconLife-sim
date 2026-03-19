#include "money_laundering_module.h"

#include "core/world_state/world_state.h"
#include "core/world_state/player.h"

#include <algorithm>
#include <cmath>

namespace econlife {

// ============================================================================
// Static utility functions
// ============================================================================

float MoneyLaunderingModule::compute_transfer_this_tick(
    float launder_rate_per_tick,
    float dirty_amount,
    float laundered_so_far)
{
    float remaining = dirty_amount - laundered_so_far;
    if (remaining <= 0.0f) return 0.0f;
    return std::min(launder_rate_per_tick, remaining);
}

float MoneyLaunderingModule::compute_clean_amount(
    float transfer_this_tick,
    float conversion_loss_rate)
{
    return transfer_this_tick * (1.0f - conversion_loss_rate);
}

bool MoneyLaunderingModule::should_generate_structuring_evidence(
    uint32_t current_tick,
    uint32_t started_tick,
    uint32_t structuring_token_interval)
{
    if (structuring_token_interval == 0) return false;
    uint32_t elapsed = current_tick - started_tick;
    return (elapsed > 0) && (elapsed % structuring_token_interval == 0);
}

bool MoneyLaunderingModule::should_generate_shell_chain_evidence(
    uint32_t current_tick,
    uint32_t started_tick,
    uint32_t shell_chain_evidence_interval)
{
    if (shell_chain_evidence_interval == 0) return false;
    uint32_t elapsed = current_tick - started_tick;
    return (elapsed > 0) && (elapsed % shell_chain_evidence_interval == 0);
}

float MoneyLaunderingModule::compute_crypto_evidence_probability(
    float launder_rate_per_tick,
    float mixer_traceability,
    float max_le_skill,
    float crypto_evidence_skill_divisor)
{
    if (crypto_evidence_skill_divisor <= 0.0f) return 0.0f;
    return launder_rate_per_tick * mixer_traceability * max_le_skill / crypto_evidence_skill_divisor;
}

float MoneyLaunderingModule::compute_commingling_capacity(
    float business_revenue_per_tick,
    float commingle_capacity_fraction,
    float rate_commingle_max)
{
    float capacity = business_revenue_per_tick * commingle_capacity_fraction;
    return std::min(capacity, rate_commingle_max);
}

float MoneyLaunderingModule::compute_fiu_structuring_suspicion(
    uint32_t sub_threshold_deposit_count,
    uint32_t structuring_deposit_count_threshold)
{
    if (structuring_deposit_count_threshold == 0) return 0.0f;
    if (sub_threshold_deposit_count <= structuring_deposit_count_threshold) return 0.0f;
    // Suspicion scales linearly above threshold
    float excess = static_cast<float>(sub_threshold_deposit_count - structuring_deposit_count_threshold);
    float suspicion = 0.35f + excess * 0.10f;
    return std::clamp(suspicion, 0.0f, 1.0f);
}

bool MoneyLaunderingModule::is_operation_completed(
    float laundered_so_far,
    float dirty_amount)
{
    return laundered_so_far >= dirty_amount;
}

// ============================================================================
// Sequential execution
// ============================================================================

void MoneyLaunderingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Process all active operations sorted by id ascending for determinism
    std::sort(operations_.begin(), operations_.end(),
              [](const LaunderingOperation& a, const LaunderingOperation& b) {
                  return a.id < b.id;
              });

    for (auto& op : operations_) {
        // Skip paused or completed operations
        if (op.paused || op.completed) continue;

        // Compute transfer this tick
        float transfer = compute_transfer_this_tick(
            op.launder_rate_per_tick, op.dirty_amount, op.laundered_so_far);

        if (transfer <= 0.0f) continue;

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
        }

        // Generate method-specific evidence
        bool generate_evidence = false;
        EvidenceType evidence_type = EvidenceType::financial;

        switch (op.method) {
            case LaunderingMethod::structuring:
                generate_evidence = should_generate_structuring_evidence(
                    state.current_tick, op.started_tick, STRUCTURING_TOKEN_INTERVAL);
                evidence_type = EvidenceType::financial;
                break;

            case LaunderingMethod::shell_company_chain:
                // Generate evidence per chain node per interval
                if (!op.shell_chain_business_ids.empty()) {
                    generate_evidence = should_generate_shell_chain_evidence(
                        state.current_tick, op.started_tick, SHELL_CHAIN_EVIDENCE_INTERVAL);
                    if (generate_evidence) {
                        // Generate one token per chain node
                        for (size_t i = 1; i < op.shell_chain_business_ids.size(); ++i) {
                            EvidenceDelta ev;
                            ev.new_token = EvidenceToken{
                                0, EvidenceType::documentary,
                                op.actor_id, op.actor_id,
                                0.25f, 0.002f,
                                state.current_tick, 0, true
                            };
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
                    ((state.current_tick - op.started_tick) % TRADE_INVOICE_EVIDENCE_INTERVAL == 0) &&
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
                generate_evidence = (state.current_tick >= op.started_tick) &&
                    ((state.current_tick - op.started_tick) % COMMINGLING_EVIDENCE_INTERVAL == 0) &&
                    ((state.current_tick - op.started_tick) > 0);
                evidence_type = EvidenceType::testimonial;
                break;
        }

        if (generate_evidence && op.method != LaunderingMethod::shell_company_chain) {
            EvidenceDelta ev;
            ev.new_token = EvidenceToken{
                0, evidence_type,
                op.actor_id, op.actor_id,
                0.30f, 0.002f,
                state.current_tick, 0, true
            };
            delta.evidence_deltas.push_back(ev);
            op.evidence_generated_total += 1.0f;
        }

        // Check completion
        if (is_operation_completed(op.laundered_so_far, op.dirty_amount)) {
            op.completed = true;
        }
    }

    // Monthly FIU pattern analysis
    if (state.current_tick > 0 && state.current_tick % FIU_MONTHLY_INTERVAL == 0) {
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
            float suspicion = compute_fiu_structuring_suspicion(
                count, STRUCTURING_DEPOSIT_COUNT_THRESHOLD);
            if (suspicion > FIU_TOKEN_THRESHOLD) {
                fiu_results_.push_back(FIUPatternResult{
                    actor_id, suspicion, LaunderingMethod::structuring});

                // Generate financial evidence token
                EvidenceDelta ev;
                ev.new_token = EvidenceToken{
                    0, EvidenceType::financial,
                    0, actor_id,
                    suspicion, 0.005f,
                    state.current_tick, 0, true
                };
                delta.evidence_deltas.push_back(ev);
            }
        }
    }
}

}  // namespace econlife
