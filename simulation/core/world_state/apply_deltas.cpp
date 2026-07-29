// apply_deltas — mutates WorldState by applying accumulated DeltaBuffer changes.
//
// Rules:
// - Additive optional<float> fields: add value, clamp to domain range
// - Replacement optional fields: overwrite target
// - Append vectors: push_back new entries
// - NaN protection: any NaN delta is treated as 0.0

#include "apply_deltas.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "core/config/package_config.h"
#include "core/good_id_hash.h"
#include "core/tick/deferred_work.h"
#include "core/world_gen/goods_catalog.h"
#include "modules/technology/technology_types.h"
#include "player.h"
#include "world_state.h"

namespace econlife {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
static float clamp_neg1_1(float v) {
    return std::clamp(v, -1.0f, 1.0f);
}
static float safe_add(float base, float delta) {
    return std::isnan(delta) ? base : base + delta;
}

// Default safety ceilings — used when no config is provided.
static const SafetyCeilingsConfig DEFAULT_CEILINGS{};

// ---------------------------------------------------------------------------
// apply_npc_deltas
// ---------------------------------------------------------------------------
static void apply_npc_deltas(WorldState& world, const std::vector<NPCDelta>& deltas,
                             const SafetyCeilingsConfig& ceil) {
    if (deltas.empty())
        return;

    // Build index for O(1) NPC lookup instead of O(N) per delta.
    std::unordered_map<uint32_t, NPC*> npc_index;
    npc_index.reserve(world.significant_npcs.size());
    for (auto& n : world.significant_npcs) {
        npc_index[n.id] = &n;
    }

    for (const auto& d : deltas) {
        auto it = npc_index.find(d.npc_id);
        if (it == npc_index.end())
            continue;
        NPC* npc = it->second;

        // capital_delta: additive. Floor at 0; NO arbitrary upper wealth cap —
        // personal wealth is bounded by what the economy produces, not a magic
        // number (real fortunes have no ceiling). npc_capital_ceiling is used only
        // as a crash sentinel for non-finite values (a genuine bug guard).
        if (d.capital_delta.has_value()) {
            npc->capital = safe_add(npc->capital, *d.capital_delta);
            if (std::isinf(npc->capital) || std::isnan(npc->capital)) {
                npc->capital = ceil.npc_capital_ceiling;
            }
            npc->capital = std::max(0.0f, npc->capital);
        }

        // new_status: replacement
        if (d.new_status.has_value()) {
            npc->status = *d.new_status;
        }

        // new_occupation: replacement (livelihood assignment, commons regimes)
        if (d.new_occupation.has_value()) {
            npc->occupation = *d.new_occupation;
        }

        // age_delta: additive (years); clamped non-negative
        if (d.age_delta.has_value()) {
            npc->age_years = std::max(0.0f, npc->age_years + *d.age_delta);
        }

        // risk_tolerance_delta: additive; clamped to [0,1]
        if (d.risk_tolerance_delta.has_value()) {
            npc->risk_tolerance = clamp01(npc->risk_tolerance + *d.risk_tolerance_delta);
        }

        // investigator_meter: additive fill (clamped) + target assignment.
        if (d.investigator_meter_target.has_value()) {
            auto& m = npc->investigator_meter;
            if (m.target_npc_id != *d.investigator_meter_target) {
                m.target_npc_id = *d.investigator_meter_target;
                if (m.opened_tick == 0 && m.current_level == 0.0f)
                    m.opened_tick = world.current_tick;
                m.case_escalated = false;  // new target -> fresh case window
            }
        }
        if (d.investigator_meter_fill_delta.has_value()) {
            auto& m = npc->investigator_meter;
            if (m.opened_tick == 0)
                m.opened_tick = world.current_tick;
            m.current_level = clamp01(m.current_level + *d.investigator_meter_fill_delta);
        }

        // new_travel_status: replacement
        if (d.new_travel_status.has_value()) {
            npc->travel_status = *d.new_travel_status;
        }

        // new_memory_entry: append (with overflow protection)
        if (d.new_memory_entry.has_value()) {
            if (npc->memory_log.size() >= MAX_MEMORY_ENTRIES) {
                // Evict lowest-decay entry
                auto weakest = std::min_element(
                    npc->memory_log.begin(), npc->memory_log.end(),
                    [](const MemoryEntry& a, const MemoryEntry& b) { return a.decay < b.decay; });
                if (weakest != npc->memory_log.end()) {
                    npc->memory_log.erase(weakest);
                }
            }
            npc->memory_log.push_back(*d.new_memory_entry);
        }

        // updated_relationship: upsert by target_npc_id.
        // trust/fear/obligation_balance are DELTAS merged additively into an
        // existing relationship; on insert the struct provides initial values.
        if (d.updated_relationship.has_value()) {
            const auto& rel = *d.updated_relationship;
            bool found = false;
            for (auto& existing : npc->relationships) {
                if (existing.target_npc_id == rel.target_npc_id) {
                    // Update: merge trust/fear/obligation additively, clamp
                    existing.trust = clamp_neg1_1(existing.trust + rel.trust);
                    existing.fear = clamp01(existing.fear + rel.fear);
                    existing.obligation_balance =
                        clamp_neg1_1(existing.obligation_balance + rel.obligation_balance);
                    existing.last_interaction_tick = rel.last_interaction_tick;
                    if (rel.is_movement_ally)
                        existing.is_movement_ally = true;
                    // recovery_ceiling: ratchet-down merge (§13 floor principle).
                    // A delta may lower the ceiling (floored at the 0.15 minimum)
                    // but never raise it; senders use 1.0 for "no change".
                    if (rel.recovery_ceiling < existing.recovery_ceiling) {
                        existing.recovery_ceiling = std::max(rel.recovery_ceiling, 0.15f);
                    }
                    // Enforce recovery ceiling
                    if (existing.trust > existing.recovery_ceiling) {
                        existing.trust = existing.recovery_ceiling;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Insert new relationship
                Relationship new_rel = rel;
                new_rel.trust = clamp_neg1_1(new_rel.trust);
                new_rel.fear = clamp01(new_rel.fear);
                if (new_rel.recovery_ceiling < 0.15f)
                    new_rel.recovery_ceiling = 0.15f;
                npc->relationships.push_back(new_rel);
            }
        }

        // motivation_replacement: full vector override (preferred path).
        // Replaces the entire motivation vector. Invariant: must sum to 1.0.
        if (d.motivation_replacement.has_value()) {
            npc->motivations = *d.motivation_replacement;
        } else if (d.motivation_delta.has_value() && !std::isnan(*d.motivation_delta)) {
            // Legacy: additive shift to financial_gain slot, then renormalize.
            npc->motivations.weights[0] += *d.motivation_delta;
            // Renormalize to sum to 1.0
            float sum = 0.0f;
            for (float w : npc->motivations.weights)
                sum += std::max(0.0f, w);
            if (sum > 0.0f) {
                for (float& w : npc->motivations.weights) {
                    w = std::max(0.0f, w) / sum;
                }
            }
        }

        // set_addiction_state: full AddictionState replacement. Used by
        // AddictionModule each tick to persist stage/craving/tolerance
        // changes, and by drug_economy (or similar) to seed an NPC into
        // the state machine. Clamping the floats keeps tolerance/craving
        // in [0, 1] even if the delta wrote out-of-range values.
        if (d.set_addiction_state.has_value()) {
            npc->addiction_state = *d.set_addiction_state;
            npc->addiction_state.tolerance = clamp01(npc->addiction_state.tolerance);
            npc->addiction_state.craving = clamp01(npc->addiction_state.craving);
            npc->addiction_state.relapse_probability =
                clamp01(npc->addiction_state.relapse_probability);
        }
    }
}

// ---------------------------------------------------------------------------
// apply_player_delta
// ---------------------------------------------------------------------------
static void apply_player_delta(WorldState& world, const PlayerDelta& d) {
    if (!world.player)
        return;
    PlayerCharacter& p = *world.player;

    if (d.health_delta.has_value()) {
        p.health.current_health = clamp01(safe_add(p.health.current_health, *d.health_delta));
    }
    if (d.wealth_delta.has_value()) {
        p.wealth = safe_add(p.wealth, *d.wealth_delta);
    }
    if (d.reputation_business_delta.has_value()) {
        p.reputation.public_business =
            std::clamp(p.reputation.public_business + *d.reputation_business_delta, -1.0f, 1.0f);
    }
    if (d.reputation_political_delta.has_value()) {
        p.reputation.public_political =
            std::clamp(p.reputation.public_political + *d.reputation_political_delta, -1.0f, 1.0f);
    }
    if (d.reputation_social_delta.has_value()) {
        p.reputation.public_social =
            std::clamp(p.reputation.public_social + *d.reputation_social_delta, -1.0f, 1.0f);
    }
    if (d.exhaustion_delta.has_value()) {
        p.health.exhaustion_accumulator =
            clamp01(safe_add(p.health.exhaustion_accumulator, *d.exhaustion_delta));
    }
    if (d.skill_delta.has_value()) {
        const auto& sd = *d.skill_delta;
        for (auto& skill : p.skills) {
            if (skill.domain == static_cast<SkillDomain>(sd.skill_id)) {
                skill.level = std::clamp(safe_add(skill.level, sd.value), SKILL_DOMAIN_FLOOR, 1.0f);
                break;
            }
        }
    }
    if (d.new_evidence_awareness.has_value()) {
        EvidenceAwarenessEntry entry{};
        entry.token_id = *d.new_evidence_awareness;
        entry.discovery_tick = world.current_tick;
        entry.source_npc_id = 0;
        p.evidence_awareness_map.push_back(entry);
    }
    if (d.relationship_delta.has_value()) {
        const auto& rd = *d.relationship_delta;
        bool found = false;
        for (auto& rel : p.relationships) {
            if (rel.target_npc_id == rd.target_npc_id) {
                rel.trust = clamp_neg1_1(rel.trust + rd.trust_delta);
                if (rel.trust > rel.recovery_ceiling)
                    rel.trust = rel.recovery_ceiling;
                found = true;
                break;
            }
        }
        if (!found) {
            Relationship new_rel{};
            new_rel.target_npc_id = rd.target_npc_id;
            new_rel.trust = clamp_neg1_1(rd.trust_delta);
            new_rel.fear = 0.0f;
            new_rel.obligation_balance = 0.0f;
            new_rel.last_interaction_tick = world.current_tick;
            new_rel.is_movement_ally = false;
            new_rel.recovery_ceiling = 1.0f;
            p.relationships.push_back(new_rel);
        }
    }
    // Player location updates (replacement fields from travel system).
    if (d.new_province_id.has_value()) {
        p.current_province_id = *d.new_province_id;
    }
    if (d.new_travel_status.has_value()) {
        p.travel_status = *d.new_travel_status;
    }
}

// ---------------------------------------------------------------------------
// apply_business_deltas
// ---------------------------------------------------------------------------
static void apply_business_deltas(WorldState& world, const std::vector<BusinessDelta>& deltas,
                                  const SafetyCeilingsConfig& ceil) {
    if (deltas.empty())
        return;

    // Build index for O(1) business lookup.
    std::unordered_map<uint32_t, NPCBusiness*> biz_index;
    biz_index.reserve(world.npc_businesses.size());
    for (auto& b : world.npc_businesses) {
        biz_index[b.id] = &b;
    }

    for (const auto& d : deltas) {
        auto it = biz_index.find(d.business_id);
        if (it == biz_index.end())
            continue;
        auto& biz = *it->second;

        if (d.cash_delta.has_value()) {
            biz.cash = safe_add(biz.cash, *d.cash_delta);
            // Crash sentinel ONLY for non-finite values; no arbitrary cap on
            // business wealth (it is bounded by the economy, not a magic number).
            if (std::isinf(biz.cash) || std::isnan(biz.cash)) {
                biz.cash = (biz.cash > 0.0f || std::isnan(biz.cash)) ? ceil.business_cash_ceiling
                                                                     : -ceil.business_cash_ceiling;
            }
        }
        if (d.revenue_per_tick_update.has_value()) {
            biz.revenue_per_tick =
                std::clamp(*d.revenue_per_tick_update, 0.0f, ceil.business_revenue_ceiling);
        }
        if (d.cost_per_tick_update.has_value()) {
            biz.cost_per_tick = std::max(0.0f, *d.cost_per_tick_update);
        }
        if (d.output_quality_update.has_value()) {
            biz.output_quality = clamp01(*d.output_quality_update);
        }
        if (d.owner_id_update.has_value()) {
            biz.owner_id = *d.owner_id_update;
        }
        if (d.next_decision_tick_update.has_value()) {
            biz.strategic_decision_tick = *d.next_decision_tick_update;
        }
        if (d.net_signal_update.has_value()) {
            biz.net_signal = clamp01(*d.net_signal_update);
        }
    }
}

// ---------------------------------------------------------------------------
// apply_market_deltas
// ---------------------------------------------------------------------------
static void apply_market_deltas(WorldState& world, const std::vector<MarketDelta>& deltas,
                                const SafetyCeilingsConfig& ceil) {
    if (deltas.empty())
        return;

    // Build index: (good_id, province_id) -> RegionalMarket* for O(1) lookup.
    // Composite key packed into uint64_t: upper 32 = good_id, lower 32 = province_id.
    std::unordered_map<uint64_t, RegionalMarket*> market_index;
    market_index.reserve(world.regional_markets.size());
    for (auto& m : world.regional_markets) {
        uint64_t key = (static_cast<uint64_t>(m.good_id) << 32) | m.province_id;
        market_index[key] = &m;
    }

    for (const auto& d : deltas) {
        uint64_t key = (static_cast<uint64_t>(d.good_id) << 32) | d.region_id;
        auto it = market_index.find(key);
        if (it == market_index.end())
            continue;
        auto& m = *it->second;

        if (d.supply_delta.has_value()) {
            m.supply =
                std::clamp(safe_add(m.supply, *d.supply_delta), 0.0f, ceil.market_supply_ceiling);
        }
        if (d.demand_buffer_delta.has_value()) {
            m.demand_buffer = std::clamp(safe_add(m.demand_buffer, *d.demand_buffer_delta), 0.0f,
                                         ceil.market_supply_ceiling);
        }
        if (d.spot_price_override.has_value()) {
            float price = *d.spot_price_override;
            m.spot_price = std::clamp(std::isnan(price) ? m.spot_price : price, 0.001f,
                                      ceil.market_price_ceiling);
        }
        if (d.equilibrium_price_override.has_value()) {
            float price = *d.equilibrium_price_override;
            m.equilibrium_price = std::clamp(std::isnan(price) ? m.equilibrium_price : price,
                                             0.001f, ceil.market_price_ceiling);
        }
    }
}

// ---------------------------------------------------------------------------
// apply_evidence_deltas
// ---------------------------------------------------------------------------
static void apply_evidence_deltas(WorldState& world, const std::vector<EvidenceDelta>& deltas,
                                  uint32_t evidence_decay_interval) {
    if (deltas.empty())
        return;

    // Reserve pool capacity up-front so push_back doesn't invalidate the
    // EvidenceToken* pointers cached in by_id.
    size_t new_token_count = 0;
    for (const auto& d : deltas) {
        if (d.new_token.has_value())
            ++new_token_count;
    }
    if (new_token_count > 0) {
        world.evidence_pool.reserve(world.evidence_pool.size() + new_token_count);
    }

    // Build index for O(1) token lookup, plus running max_id cursor so the
    // id == 0 fallback path doesn't rescan the pool per insertion.
    std::unordered_map<uint32_t, EvidenceToken*> by_id;
    by_id.reserve(world.evidence_pool.size() + new_token_count);
    uint32_t max_id = 0;
    for (auto& t : world.evidence_pool) {
        by_id[t.id] = &t;
        if (t.id > max_id)
            max_id = t.id;
    }

    for (const auto& d : deltas) {
        if (d.new_token.has_value()) {
            EvidenceToken token = *d.new_token;
            if (token.id == 0) {
                token.id = ++max_id;
            } else if (token.id > max_id) {
                max_id = token.id;
            }
            world.evidence_pool.push_back(token);
            by_id[token.id] = &world.evidence_pool.back();
            // Schedule initial decay. Handler self-reschedules while token stays active.
            if (token.decay_rate > 0.0f) {
                world.deferred_work_queue.push({world.current_tick + evidence_decay_interval,
                                                WorkType::evidence_decay_batch, token.id,
                                                EvidenceDecayPayload{token.id}});
            }
        }
        if (d.retired_token_id.has_value()) {
            auto it = by_id.find(*d.retired_token_id);
            if (it != by_id.end()) {
                it->second->is_active = false;
            }
        }
        if (d.updated_token_id.has_value() && d.updated_actionability.has_value()) {
            auto it = by_id.find(*d.updated_token_id);
            if (it != by_id.end()) {
                it->second->actionability = *d.updated_actionability;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_cohort_stats_deltas — full replacement of a province's cohort set and
// derived aggregates (sole writer: population_aging). region_id indexes
// world.provinces directly (province-parallel emit).
// ---------------------------------------------------------------------------
static void apply_cohort_stats_deltas(WorldState& world,
                                      const std::vector<CohortStatsDelta>& deltas) {
    for (const auto& d : deltas) {
        if (d.region_id >= world.provinces.size())
            continue;
        auto& prov = world.provinces[d.region_id];
        if (!prov.cohort_stats)
            continue;
        prov.cohort_stats->cohorts = d.cohorts;
        prov.cohort_stats->total_population = d.total_population;
        prov.cohort_stats->mean_income = d.mean_income;
        prov.cohort_stats->gini_coefficient = std::clamp(d.gini_coefficient, 0.0f, 1.0f);
        prov.cohort_stats->hardiness = std::clamp(d.hardiness, 0.05f, 5.0f);
        // The town's size is a headcount of the people in it, not an estimate: derived
        // from the urban cohorts exactly as total_population is derived from all of
        // them. Migration and the urban graveyard move those cohorts; this only reads
        // them, so there is no second writer and nothing to drift out of step.
        uint64_t urban = 0;
        for (const auto& [g, c] : d.cohorts) {
            if (g == DemographicGroup::youth_urban || g == DemographicGroup::working_urban_low ||
                g == DemographicGroup::working_urban_mid ||
                g == DemographicGroup::working_urban_high || g == DemographicGroup::retiree_urban)
                urban += c.size;
        }
        prov.cohort_stats->urban_population = static_cast<float>(urban);
    }
}

// apply_nation_deltas
// ---------------------------------------------------------------------------
static void apply_nation_deltas(WorldState& world, const std::vector<NationDelta>& deltas) {
    if (deltas.empty())
        return;
    std::unordered_map<uint32_t, Nation*> by_id;
    by_id.reserve(world.nations.size());
    for (auto& n : world.nations)
        by_id[n.id] = &n;

    for (const auto& d : deltas) {
        auto it = by_id.find(d.nation_id);
        if (it == by_id.end())
            continue;
        Nation& n = *it->second;
        if (d.legitimacy_update.has_value())
            n.political_cycle.national_legitimacy = clamp01(*d.legitimacy_update);
        if (d.approval_delta.has_value())
            n.political_cycle.national_approval =
                clamp01(n.political_cycle.national_approval + *d.approval_delta);
        if (d.government_type_update.has_value())
            n.government_type = static_cast<GovernmentType>(*d.government_type_update);
    }
}

// ---------------------------------------------------------------------------
// apply_deposit_deltas — deplete finite resource deposits from extraction.
// ---------------------------------------------------------------------------
static void apply_deposit_deltas(WorldState& world, const std::vector<DepositDelta>& deltas) {
    if (deltas.empty())
        return;
    for (const auto& d : deltas) {
        if (d.province_id >= world.provinces.size())
            continue;
        for (auto& dep : world.provinces[d.province_id].deposits) {
            if (dep.id == d.deposit_id) {
                dep.quantity_remaining =
                    std::max(0.0f, dep.quantity_remaining - d.quantity_extracted);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_fisheries_deltas — update province fish stock (Schaefer dynamics).
// ---------------------------------------------------------------------------
static void apply_fisheries_deltas(WorldState& world, const std::vector<FisheriesDelta>& deltas) {
    if (deltas.empty())
        return;
    for (const auto& d : deltas) {
        if (d.province_id >= world.provinces.size())
            continue;
        auto& fish = world.provinces[d.province_id].fisheries;
        fish.current_stock =
            std::clamp(safe_add(fish.current_stock, d.stock_delta), 0.0f, fish.carrying_capacity);
    }
}

// ---------------------------------------------------------------------------
// apply_region_deltas
// ---------------------------------------------------------------------------
static void apply_region_deltas(WorldState& world, const std::vector<RegionDelta>& deltas) {
    if (deltas.empty())
        return;

    // Build region_id -> Province* bucket. One region can map to multiple
    // provinces; bucket vectors are populated in world.provinces order so
    // iterating a bucket reproduces the original nested-loop iteration order
    // (determinism).
    std::unordered_map<uint32_t, std::vector<Province*>> by_region;
    for (auto& prov : world.provinces) {
        by_region[prov.region_id].push_back(&prov);
    }

    for (const auto& d : deltas) {
        auto bucket = by_region.find(d.region_id);
        if (bucket == by_region.end())
            continue;
        for (Province* prov_ptr : bucket->second) {
            Province& prov = *prov_ptr;
            {
                auto& c = prov.conditions;
                if (d.stability_delta.has_value()) {
                    c.stability_score = clamp01(safe_add(c.stability_score, *d.stability_delta));
                }
                if (d.inequality_delta.has_value()) {
                    c.inequality_index = clamp01(safe_add(c.inequality_index, *d.inequality_delta));
                }
                if (d.regulatory_compliance_delta.has_value()) {
                    c.regulatory_compliance_index = clamp01(
                        safe_add(c.regulatory_compliance_index, *d.regulatory_compliance_delta));
                }
                if (d.drought_modifier_delta.has_value()) {
                    c.drought_modifier =
                        clamp01(safe_add(c.drought_modifier, *d.drought_modifier_delta));
                }
                if (d.flood_modifier_delta.has_value()) {
                    c.flood_modifier = clamp01(safe_add(c.flood_modifier, *d.flood_modifier_delta));
                }

                // Population-fraction monitors live on cohort_stats since the
                // schema-v5 consolidation. Initialise lazily if missing.
                if (!prov.cohort_stats) {
                    prov.cohort_stats = std::make_unique<RegionCohortStats>();
                }
                auto& cs = *prov.cohort_stats;
                if (d.crime_rate_delta.has_value()) {
                    cs.crime_rate = clamp01(safe_add(cs.crime_rate, *d.crime_rate_delta));
                }
                if (d.addiction_rate_delta.has_value()) {
                    cs.addiction_rate =
                        clamp01(safe_add(cs.addiction_rate, *d.addiction_rate_delta));
                }
                if (d.criminal_dominance_delta.has_value()) {
                    cs.criminal_dominance_index =
                        clamp01(safe_add(cs.criminal_dominance_index, *d.criminal_dominance_delta));
                }
                if (d.formal_employment_rate_delta.has_value()) {
                    cs.formal_employment_rate = clamp01(
                        safe_add(cs.formal_employment_rate, *d.formal_employment_rate_delta));
                }
                if (d.sick_rate_delta.has_value()) {
                    cs.sick_rate = clamp01(safe_add(cs.sick_rate, *d.sick_rate_delta));
                }
                if (d.homeless_rate_delta.has_value()) {
                    cs.homeless_rate = clamp01(safe_add(cs.homeless_rate, *d.homeless_rate_delta));
                }
                if (d.unemployment_rate_delta.has_value()) {
                    cs.unemployment_rate =
                        clamp01(safe_add(cs.unemployment_rate, *d.unemployment_rate_delta));
                }
                if (d.subsistence_surplus_replacement.has_value()) {
                    // Replacement; recomputed each tick by the subsistence module.
                    // Clamp to a sane non-negative range (0 = total famine).
                    float v = *d.subsistence_surplus_replacement;
                    if (!(v >= 0.0f))
                        v = 0.0f;
                    if (v > 10.0f)
                        v = 10.0f;
                    cs.subsistence_surplus_ratio = v;
                }
                if (d.specialist_fraction_replacement.has_value()) {
                    // Replacement; recomputed each tick by subsistence from the
                    // population the harvest does NOT need on the land. A share of
                    // real people, so it lives in [0, 1] by construction.
                    float v = *d.specialist_fraction_replacement;
                    if (!(v >= 0.0f))
                        v = 0.0f;
                    if (v > 1.0f)
                        v = 1.0f;
                    cs.specialist_fraction = v;
                }
                if (d.codified_knowledge_delta.has_value()) {
                    // Additive. Floored at 0 — a corpus cannot be less than nothing.
                    // No upper bound: what a society writes down is limited by what it
                    // knows and how many scribes it can feed, not by a cap here.
                    cs.codified_knowledge =
                        std::max(0.0f, safe_add(cs.codified_knowledge, *d.codified_knowledge_delta));
                }
                if (d.soil_health_delta.has_value()) {
                    // Additive. Bounded to [0, 1] by DEFINITION — it is a fraction of
                    // pristine fertility, not a tuning band: land cannot be more than
                    // untouched or less than dead.
                    float v = safe_add(cs.soil_health, *d.soil_health_delta);
                    if (!(v >= 0.0f))
                        v = 0.0f;
                    if (v > 1.0f)
                        v = 1.0f;
                    cs.soil_health = v;
                }
                if (d.productive_capital_delta.has_value()) {
                    // Additive: investment out of surplus minus the year's wear. A
                    // real stock, so it cannot go negative (you cannot un-build past
                    // nothing); the floor is physical, not a behaviour cap.
                    cs.productive_capital =
                        std::max(0.0f, safe_add(cs.productive_capital, *d.productive_capital_delta));
                }
                if (d.food_store_replacement.has_value()) {
                    // Replacement; the subsistence module folds the year's net food
                    // into the granary and writes the new (clamped) stock here.
                    float v = *d.food_store_replacement;
                    cs.food_store = (v >= 0.0f) ? v : 0.0f;
                }
                if (d.territorial_conflict_stage_replacement.has_value()) {
                    // Replacement; criminal_operations recomputes the per-province
                    // conflict intensity each tick from org conflict_state.
                    cs.territorial_conflict_stage =
                        std::min(*d.territorial_conflict_stage_replacement, static_cast<uint8_t>(6));
                }
                if (d.grain_surplus_replacement.has_value()) {
                    // Replacement; subsistence recomputes the haulable surplus each tick.
                    float v = *d.grain_surplus_replacement;
                    cs.grain_surplus = (v >= 0.0f) ? v : 0.0f;
                }
                if (d.net_feedable_surplus_replacement.has_value()) {
                    // Replacement; grain_logistics recomputes the catchment surplus each tick.
                    float v = *d.net_feedable_surplus_replacement;
                    cs.net_feedable_surplus = (v >= 0.0f) ? v : 0.0f;
                }
                if (d.urban_capacity_replacement.has_value()) {
                    // Replacement; grain_logistics recomputes what the catchment could
                    // feed each tick. The town's actual size is not set here — it is the
                    // urban cohort headcount, moved by migration and mortality.
                    float v = *d.urban_capacity_replacement;
                    cs.urban_capacity = (v >= 0.0f) ? v : 0.0f;
                }
                if (d.plague_susceptible_replacement.has_value()) {
                    // A share of the population: physically in [0,1]. NaN -> fully
                    // susceptible, which is the safe default for a stock nobody has
                    // touched (it is what a fresh world starts with).
                    float v = *d.plague_susceptible_replacement;
                    cs.plague_susceptible_fraction = std::isnan(v) ? 1.0f : std::clamp(v, 0.0f, 1.0f);
                }
                if (d.supported_specialist_fraction_replacement.has_value()) {
                    float v = *d.supported_specialist_fraction_replacement;
                    cs.supported_specialist_fraction = std::isnan(v) ? 0.0f : std::clamp(v, 0.0f, 1.0f);
                }
                if (d.political_stress_replacement.has_value()) {
                    // Replacement; structural_demography recomputes the PSI each year.
                    // Uncapped above by design — the death rate it drives arrives as
                    // 1 - exp(-rate), which is where the bound physically belongs.
                    float v = *d.political_stress_replacement;
                    cs.political_stress = (std::isnan(v) || v < 0.0f) ? 0.0f : v;
                }
                if (d.faction_death_fraction_replacement.has_value()) {
                    // A fraction of people: you cannot lose more than everyone. NaN -> 0.
                    float v = *d.faction_death_fraction_replacement;
                    cs.faction_death_fraction = std::isnan(v) ? 0.0f : std::clamp(v, 0.0f, 1.0f);
                }
                if (d.ghost_land_fraction_replacement.has_value()) {
                    // Replacement; energy_base recomputes the year's coal burn each tick.
                    float v = *d.ghost_land_fraction_replacement;
                    cs.ghost_land_fraction = (v >= 0.0f) ? v : 0.0f;
                }
                if (d.coal_burned_replacement.has_value()) {
                    float v = *d.coal_burned_replacement;
                    cs.coal_burned_per_year = (v >= 0.0f) ? v : 0.0f;
                }
                if (d.war_death_fraction_replacement.has_value()) {
                    // Replacement; warfare recomputes annually. Physical bounds: an
                    // extra death fraction lies in [0, 1] (you cannot lose more than
                    // everyone). NaN falls to 0.
                    float v = *d.war_death_fraction_replacement;
                    cs.war_death_fraction = std::isnan(v) ? 0.0f : std::clamp(v, 0.0f, 1.0f);
                }
                if (d.food_store_delta.has_value()) {
                    // Additive conserved grain flow (war rations/plunder/burn,
                    // redistribution). Physical floor: a granary cannot go negative;
                    // capacity is re-enforced by subsistence banking next tick.
                    float dv = *d.food_store_delta;
                    if (!std::isnan(dv))
                        cs.food_store = std::max(0.0f, cs.food_store + dv);
                }
                if (d.cohesion_delta.has_value()) {
                    prov.community.cohesion =
                        clamp01(safe_add(prov.community.cohesion, *d.cohesion_delta));
                }
                if (d.grievance_delta.has_value()) {
                    prov.community.grievance_level =
                        clamp01(safe_add(prov.community.grievance_level, *d.grievance_delta));
                }
                if (d.institutional_trust_delta.has_value()) {
                    prov.community.institutional_trust = clamp01(
                        safe_add(prov.community.institutional_trust, *d.institutional_trust_delta));
                }
                if (d.resource_access_delta.has_value()) {
                    prov.community.resource_access =
                        clamp01(safe_add(prov.community.resource_access, *d.resource_access_delta));
                }
                if (d.response_stage_replacement.has_value()) {
                    prov.community.response_stage =
                        std::min(*d.response_stage_replacement, static_cast<uint8_t>(6));
                }
                if (d.infrastructure_rating_delta.has_value()) {
                    prov.infrastructure_rating = clamp01(
                        safe_add(prov.infrastructure_rating, *d.infrastructure_rating_delta));
                }
                if (d.avg_property_value_update.has_value()) {
                    float v = *d.avg_property_value_update;
                    prov.avg_property_value = std::isfinite(v) ? std::max(0.0f, v) : 0.0f;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_currency_deltas
// ---------------------------------------------------------------------------
static void apply_currency_deltas(WorldState& world, const std::vector<CurrencyDelta>& deltas) {
    if (deltas.empty())
        return;

    std::unordered_map<uint32_t, CurrencyRecord*> by_nation;
    by_nation.reserve(world.currencies.size());
    for (auto& cur : world.currencies) {
        by_nation[cur.nation_id] = &cur;
    }

    for (const auto& d : deltas) {
        auto it = by_nation.find(d.nation_id);
        if (it == by_nation.end())
            continue;
        CurrencyRecord& cur = *it->second;
        if (d.usd_rate_update.has_value()) {
            const float requested = *d.usd_rate_update;
            // std::max with NaN returns NaN — guard explicitly. Matches
            // safe_add()'s NaN policy: drop the delta, keep the previous
            // value. Inf is also rejected since clamping it would yield
            // either +Inf or the floor depending on sign.
            if (!std::isnan(requested) && !std::isinf(requested)) {
                cur.usd_rate = std::max(0.001f, requested);
            }
        }
        if (d.pegged_update.has_value()) {
            cur.pegged = *d.pegged_update;
        }
        if (d.foreign_reserves_delta.has_value()) {
            cur.foreign_reserves =
                clamp01(safe_add(cur.foreign_reserves, *d.foreign_reserves_delta));
        }
    }
}

// ---------------------------------------------------------------------------
// apply_append_deltas — calendar entries, scene cards, obligations
// ---------------------------------------------------------------------------
static void apply_append_deltas(WorldState& world, DeltaBuffer& delta) {
    for (auto& entry : delta.new_calendar_entries) {
        world.calendar.push_back(std::move(entry));
    }
    for (auto& card : delta.new_scene_cards) {
        world.pending_scene_cards.push_back(std::move(card));
    }
    for (auto& node : delta.new_obligation_nodes) {
        if (node.id == 0) {
            uint32_t max_id = 0;
            for (const auto& n : world.obligation_network) {
                if (n.id > max_id)
                    max_id = n.id;
            }
            node.id = max_id + 1;
        }
        world.obligation_network.push_back(std::move(node));
    }
}

// ---------------------------------------------------------------------------
// apply_lod2_price_deltas
// ---------------------------------------------------------------------------
// LOD 2 emits raw_modifier values per good_id; this routine applies lerp
// smoothing against the existing stored modifier (default 1.0 when the good
// is new) using LodSystemConfig::lod2_smoothing_rate. The smoothing rate is
// read from the per-tick config when supplied; otherwise the LodSystemConfig
// default (0.30) is used.
static void apply_lod2_price_deltas(WorldState& world,
                                    const std::vector<Lod2PriceIndexDelta>& deltas,
                                    float smoothing_rate) {
    if (deltas.empty())
        return;
    if (!world.lod2_price_index)
        world.lod2_price_index = std::make_unique<GlobalCommodityPriceIndex>();

    auto& index = *world.lod2_price_index;
    for (const auto& d : deltas) {
        if (std::isnan(d.raw_modifier) || std::isinf(d.raw_modifier))
            continue;  // reject pathological inputs
        auto it = index.lod2_price_modifier.find(d.good_id);
        const float old_modifier = (it == index.lod2_price_modifier.end()) ? 1.0f : it->second;
        const float smoothed = old_modifier + smoothing_rate * (d.raw_modifier - old_modifier);
        index.lod2_price_modifier[d.good_id] = smoothed;
    }
    index.last_updated_tick = world.current_tick;
}

// ---------------------------------------------------------------------------
// apply_technology_deltas
// ---------------------------------------------------------------------------
static void apply_technology_deltas(WorldState& world, const std::vector<TechnologyDelta>& deltas) {
    for (const auto& td : deltas) {
        // Era transition. NOT forward-only any more: a society that can no longer carry
        // what it knew loses the era (knowledge_module publishes the regression). Rome
        // fell; the Maya cities emptied. Advancement is a sawtooth with a ratchet, not
        // a ramp, so this applies a change in either direction — but only a change of
        // one step, which is all either path ever publishes.
        if (td.new_era.has_value()) {
            const uint8_t target = *td.new_era;
            const uint8_t current = world.technology.current_era;
            if (target != current && target >= 1) {
                world.technology.current_era = target;
                world.technology.era_started_tick = world.current_tick;
            }
        }
        // Knowledge accumulation (additive; floored at 0).
        if (td.knowledge_delta.has_value()) {
            world.technology.knowledge_level =
                std::max(0.0f, world.technology.knowledge_level + *td.knowledge_delta);
        }

        // Domain knowledge decay/adjustment (additive, clamped 0.0–1.0).
        if (td.domain_index.has_value() && td.domain_knowledge_delta.has_value()) {
            uint8_t idx = *td.domain_index;
            if (idx < RESEARCH_DOMAIN_COUNT) {
                float val = world.technology.domain_knowledge[idx];
                val = safe_add(val, *td.domain_knowledge_delta);
                world.technology.domain_knowledge[idx] = std::clamp(val, 0.0f, 1.0f);
            }
        }

        // Per-business maturation level update (replacement).
        if (td.business_id.has_value() && td.node_key.has_value() &&
            td.maturation_level_update.has_value()) {
            for (auto& biz : world.npc_businesses) {
                if (biz.id == *td.business_id) {
                    auto it = biz.actor_tech_state.holdings.find(*td.node_key);
                    if (it != biz.actor_tech_state.holdings.end()) {
                        it->second.maturation_level =
                            std::min(*td.maturation_level_update, it->second.maturation_ceiling);
                    }
                    break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_dissolved_businesses
// ---------------------------------------------------------------------------
static void apply_dissolved_businesses(WorldState& world,
                                       const std::vector<DissolvedBusinessDelta>& deltas) {
    if (deltas.empty())
        return;

    // Single remove_if pass over the dissolved-id set, instead of one pass per
    // delta.
    std::unordered_set<uint32_t> dissolved_ids;
    dissolved_ids.reserve(deltas.size());
    for (const auto& d : deltas) {
        dissolved_ids.insert(d.business_id);
    }
    world.npc_businesses.erase(
        std::remove_if(world.npc_businesses.begin(), world.npc_businesses.end(),
                       [&](const NPCBusiness& b) { return dissolved_ids.count(b.id) != 0; }),
        world.npc_businesses.end());
}

// ---------------------------------------------------------------------------
// apply_new_businesses
// ---------------------------------------------------------------------------
static void apply_new_businesses(WorldState& world, const std::vector<NewBusinessDelta>& deltas) {
    for (const auto& d : deltas) {
        world.npc_businesses.push_back(d.new_business);
    }
}

// apply_new_facilities (Phase 11 construction delivery)
static void apply_new_facilities(WorldState& world, const std::vector<NewFacilityDelta>& deltas) {
    for (const auto& d : deltas) {
        world.facilities.push_back(d.new_facility);
    }
}

// ---------------------------------------------------------------------------
// apply_scene_card_choice_deltas
// ---------------------------------------------------------------------------
static void apply_scene_card_choice_deltas(WorldState& world,
                                           const std::vector<SceneCardChoiceDelta>& deltas) {
    for (const auto& d : deltas) {
        for (auto& card : world.pending_scene_cards) {
            if (card.id == d.scene_card_id) {
                card.chosen_choice_id = d.chosen_choice_id;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_calendar_commit_deltas
// ---------------------------------------------------------------------------
static void apply_calendar_commit_deltas(WorldState& world,
                                         const std::vector<CalendarCommitDelta>& deltas) {
    for (const auto& d : deltas) {
        for (auto& entry : world.calendar) {
            if (entry.id == d.calendar_entry_id) {
                entry.player_committed = d.committed;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// apply_deltas — main entry point
// ---------------------------------------------------------------------------
void apply_deltas(WorldState& world, DeltaBuffer& delta, const SafetyCeilingsConfig* ceilings,
                  const PackageConfig* config) {
    const auto& ceil = ceilings ? *ceilings : DEFAULT_CEILINGS;
    // Extract evidence_decay_interval from config; fall back to DrainConfig default (7).
    uint32_t evidence_decay_interval =
        (config && config->consequence_delays.evidence_decay_interval > 0)
            ? config->consequence_delays.evidence_decay_interval
            : 7u;
    apply_npc_deltas(world, delta.npc_deltas, ceil);
    apply_player_delta(world, delta.player_delta);
    apply_business_deltas(world, delta.business_deltas, ceil);
    apply_market_deltas(world, delta.market_deltas, ceil);
    apply_evidence_deltas(world, delta.evidence_deltas, evidence_decay_interval);
    apply_region_deltas(world, delta.region_deltas);
    apply_nation_deltas(world, delta.nation_deltas);
    apply_deposit_deltas(world, delta.deposit_deltas);
    apply_fisheries_deltas(world, delta.fisheries_deltas);
    apply_cohort_stats_deltas(world, delta.cohort_stats_deltas);

    // Consequence queue (GDD §21): schedule new entries and process cancellations.
    for (const auto& cd : delta.consequence_deltas) {
        if (cd.cancelled_entry_id.has_value()) {
            for (auto& e : world.consequence_queue) {
                if (e.id == *cd.cancelled_entry_id)
                    e.cancelled = true;
            }
        }
        if (cd.new_consequence.has_value()) {
            world.consequence_queue.push_back(*cd.new_consequence);
        }
    }
    apply_currency_deltas(world, delta.currency_deltas);
    apply_technology_deltas(world, delta.technology_deltas);
    const float lod2_smoothing_rate =
        config ? config->lod_system.lod2_smoothing_rate : LodSystemConfig{}.lod2_smoothing_rate;
    apply_lod2_price_deltas(world, delta.lod2_price_index_deltas, lod2_smoothing_rate);
    apply_dissolved_businesses(world, delta.dissolved_businesses);
    apply_new_businesses(world, delta.new_businesses);
    apply_new_facilities(world, delta.new_facilities);
    apply_append_deltas(world, delta);
    apply_scene_card_choice_deltas(world, delta.scene_card_choice_deltas);
    apply_calendar_commit_deltas(world, delta.calendar_commit_deltas);

    // Route cross-province deltas into WorldState's CrossProvinceDeltaBuffer
    // for application at the start of the next tick.
    for (auto& cpd : delta.cross_province_deltas) {
        world.cross_province_delta_buffer.entries.push_back(std::move(cpd));
    }

    // Route legal case seeds into WorldState's pending queue. legal_process
    // drains them at the start of its execute() within the same tick (it
    // runs at Tier 9, after investigator_engine at Tier 8 which is the
    // primary producer). The queue must be empty at save time.
    for (auto& seed : delta.new_legal_case_seeds) {
        world.pending_legal_case_seeds.push_back(std::move(seed));
    }

    // Route protection-racket seeds into WorldState's pending queue.
    // protection_rackets (Tier 8) drains them in init_for_tick within the
    // same tick (criminal_operations at Tier 7 is the producer). The queue
    // must be empty at save time.
    for (auto& seed : delta.new_racket_seeds) {
        world.pending_racket_seeds.push_back(std::move(seed));
    }

    // Route money-laundering seeds into WorldState's pending queue.
    // money_laundering (Tier 9) drains them at the start of its execute()
    // within the same tick (criminal_operations at Tier 7 is the producer).
    for (auto& seed : delta.new_laundering_seeds) {
        world.pending_laundering_seeds.push_back(std::move(seed));
    }

    // Route random event triggers into WorldState's pending queue.
    // random_events drains them at the start of its execute(). Unlike
    // legal_case_seeds this queue MAY persist across tick boundaries
    // because typical producers (currency_exchange at Tier 11) run after
    // the consumer (random_events at Tier 1), and is therefore persisted
    // by persistence schema v8+.
    for (auto& trig : delta.new_random_event_triggers) {
        world.pending_random_event_triggers.push_back(std::move(trig));
    }

    // Route property transaction requests into WorldState's pending
    // queue. real_estate drains them at the start of its execute()
    // within the same tick (real_estate Tier 4 follows player_actions
    // Tier 0). Queue is empty at save time.
    for (auto& tx : delta.new_property_transactions) {
        world.pending_property_transactions.push_back(std::move(tx));
    }

    // Route new loan requests (Phase 4 — mortgage origination) into
    // pending_loan_requests. Banking drains them at the start of its
    // execute() (Tier 5 follows real_estate Tier 4). Queue empty at
    // save time.
    for (auto& req : delta.new_loan_requests) {
        world.pending_loan_requests.push_back(std::move(req));
    }

    // Route foreclosure requests (Phase 5) into pending_property_foreclosures.
    // Cross-tick: banking (Tier 5) emits, real_estate (Tier 4) drains
    // next tick. Persisted by schema v11+.
    for (auto& fc : delta.new_property_foreclosures) {
        world.pending_property_foreclosures.push_back(std::move(fc));
    }

    // Route auction bid requests (Phase 6) into pending_auction_bid_requests.
    // Same-tick: player_actions (Tier 0) emits, real_estate (Tier 4) drains.
    for (auto& bid : delta.new_auction_bid_requests) {
        world.pending_auction_bid_requests.push_back(std::move(bid));
    }

    // Route zoning-change requests (Phase 7) into pending_zoning_requests.
    // Same-tick: player_actions (Tier 0) emits, real_estate (Tier 4) drains.
    for (auto& zr : delta.new_zoning_requests) {
        world.pending_zoning_requests.push_back(std::move(zr));
    }

    // Route subdivision/merge requests (Phase 8) into
    // pending_subdivision_requests. Same-tick consumer.
    for (auto& sr : delta.new_subdivision_requests) {
        world.pending_subdivision_requests.push_back(std::move(sr));
    }

    // Route business acquisition offers (Phase 10) into
    // pending_business_acquisition_requests. Same-tick consumer.
    for (auto& ba : delta.new_business_acquisitions) {
        world.pending_business_acquisition_requests.push_back(std::move(ba));
    }

    // Route construction requests/awards (Phase 11). Same-tick consumer.
    for (auto& cr : delta.new_construction_requests) {
        world.pending_construction_requests.push_back(std::move(cr));
    }
    for (auto& ca : delta.new_construction_awards) {
        world.pending_construction_awards.push_back(std::move(ca));
    }

    // Refresh the province → significant_npcs index. Most apply_deltas calls
    // touch NPCs (status, capital), and the worst-case full sweep is O(N), so
    // a conditional rebuild buys little. The orchestrator separately calls
    // rebuild_npc_indices when it knows the index is needed; doing it here too
    // keeps callers that invoke apply_deltas directly (drain, tests) honest.
    rebuild_npc_indices(world);

    // Clear the delta buffer for next step
    delta.npc_deltas.clear();
    delta.player_delta = PlayerDelta{};
    delta.market_deltas.clear();
    delta.evidence_deltas.clear();
    delta.consequence_deltas.clear();
    delta.business_deltas.clear();
    delta.region_deltas.clear();
    delta.currency_deltas.clear();
    delta.technology_deltas.clear();
    delta.new_calendar_entries.clear();
    delta.new_scene_cards.clear();
    delta.new_obligation_nodes.clear();
    delta.cross_province_deltas.clear();
    delta.dissolved_businesses.clear();
    delta.new_businesses.clear();
    delta.scene_card_choice_deltas.clear();
    delta.calendar_commit_deltas.clear();
    delta.new_legal_case_seeds.clear();
    delta.new_racket_seeds.clear();
    delta.new_laundering_seeds.clear();
    delta.cohort_stats_deltas.clear();
    delta.new_random_event_triggers.clear();
    delta.new_property_transactions.clear();
    delta.new_loan_requests.clear();
    delta.new_property_foreclosures.clear();
    delta.new_auction_bid_requests.clear();
    delta.new_zoning_requests.clear();
    delta.new_subdivision_requests.clear();
    delta.new_business_acquisitions.clear();
    delta.new_facilities.clear();
    delta.new_construction_requests.clear();
    delta.new_construction_awards.clear();
}

// ---------------------------------------------------------------------------
// apply_cross_province_deltas
// ---------------------------------------------------------------------------
void apply_cross_province_deltas(WorldState& world) {
    auto& cpd = world.cross_province_delta_buffer;

    // Partition: apply entries that are due, retain entries scheduled for later.
    std::vector<CrossProvinceDelta> pending;

    for (const auto& entry : cpd.entries) {
        if (entry.due_tick > world.current_tick) {
            pending.push_back(entry);
            continue;
        }

        if (entry.npc_delta.has_value()) {
            // Apply NPC delta to the target province's NPC
            std::vector<NPCDelta> single = {*entry.npc_delta};
            apply_npc_deltas(world, single, DEFAULT_CEILINGS);
        }
        if (entry.market_delta.has_value()) {
            std::vector<MarketDelta> single = {*entry.market_delta};
            apply_market_deltas(world, single, DEFAULT_CEILINGS);
        }
    }

    // Retain only entries not yet due.
    cpd.entries = std::move(pending);

    // Cross-province NPC deltas can change current_province_id; refresh the
    // bucket index so the first module of the new tick sees a consistent view.
    rebuild_npc_indices(world);
}

// ---------------------------------------------------------------------------
// lookup_good_id
// ---------------------------------------------------------------------------
uint32_t lookup_good_id(const WorldState& world, const std::string& good_id_str) {
    if (world.goods_catalog) {
        if (const GoodDefinition* def = world.goods_catalog->find(good_id_str)) {
            return def->numeric_id;
        }
        // Catalog is set but doesn't know this good — return 0 rather than
        // a hash that would silently route to a phantom market.
        return 0u;
    }
    // No catalog (typically a unit test). Fall back to the FNV-1a hash so
    // tests that built their markets with the same hash continue to work.
    return good_id_hash(good_id_str);
}

// ---------------------------------------------------------------------------
// lookup_market / markets_in_province
// ---------------------------------------------------------------------------
const RegionalMarket* lookup_market(const WorldState& world, uint32_t good_id,
                                    uint32_t province_id) {
    const uint64_t key = (static_cast<uint64_t>(good_id) << 32) | province_id;
    if (auto it = world.market_index_by_good_province.find(key);
        it != world.market_index_by_good_province.end()) {
        return &world.regional_markets[it->second];
    }
    if (!world.market_index_by_good_province.empty()) {
        return nullptr;  // index built; absence is real
    }
    for (const auto& m : world.regional_markets) {
        if (m.good_id == good_id && m.province_id == province_id) {
            return &m;
        }
    }
    return nullptr;
}

std::vector<uint32_t> markets_in_province(const WorldState& world, uint32_t province_id) {
    if (province_id < world.market_indices_by_province.size() &&
        !world.market_indices_by_province[province_id].empty()) {
        return world.market_indices_by_province[province_id];
    }
    // Fallback only when the index is empty AND there are markets — otherwise
    // an empty result is real.
    if (!world.market_indices_by_province.empty() && !world.regional_markets.empty()) {
        bool any_bucket_populated = false;
        for (const auto& bucket : world.market_indices_by_province) {
            if (!bucket.empty()) {
                any_bucket_populated = true;
                break;
            }
        }
        if (any_bucket_populated) {
            return {};  // index built; this province genuinely has no markets
        }
    }
    std::vector<uint32_t> out;
    for (size_t i = 0; i < world.regional_markets.size(); ++i) {
        if (world.regional_markets[i].province_id == province_id) {
            out.push_back(static_cast<uint32_t>(i));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// lookup_npc_by_id
// ---------------------------------------------------------------------------
const NPC* lookup_npc_by_id(const WorldState& world, uint32_t npc_id) {
    auto it = world.npc_index_by_id.find(npc_id);
    if (it != world.npc_index_by_id.end()) {
        return &world.significant_npcs[it->second];
    }
    // Index built and id not present — absence is real.
    if (!world.npc_index_by_id.empty()) {
        return nullptr;
    }
    // Index not built (typically a unit test that constructed WorldState
    // piecemeal). Fall back to a linear scan so the helper is safe to call
    // before rebuild_npc_indices() has run.
    for (const auto& npc : world.significant_npcs) {
        if (npc.id == npc_id) {
            return &npc;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// rebuild_npc_indices
// ---------------------------------------------------------------------------
void rebuild_npc_indices(WorldState& world) {
    // Rebuild the id → index map first; it is independent of province count
    // and used by callers that don't care about the province bucket.
    auto& by_id = world.npc_index_by_id;
    by_id.clear();
    by_id.reserve(world.significant_npcs.size());
    for (size_t i = 0; i < world.significant_npcs.size(); ++i) {
        by_id[world.significant_npcs[i].id] = static_cast<uint32_t>(i);
    }

    const size_t province_count = world.provinces.size();
    auto& buckets = world.npc_indices_by_province;
    buckets.assign(province_count, {});

    if (province_count == 0)
        return;

    // First pass: count to size each bucket exactly. Avoids growth churn at
    // the 2k-NPC scale where the per-bucket vectors might otherwise reallocate
    // a few times.
    std::vector<size_t> counts(province_count, 0);
    for (const auto& npc : world.significant_npcs) {
        if (npc.current_province_id < province_count) {
            ++counts[npc.current_province_id];
        }
    }
    for (size_t p = 0; p < province_count; ++p) {
        buckets[p].reserve(counts[p]);
    }

    // Second pass: populate. Iterating significant_npcs in vector order means
    // each bucket is filled in the same order as the underlying vector, so
    // bucket entries are id-ascending when significant_npcs is id-ascending
    // (the standard invariant after world generation and apply_deltas).
    for (size_t i = 0; i < world.significant_npcs.size(); ++i) {
        const auto& npc = world.significant_npcs[i];
        if (npc.current_province_id < province_count) {
            buckets[npc.current_province_id].push_back(static_cast<uint32_t>(i));
        }
        // NPCs with out-of-range province_id are skipped silently — they
        // would already be invisible to province-filtered scans today.
    }

    // home_province bucket: same shape as the current_province bucket but
    // keyed by home_province_id. Used by modules whose contract is
    // "residents of this province".
    auto& home_buckets = world.npc_indices_by_home_province;
    home_buckets.assign(province_count, {});
    std::vector<size_t> home_counts(province_count, 0);
    for (const auto& npc : world.significant_npcs) {
        if (npc.home_province_id < province_count) {
            ++home_counts[npc.home_province_id];
        }
    }
    for (size_t p = 0; p < province_count; ++p) {
        home_buckets[p].reserve(home_counts[p]);
    }
    for (size_t i = 0; i < world.significant_npcs.size(); ++i) {
        const auto& npc = world.significant_npcs[i];
        if (npc.home_province_id < province_count) {
            home_buckets[npc.home_province_id].push_back(static_cast<uint32_t>(i));
        }
    }

    // --- regional_markets indices ---
    //
    // Buckets and the (good_id, province_id) composite-key map are rebuilt
    // alongside the NPC indices. Markets do not migrate between provinces, so
    // these would be stable after world generation in practice — but era
    // transitions and mod hot-loads can append new markets, and the rebuild
    // cost is O(M) ≈ 1500 ops at V1 scale, negligible next to the rebuild's
    // existing NPC pass.
    auto& market_buckets = world.market_indices_by_province;
    market_buckets.assign(province_count, {});
    auto& market_kv = world.market_index_by_good_province;
    market_kv.clear();
    market_kv.reserve(world.regional_markets.size());

    if (province_count > 0) {
        std::vector<size_t> market_counts(province_count, 0);
        for (const auto& m : world.regional_markets) {
            if (m.province_id < province_count) {
                ++market_counts[m.province_id];
            }
        }
        for (size_t p = 0; p < province_count; ++p) {
            market_buckets[p].reserve(market_counts[p]);
        }
    }

    for (size_t i = 0; i < world.regional_markets.size(); ++i) {
        const auto& m = world.regional_markets[i];
        if (m.province_id < province_count) {
            market_buckets[m.province_id].push_back(static_cast<uint32_t>(i));
        }
        // The composite-key map keeps every market regardless of bucket
        // eligibility — the contract is "id → index", not "in some bucket".
        const uint64_t key = (static_cast<uint64_t>(m.good_id) << 32) | m.province_id;
        market_kv[key] = static_cast<uint32_t>(i);
    }
}

}  // namespace econlife
