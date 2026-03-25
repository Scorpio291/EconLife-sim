#pragma once

// NPC Spending Module — province-parallel tick module that computes consumer
// demand from significant NPCs and background population cohorts, writing
// demand contributions to RegionalMarket.demand_buffer for next tick's price
// calculation.
//
// See docs/interfaces/npc_spending/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/tick/tick_module.h"
#include "modules/npc_spending/npc_spending_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;

// BuyerType forward-declared via economy_types.h include in npc_spending_types.h

// ---------------------------------------------------------------------------
// NpcSpendingModule — ITickModule implementation for consumer demand
// ---------------------------------------------------------------------------
class NpcSpendingModule : public ITickModule {
   public:
    std::string_view name() const noexcept override { return "npc_spending"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return true; }

    std::vector<std::string_view> runs_after() const override {
        return {"npc_behavior", "price_engine"};
    }

    std::vector<std::string_view> runs_before() const override { return {}; }

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- Module-internal NPC buyer profile storage ---
    // NPC struct does not carry BuyerType; this module manages the mapping.
    std::vector<NPCBuyerProfile>& buyer_profiles() { return buyer_profiles_; }
    const std::vector<NPCBuyerProfile>& buyer_profiles() const { return buyer_profiles_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute income factor: (capital / reference_income) ^ income_elasticity,
    // clamped to [0.0, max_income_factor].
    static float compute_income_factor(float capital, float reference_income,
                                       float income_elasticity, float max_income_factor);

    // Compute price factor: (base_price / max(spot_price, 0.01)) ^ |adjusted_elasticity|,
    // clamped to [min_price_factor, infinity).
    // adjusted_elasticity = price_elasticity * buyer_type_elasticity_modulator(buyer_type).
    static float compute_price_factor(float base_price, float spot_price, float price_elasticity,
                                      BuyerType buyer_type, float min_price_factor);

    // Compute quality factor: 1.0 + quality_weight * (batch_quality - market_quality_avg).
    static float compute_quality_factor(float batch_quality, float market_quality_avg,
                                        BuyerType buyer_type);

    // Compute full demand contribution for one NPC and one good.
    static float compute_demand_contribution(float base_demand_units, float income_factor,
                                             float price_factor, float quality_factor);

    // BuyerType elasticity modulator: necessity=0.1, price_sensitive=1.5,
    // quality_seeker=0.6, brand_loyal=0.8.
    static float buyer_type_elasticity_modulator(BuyerType buyer_type);

    // BuyerType quality weight: price_sensitive=0.0, brand_loyal=0.3,
    // quality_seeker=0.6, necessity_buyer=0.0.
    static float buyer_type_quality_weight(BuyerType buyer_type);

    // --- Constants ---
    struct Constants {
        static constexpr float reference_income = 1000.0f;
        static constexpr float max_income_factor = 5.0f;
        static constexpr float min_price_factor = 0.05f;
        static constexpr float default_base_demand_units = 1.0f;
        static constexpr float default_income_elasticity = 1.0f;
        static constexpr float default_price_elasticity = -1.0f;
        static constexpr float default_base_price = 10.0f;
        static constexpr float default_quality_weight = 0.0f;
    };

   private:
    std::vector<NPCBuyerProfile> buyer_profiles_;

    // Find buyer type for an NPC. Returns necessity_buyer if no profile found.
    BuyerType get_buyer_type(uint32_t npc_id) const;
};

}  // namespace econlife
