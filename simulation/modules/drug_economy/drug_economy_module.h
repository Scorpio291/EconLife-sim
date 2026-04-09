#pragma once

// drug_economy module header.
// Province-parallel execution: each province's drug production and distribution
// operates independently. Cross-province shipments use TransitShipment infrastructure.
//
// Processes drug supply chain: production at criminal facilities, wholesale/retail
// distribution, pricing through RegionalMarket informal layer, quality tracking.
//
// See docs/interfaces/drug_economy/INTERFACE.md for the canonical specification.

#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "drug_economy_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPCBusiness;

class DrugEconomyModule : public ITickModule {
   public:
    explicit DrugEconomyModule(const DrugEconomyConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "drug_economy"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override {
        return {"criminal_operations", "production"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"npc_spending", "addiction"};
    }

    bool is_province_parallel() const noexcept override { return true; }

    void init_for_tick(const WorldState& state) override;
    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Static utility functions (public for testing) ---

    // Compute wholesale price from retail spot price
    static float compute_wholesale_price(float retail_spot_price, float wholesale_price_fraction);

    // Degrade quality through distribution tier
    static float degrade_quality(float input_quality, float degradation_factor);

    // Compute consumer demand from addiction rate
    static float compute_addiction_demand(float addiction_rate, uint32_t province_population,
                                          float demand_per_addict);

    // Compute precursor consumption for a drug recipe
    static float compute_precursor_consumption(float drug_output, float precursor_ratio);

    // Check if a drug is legal in a province
    static bool is_drug_legal(const DrugLegalizationStatus& status, DrugType drug_type);

    // Compute meth chemical waste signature
    static float compute_meth_waste_signature(float output_quantity, float waste_per_unit);

   private:
    DrugEconomyConfig cfg_;
    // Internal state: production records per tick
    std::vector<DrugProductionRecord> production_records_;
    // Per-province legalization status (index = province_id)
    std::vector<DrugLegalizationStatus> legalization_status_;
};

}  // namespace econlife
