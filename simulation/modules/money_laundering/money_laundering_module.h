#pragma once

// money_laundering module header.
// Sequential execution (not province-parallel); laundering operations are actor-level.
//
// Processes all active LaunderingOperation records each tick: transfers criminal
// revenue through one of six methods, deducting conversion losses, generating
// method-specific evidence tokens. Runs monthly FIU pattern analysis.
//
// See docs/interfaces/money_laundering/INTERFACE.md for the canonical specification.

#include "core/tick/tick_module.h"
#include "money_laundering_types.h"
#include <vector>

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;

class MoneyLaunderingModule : public ITickModule {
public:
    std::string_view name() const noexcept override { return "money_laundering"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"criminal_operations", "evidence"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"investigator_engine"};
    }

    bool is_province_parallel() const noexcept override { return false; }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (public for testing) ---

    // Compute transfer amount for this tick
    static float compute_transfer_this_tick(
        float launder_rate_per_tick,
        float dirty_amount,
        float laundered_so_far);

    // Compute clean amount after conversion loss
    static float compute_clean_amount(
        float transfer_this_tick,
        float conversion_loss_rate);

    // Check if structuring evidence should be generated this tick
    static bool should_generate_structuring_evidence(
        uint32_t current_tick,
        uint32_t started_tick,
        uint32_t structuring_token_interval);

    // Check if shell chain evidence should be generated for a chain node
    static bool should_generate_shell_chain_evidence(
        uint32_t current_tick,
        uint32_t started_tick,
        uint32_t shell_chain_evidence_interval);

    // Compute crypto evidence probability per tick
    static float compute_crypto_evidence_probability(
        float launder_rate_per_tick,
        float mixer_traceability,
        float max_le_skill,
        float crypto_evidence_skill_divisor);

    // Compute commingling capacity cap
    static float compute_commingling_capacity(
        float business_revenue_per_tick,
        float commingle_capacity_fraction,
        float rate_commingle_max);

    // Compute FIU structuring suspicion score
    static float compute_fiu_structuring_suspicion(
        uint32_t sub_threshold_deposit_count,
        uint32_t structuring_deposit_count_threshold);

    // Check if operation is completed
    static bool is_operation_completed(
        float laundered_so_far,
        float dirty_amount);

    // --- Named constants from INTERFACE.md ---
    static constexpr uint32_t STRUCTURING_TOKEN_INTERVAL         = 7;
    static constexpr uint32_t SHELL_CHAIN_EVIDENCE_INTERVAL      = 30;
    static constexpr uint32_t TRADE_INVOICE_EVIDENCE_INTERVAL    = 20;
    static constexpr uint32_t COMMINGLING_EVIDENCE_INTERVAL      = 15;
    static constexpr uint32_t MAX_CHAIN_DEPTH                    = 5;
    static constexpr float    COMMINGLE_CAPACITY_FRACTION        = 0.40f;
    static constexpr float    RATE_COMMINGLE_MAX                 = 5000.0f;
    static constexpr float    CRYPTO_EVIDENCE_SKILL_DIVISOR      = 10.0f;
    static constexpr float    FIU_TOKEN_THRESHOLD                = 0.35f;
    static constexpr float    FIU_METER_FILL_SCALE               = 0.10f;
    static constexpr uint32_t FIU_MONTHLY_INTERVAL               = 30;
    static constexpr uint32_t STRUCTURING_DEPOSIT_COUNT_THRESHOLD = 8;
    static constexpr float    ORG_CAPACITY_MULTIPLIER            = 0.25f;
    static constexpr uint32_t TICKS_PER_QUARTER                  = 90;

private:
    // Internal state: active operations
    std::vector<LaunderingOperation> operations_;
    std::vector<FIUPatternResult> fiu_results_;
};

}  // namespace econlife
