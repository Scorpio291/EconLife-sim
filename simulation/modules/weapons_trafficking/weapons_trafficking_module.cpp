#include "weapons_trafficking_module.h"

#include <algorithm>
#include <cmath>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"
#include "modules/criminal_operations/criminal_operations_types.h"

namespace econlife {

// ============================================================================
// Static utility functions
// ============================================================================

float WeaponsTraffickingModule::compute_informal_spot_price(float base_price,
                                                            float conflict_demand_modifier,
                                                            float supply_this_tick,
                                                            float price_floor_supply) {
    float effective_supply = std::max(supply_this_tick, price_floor_supply);
    return base_price * (1.0f + conflict_demand_modifier) / effective_supply;
}

float WeaponsTraffickingModule::get_conflict_demand_modifier(uint8_t conflict_stage) {
    return TerritorialConflictDemandModifier::get_modifier(conflict_stage);
}

float WeaponsTraffickingModule::compute_diversion_output(float total_output,
                                                         float diversion_fraction,
                                                         float max_diversion_fraction) {
    float clamped_fraction = clamp_diversion_fraction(diversion_fraction, max_diversion_fraction);
    return total_output * clamped_fraction;
}

float WeaponsTraffickingModule::clamp_diversion_fraction(float requested_fraction,
                                                         float max_diversion_fraction) {
    return std::clamp(requested_fraction, 0.0f, max_diversion_fraction);
}

bool WeaponsTraffickingModule::is_embargo_item(WeaponType weapon_type) {
    return weapon_type == WeaponType::heavy_weapons;
}

float WeaponsTraffickingModule::compute_chain_custody_actionability(float base_actionability) {
    return std::clamp(base_actionability, 0.0f, 1.0f);
}

// ============================================================================
// Pre-parallel initialization
// ============================================================================

void WeaponsTraffickingModule::init_for_tick(const WorldState& state) {
    // Clear per-tick audit records on main thread before parallel dispatch.
    diversion_records_.clear();
    procurement_records_.clear();
}

// ============================================================================
// Province-parallel execution
// ============================================================================

void WeaponsTraffickingModule::execute_province(uint32_t province_idx, const WorldState& state,
                                                DeltaBuffer& province_delta) {
    if (province_idx >= state.provinces.size())
        return;
    const auto& province = state.provinces[province_idx];

    // Determine territorial conflict stage for demand modifier
    // Read from province conditions (criminal_dominance_index as proxy)
    // In full impl, reads from CriminalOrganization conflict_state
    uint8_t conflict_stage = 0;
    if (province.conditions.criminal_dominance_index > 0.8f) {
        conflict_stage = 5;  // open_warfare
    } else if (province.conditions.criminal_dominance_index > 0.6f) {
        conflict_stage = 4;  // personnel_violence
    } else if (province.conditions.criminal_dominance_index > 0.4f) {
        conflict_stage = 3;  // property_violence
    } else if (province.conditions.criminal_dominance_index > 0.2f) {
        conflict_stage = 1;  // economic
    }

    float demand_modifier = get_conflict_demand_modifier(conflict_stage);

    // Phase 1: Process manufacturing businesses with weapon diversion
    // Collect manufacturing businesses sorted by id ascending
    std::vector<const NPCBusiness*> manufacturers;
    for (const auto& biz : state.npc_businesses) {
        if (biz.province_id == province.id && biz.sector == BusinessSector::manufacturing &&
            biz.regulatory_violation_severity > 0.0f) {
            // regulatory_violation_severity > 0 serves as proxy for diversion_fraction
            manufacturers.push_back(&biz);
        }
    }
    std::sort(manufacturers.begin(), manufacturers.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    float total_weapon_supply = 0.0f;

    for (const auto* biz : manufacturers) {
        float total_output = biz->revenue_per_tick * 0.01f;  // proxy for weapon output
        if (total_output <= 0.0f)
            continue;

        float diversion_fraction = biz->regulatory_violation_severity * 0.5f;  // proxy
        diversion_fraction = clamp_diversion_fraction(diversion_fraction, cfg_.max_diversion_fraction);

        float diverted =
            compute_diversion_output(total_output, diversion_fraction, cfg_.max_diversion_fraction);
        float formal = total_output - diverted;

        total_weapon_supply += diverted;

        // Diversion record tracking omitted during parallel execution
        // to avoid data races on the shared diversion_records_ vector.

        // BusinessDelta: credit weapons sales revenue to the diverted business
        if (diverted > 0.0f) {
            // Revenue = diverted quantity * informal spot price
            float informal_price = compute_informal_spot_price(
                cfg_.base_price_small_arms, demand_modifier, diverted, cfg_.price_floor_supply);
            float sales_revenue = diverted * informal_price;

            BusinessDelta revenue_delta;
            revenue_delta.business_id = biz->id;
            revenue_delta.cash_delta = sales_revenue;
            revenue_delta.revenue_per_tick_update = sales_revenue;
            province_delta.business_deltas.push_back(revenue_delta);
        }

        // Generate documentary evidence per diverted shipment (inventory discrepancy)
        if (diverted > 0.0f) {
            EvidenceDelta ev;
            ev.new_token = EvidenceToken{
                0,      EvidenceType::documentary, biz->owner_id, biz->owner_id, 0.25f,
                0.003f, state.current_tick,        province.id,   true};
            province_delta.evidence_deltas.push_back(ev);

            // Physical trafficking evidence (contraband seizure risk)
            EvidenceDelta phys_ev;
            phys_ev.new_token =
                EvidenceToken{0,      EvidenceType::physical, biz->owner_id, biz->owner_id, 0.35f,
                              0.002f, state.current_tick,     province.id,   true};
            province_delta.evidence_deltas.push_back(phys_ev);
        }

        // Add diverted supply to informal market
        MarketDelta supply_delta;
        supply_delta.good_id = static_cast<uint32_t>(WeaponType::small_arms);
        supply_delta.region_id = province.id;
        supply_delta.supply_delta = diverted;
        province_delta.market_deltas.push_back(supply_delta);
    }

    // Phase 2: Weapons demand from criminal orgs (territorial conflict)
    if (demand_modifier > 0.0f) {
        // Demand scales with conflict stage
        float base_demand = 10.0f;  // proxy base demand
        float conflict_demand = base_demand * (1.0f + demand_modifier);

        MarketDelta demand_delta;
        demand_delta.good_id = static_cast<uint32_t>(WeaponType::small_arms);
        demand_delta.region_id = province.id;
        demand_delta.demand_buffer_delta = conflict_demand;
        province_delta.market_deltas.push_back(demand_delta);
    }

    // Phase 3: Check for heavy weapons transfers (embargo items)
    // Simplified: detect any heavy_weapons activity in province
    // In full impl, checks TransitShipment arrivals for heavy_weapons
    for (const auto& biz : state.npc_businesses) {
        if (biz.criminal_sector && biz.province_id == province.id) {
            // If business has very high violation severity, proxy for heavy weapons
            if (biz.regulatory_violation_severity > 0.8f) {
                // Heavy weapons embargo: spike all LE NPCs in province
                for (const auto& npc : state.significant_npcs) {
                    if (npc.current_province_id == province.id &&
                        npc.role == NPCRole::law_enforcement && npc.status == NPCStatus::active) {
                        // InvestigatorMeter spike — cannot be suppressed by corruption
                        // In full impl, directly modifies InvestigatorMeter.current_level
                        // Here, emit evidence with high actionability to represent the spike
                        EvidenceDelta ev;
                        ev.new_token = EvidenceToken{0,
                                                     EvidenceType::physical,
                                                     biz.owner_id,
                                                     biz.owner_id,
                                                     cfg_.embargo_meter_spike,
                                                     0.001f,
                                                     state.current_tick,
                                                     province.id,
                                                     true};
                        province_delta.evidence_deltas.push_back(ev);
                    }
                }

                // Queue embargo_investigation consequence (cannot be suppressed)
                ConsequenceDelta cons;
                cons.new_entry_id = biz.id;
                province_delta.consequence_deltas.push_back(cons);
                break;  // one per province per tick
            }
        }
    }
}

void WeaponsTraffickingModule::execute(const WorldState& state, DeltaBuffer& delta) {
    for (uint32_t i = 0; i < state.provinces.size(); ++i) {
        execute_province(i, state, delta);
    }
}

}  // namespace econlife
