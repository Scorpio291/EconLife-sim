#pragma once

// Financial Distribution Module — province-parallel tick module that processes
// all business-to-actor money flows each tick: salary payments, deferred salary
// recovery, quarterly bonus and dividend distributions, owner's draw, equity
// grant vesting, and suspicious transaction evidence generation.
//
// See docs/interfaces/financial_distribution/INTERFACE.md for the canonical specification.

#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/financial_distribution/financial_distribution_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPCBusiness;

// ---------------------------------------------------------------------------
// FinancialDistributionModule — ITickModule implementation for money flows
// ---------------------------------------------------------------------------
class FinancialDistributionModule : public ITickModule {
   public:
    explicit FinancialDistributionModule(const FinancialDistributionConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "financial_distribution"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"price_engine"}; }

    std::vector<std::string_view> runs_before() const override { return {"npc_business"}; }

    bool is_province_parallel() const noexcept override { return true; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- State access (for test injection and module initialization) ---

    // Per-business compensation records. The module owns these because
    // WorldState does not carry ExecutiveCompensation or BoardComposition.
    std::vector<BusinessCompensationRecord>& compensation_records() {
        return compensation_records_;
    }
    const std::vector<BusinessCompensationRecord>& compensation_records() const {
        return compensation_records_;
    }

    // Find compensation record for a business. Returns nullptr if not found.
    BusinessCompensationRecord* find_compensation_record(uint32_t business_id);
    const BusinessCompensationRecord* find_compensation_record(uint32_t business_id) const;

    // --- Static utility functions exposed for testing ---

    // Validate that a compensation mechanism is valid for the given business scale.
    // Returns true if valid, false otherwise.
    static bool is_mechanism_valid_for_scale(CompensationMechanism mechanism, BusinessScale scale);

    // Compute quarterly net profit.
    float compute_quarterly_net_profit(float revenue_per_tick, float cost_per_tick,
                                       float salary_per_tick) const;

    // Compute working capital floor for dividend payout protection.
    float compute_working_capital_floor(float cost_per_tick) const;

    // Determine if board approval is needed and granted.
    bool is_board_approved(const BoardComposition& board, uint32_t current_tick) const;

   private:
    FinancialDistributionConfig cfg_;
    std::vector<BusinessCompensationRecord> compensation_records_;

    // --- Per-province processing steps ---

    // Step 1: Process salary payments (including deferred salary recovery).
    void process_salary_payments(const NPCBusiness& business, BusinessCompensationRecord& record,
                                 const WorldState& state, DeltaBuffer& delta);

    // Step 2: Process owner's draw (micro businesses only).
    void process_owners_draw(const NPCBusiness& business, BusinessCompensationRecord& record,
                             const WorldState& state, DeltaBuffer& delta);

    // Step 3: Process quarterly bonus distribution.
    void process_quarterly_bonus(const NPCBusiness& business, BusinessCompensationRecord& record,
                                 const WorldState& state, DeltaBuffer& delta);

    // Step 4: Process quarterly dividend payout.
    void process_quarterly_dividend(const NPCBusiness& business, BusinessCompensationRecord& record,
                                    const WorldState& state, DeltaBuffer& delta);

    // Step 5: Process equity grant vesting (full_package only, large businesses).
    void process_equity_vesting(const NPCBusiness& business, BusinessCompensationRecord& record,
                                const WorldState& state, DeltaBuffer& delta);

    // Step 6: Update retained earnings from this tick's revenue and costs.
    void update_retained_earnings(const NPCBusiness& business, BusinessCompensationRecord& record);

    // --- Lookup helpers ---

    static const NPCBusiness* find_business(const WorldState& state, uint32_t business_id);
};

}  // namespace econlife
