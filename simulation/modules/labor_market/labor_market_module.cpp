// Labor Market Module — implementation.
// See labor_market_module.h for class declarations and
// docs/interfaces/labor_market/INTERFACE.md for the canonical specification.

#include "modules/labor_market/labor_market_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// LaborMarketModule — tick execution
// ===========================================================================

void LaborMarketModule::execute_province(uint32_t province_idx, const WorldState& state,
                                         DeltaBuffer& province_delta) {
    // Skip provinces not at full LOD.
    if (province_idx < state.provinces.size() &&
        state.provinces[province_idx].lod_level != SimulationLOD::full) {
        return;
    }

    // Fork RNG with province_id for deterministic province-parallel work.
    DeterministicRNG rng = DeterministicRNG(state.world_seed).fork(province_idx);

    // Step 1: Wage payments for all employed NPCs in this province.
    process_wage_payments(province_idx, state, province_delta);

    // Step 2: Process hiring decisions for active postings.
    process_hiring_decisions(province_idx, state, province_delta);

    // Step 3: Monthly voluntary departures (every 30 ticks).
    if (state.current_tick % LaborModuleConfig{}.monthly_tick_interval == 0) {
        process_voluntary_departures(province_idx, state, province_delta, rng);
    }

    // Step 4: Close expired postings.
    close_expired_postings(province_idx, state.current_tick);
}

void LaborMarketModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Global post-pass: called by the orchestrator after all province-parallel
    // execute_province() calls have been merged and applied.
    // Monthly wage update runs globally (not per-province).
    if (state.current_tick % LaborModuleConfig{}.monthly_tick_interval == 0) {
        update_regional_wages(state);
    }
}

// ===========================================================================
// LaborMarketModule — wage payments
// ===========================================================================

void LaborMarketModule::process_wage_payments(uint32_t province_id, const WorldState& state,
                                              DeltaBuffer& delta) {
    // Sort employment records by npc_id ascending for deterministic processing.
    // We iterate all records and filter to this province.
    // Collect indices first, then sort by npc_id.
    std::vector<std::size_t> province_records;
    for (std::size_t i = 0; i < employment_records_.size(); ++i) {
        const auto& rec = employment_records_[i];
        if (rec.employer_business_id == 0)
            continue;  // unemployed

        // Check if the NPC is in this province.
        const NPC* npc = find_npc(state, rec.npc_id);
        if (!npc)
            continue;
        if (npc->current_province_id != province_id)
            continue;
        if (npc->status != NPCStatus::active)
            continue;

        province_records.push_back(i);
    }

    // Sort by npc_id ascending for deterministic order.
    std::sort(province_records.begin(), province_records.end(),
              [this](std::size_t a, std::size_t b) {
                  return employment_records_[a].npc_id < employment_records_[b].npc_id;
              });

    for (std::size_t idx : province_records) {
        auto& rec = employment_records_[idx];

        // Find the employer business.
        const NPCBusiness* biz = find_business(state, rec.employer_business_id);
        if (!biz)
            continue;

        float wage = rec.offered_wage;

        if (biz->cash >= wage) {
            // Business can afford wage: pay it.
            // Deduct from business — not directly modifiable (WorldState is const).
            // We emit NPCDelta for the NPC capital credit.
            NPCDelta npc_delta{};
            npc_delta.npc_id = rec.npc_id;
            npc_delta.capital_delta = wage;
            delta.npc_deltas.push_back(npc_delta);

            // Reset deferred counter on successful payment.
            rec.deferred_salary_ticks = 0;
        } else {
            // Business cannot pay. Accumulate deferred salary.
            rec.deferred_salary_ticks++;

            // If deferred too long, generate wage theft memory.
            if (rec.deferred_salary_ticks > LaborModuleConfig{}.deferred_salary_max_ticks) {
                NPCDelta npc_delta{};
                npc_delta.npc_id = rec.npc_id;

                MemoryEntry mem{};
                mem.tick_timestamp = state.current_tick;
                mem.type = MemoryType::witnessed_wage_theft;
                mem.subject_id = rec.employer_business_id;
                mem.emotional_weight = -0.5f;  // mid-range of [-0.3, -0.7]
                mem.decay = 1.0f;
                mem.is_actionable = true;

                npc_delta.new_memory_entry = mem;
                delta.npc_deltas.push_back(npc_delta);
            }
        }
    }
}

// ===========================================================================
// LaborMarketModule — hiring decisions
// ===========================================================================

void LaborMarketModule::process_hiring_decisions(uint32_t province_id, const WorldState& state,
                                                 DeltaBuffer& delta) {
    // Process postings for this province, sorted by posting id ascending.
    std::vector<std::size_t> province_postings;
    for (std::size_t i = 0; i < job_postings_.size(); ++i) {
        auto& posting = job_postings_[i];
        if (posting.province_id != province_id)
            continue;
        if (posting.filled)
            continue;
        if (posting.expires_tick <= state.current_tick)
            continue;
        province_postings.push_back(i);
    }

    std::sort(
        province_postings.begin(), province_postings.end(),
        [this](std::size_t a, std::size_t b) { return job_postings_[a].id < job_postings_[b].id; });

    for (std::size_t idx : province_postings) {
        auto& posting = job_postings_[idx];

        // Get applications for this posting.
        auto app_it = applications_.find(posting.id);
        if (app_it == applications_.end() || app_it->second.empty()) {
            continue;
        }

        auto& apps = app_it->second;

        // Find best applicant: highest skill_level / salary_expectation ratio,
        // subject to min_skill_level and offered_wage >= salary_expectation.
        const WorkerApplication* best = nullptr;
        float best_ratio = -1.0f;

        for (const auto& app : apps) {
            // Filter: must meet minimum skill level.
            if (app.skill_level < posting.min_skill_level)
                continue;

            // Filter: offered wage must meet salary expectation.
            if (posting.offered_wage < app.salary_expectation)
                continue;

            // Filter: NPC must still be active and in this province.
            const NPC* npc = find_npc(state, app.applicant_npc_id);
            if (!npc)
                continue;
            if (npc->status != NPCStatus::active)
                continue;

            // Check NPC is not already employed.
            const EmploymentRecord* existing = find_employment(app.applicant_npc_id);
            if (existing && existing->employer_business_id != 0)
                continue;

            float ratio = app.skill_level / app.salary_expectation;
            if (ratio > best_ratio) {
                best_ratio = ratio;
                best = &app;
            }
        }

        if (best) {
            // Hire the best applicant.
            posting.filled = true;

            // Create or update employment record.
            EmploymentRecord* existing = find_employment(best->applicant_npc_id);
            if (existing) {
                existing->employer_business_id = posting.business_id;
                existing->offered_wage = posting.offered_wage;
                existing->hired_tick = state.current_tick;
                existing->deferred_salary_ticks = 0;
            } else {
                employment_records_.push_back(
                    EmploymentRecord{best->applicant_npc_id, posting.business_id,
                                     posting.offered_wage, state.current_tick, 0});
            }

            // Emit hiring memory: employment_positive.
            // Emotional weight scaled by overpay ratio.
            float overpay_ratio = (posting.offered_wage / best->salary_expectation) - 1.0f;
            float emotional_weight = 0.1f + std::min(0.4f, overpay_ratio * 0.5f);
            emotional_weight = std::max(0.1f, std::min(0.5f, emotional_weight));

            NPCDelta hire_delta{};
            hire_delta.npc_id = best->applicant_npc_id;

            MemoryEntry hire_mem{};
            hire_mem.tick_timestamp = state.current_tick;
            hire_mem.type = MemoryType::employment_positive;
            hire_mem.subject_id = posting.business_id;
            hire_mem.emotional_weight = emotional_weight;
            hire_mem.decay = 1.0f;
            hire_mem.is_actionable = false;

            hire_delta.new_memory_entry = hire_mem;
            delta.npc_deltas.push_back(hire_delta);
        }
    }
}

// ===========================================================================
// LaborMarketModule — voluntary departures
// ===========================================================================

void LaborMarketModule::process_voluntary_departures(uint32_t province_id, const WorldState& state,
                                                     DeltaBuffer& delta, DeterministicRNG& rng) {
    // Collect employed NPCs in this province, sorted by npc_id.
    std::vector<std::size_t> province_employed;
    for (std::size_t i = 0; i < employment_records_.size(); ++i) {
        auto& rec = employment_records_[i];
        if (rec.employer_business_id == 0)
            continue;

        const NPC* npc = find_npc(state, rec.npc_id);
        if (!npc)
            continue;
        if (npc->current_province_id != province_id)
            continue;
        if (npc->status != NPCStatus::active)
            continue;

        province_employed.push_back(i);
    }

    std::sort(province_employed.begin(), province_employed.end(),
              [this](std::size_t a, std::size_t b) {
                  return employment_records_[a].npc_id < employment_records_[b].npc_id;
              });

    for (std::size_t idx : province_employed) {
        auto& rec = employment_records_[idx];
        const NPC* npc = find_npc(state, rec.npc_id);
        if (!npc)
            continue;

        float satisfaction = compute_worker_satisfaction(*npc);

        // Only evaluate departure if satisfaction below threshold.
        if (satisfaction >= LaborModuleConfig{}.voluntary_departure_threshold)
            continue;

        // career motivation weight (OutcomeType::career_advance = 2).
        float career_motivation = npc->motivations.weights[2];

        float departure_prob =
            LaborModuleConfig{}.departure_base_rate * (1.0f - satisfaction) * career_motivation;

        // Draw from RNG to decide departure.
        float roll = rng.next_float();
        if (roll < departure_prob) {
            // Capture business_id before clearing employment.
            uint32_t former_employer = rec.employer_business_id;

            // NPC departs voluntarily.
            rec.employer_business_id = 0;
            rec.offered_wage = 0.0f;

            // Emit employment_negative memory.
            NPCDelta dep_delta{};
            dep_delta.npc_id = rec.npc_id;

            MemoryEntry dep_mem{};
            dep_mem.tick_timestamp = state.current_tick;
            dep_mem.type = MemoryType::employment_negative;
            dep_mem.subject_id = former_employer;
            dep_mem.emotional_weight = -0.2f;
            dep_mem.decay = 1.0f;
            dep_mem.is_actionable = false;

            dep_delta.new_memory_entry = dep_mem;
            delta.npc_deltas.push_back(dep_delta);
        }
    }
}

// ===========================================================================
// LaborMarketModule — expired posting cleanup
// ===========================================================================

void LaborMarketModule::close_expired_postings(uint32_t province_id, uint32_t current_tick) {
    for (auto& posting : job_postings_) {
        if (posting.province_id != province_id)
            continue;
        if (posting.filled)
            continue;
        if (posting.expires_tick <= current_tick) {
            posting.filled = true;  // Mark as closed (unfilled).
        }
    }
}

// ===========================================================================
// LaborMarketModule — monthly wage update
// ===========================================================================

void LaborMarketModule::update_regional_wages(const WorldState& state) {
    // For each province, for each SkillDomain, compute labor supply/demand
    // and adjust wages accordingly.
    //
    // Labor demand: count of active job postings in this province for this domain.
    // Labor supply: count of unemployed NPCs in this province with skill in this domain.
    //
    // ratio = demand / supply (or 1.0 if both are 0, 2.0 if supply is 0 with demand).
    // When ratio > 1 (labor shortage): wage rises.
    // When ratio < 1 (labor surplus): wage falls.
    // new_wage = old_wage * (1.0 + wage_adjustment_rate * (ratio - 1.0))
    // Clamp to [wage_floor, wage_ceiling].

    for (const auto& prov : state.provinces) {
        if (prov.lod_level != SimulationLOD::full)
            continue;

        uint32_t pid = prov.id;

        // Compute wage ceiling from province median income.
        float median_income = prov.demographics.income_middle_fraction;
        // For V1: use a reasonable default if median_income is near-zero.
        float base_wage = (median_income > 0.01f) ? median_income : 1.0f;
        float wage_ceiling = LaborModuleConfig{}.wage_ceiling_multiplier * base_wage;

        // Count demand and supply per domain.
        // We iterate over a fixed set of SkillDomain values.
        static constexpr uint8_t NUM_SKILL_DOMAINS = 15;

        for (uint8_t d = 0; d < NUM_SKILL_DOMAINS; ++d) {
            auto domain = static_cast<SkillDomain>(d);
            ProvinceSkillKey key{pid, domain};

            // Count demand: active unfilled postings in this province for this domain.
            float demand = 0.0f;
            for (const auto& posting : job_postings_) {
                if (posting.province_id == pid && posting.required_domain == domain &&
                    !posting.filled) {
                    demand += 1.0f;
                }
            }

            // Count supply: unemployed NPCs in this province with this skill.
            float supply = 0.0f;
            for (uint32_t npc_id : prov.significant_npc_ids) {
                const EmploymentRecord* emp = find_employment(npc_id);
                if (emp && emp->employer_business_id != 0)
                    continue;  // already employed

                // Check if NPC has skill in this domain.
                float skill = get_npc_skill(npc_id, domain);
                if (skill > 0.0f) {
                    supply += 1.0f;
                }
            }

            // ratio = demand / supply. When demand > supply (labor shortage),
            // ratio > 1, wage rises. When demand < supply (surplus), ratio < 1,
            // wage falls. If supply == 0, use demand directly (capped at 2.0).
            float ratio = 1.0f;
            if (supply > 0.0f && demand > 0.0f) {
                ratio = demand / supply;
            } else if (demand > 0.0f && supply == 0.0f) {
                ratio = 2.0f;  // extreme shortage cap
            }
            // demand == 0: ratio stays 1.0 (no adjustment).

            // Get current wage (or initialize to a reasonable default).
            auto it = regional_wages_.find(key);
            float current_wage = (it != regional_wages_.end()) ? it->second : base_wage;

            // Adjust wage.
            float new_wage =
                current_wage * (1.0f + LaborModuleConfig{}.wage_adjustment_rate * (ratio - 1.0f));

            // Clamp.
            new_wage = std::max(LaborModuleConfig{}.wage_floor, new_wage);
            new_wage = std::min(wage_ceiling, new_wage);

            regional_wages_[key] = new_wage;
        }
    }
}

// ===========================================================================
// LaborMarketModule — employer reputation
// ===========================================================================

float LaborMarketModule::compute_employer_reputation(uint32_t business_id,
                                                     const WorldState& state) {
    // Reputation is derived from worker memory logs.
    // For each NPC, scan memory_log for entries with subject_id == business_id.
    // Positive employment memories increase reputation; negative decrease.
    // Default: 0.5 for unknown employers.
    float total_weight = 0.0f;
    float positive_weight = 0.0f;
    int memory_count = 0;

    for (const auto& npc : state.significant_npcs) {
        for (const auto& mem : npc.memory_log) {
            if (mem.subject_id != business_id)
                continue;

            if (mem.type == MemoryType::employment_positive ||
                mem.type == MemoryType::employment_negative ||
                mem.type == MemoryType::witnessed_wage_theft ||
                mem.type == MemoryType::witnessed_illegal_activity ||
                mem.type == MemoryType::witnessed_safety_violation) {
                float abs_weight = std::abs(mem.emotional_weight) * mem.decay;
                total_weight += abs_weight;
                if (mem.emotional_weight > 0.0f) {
                    positive_weight += abs_weight;
                }
                memory_count++;
            }
        }
    }

    if (memory_count == 0) {
        return LaborModuleConfig{}.reputation_default;
    }

    if (total_weight <= 0.0f) {
        return LaborModuleConfig{}.reputation_default;
    }

    return positive_weight / total_weight;
}

// ===========================================================================
// LaborMarketModule — worker satisfaction
// ===========================================================================

float LaborMarketModule::compute_worker_satisfaction(const NPC& npc) {
    // Satisfaction is computed fresh from the memory log.
    // Sum positive employment memories minus negative ones, normalized.
    float positive_sum = 0.0f;
    float negative_sum = 0.0f;

    for (const auto& mem : npc.memory_log) {
        if (mem.type == MemoryType::employment_positive) {
            positive_sum += mem.emotional_weight * mem.decay;
        } else if (mem.type == MemoryType::employment_negative ||
                   mem.type == MemoryType::witnessed_wage_theft ||
                   mem.type == MemoryType::witnessed_safety_violation ||
                   mem.type == MemoryType::witnessed_illegal_activity) {
            negative_sum += std::abs(mem.emotional_weight) * mem.decay;
        }
    }

    float total = positive_sum + negative_sum;
    if (total <= 0.0f) {
        return 0.5f;  // Neutral satisfaction if no employment memories.
    }

    return positive_sum / total;
}

// ===========================================================================
// LaborMarketModule — skill lookup
// ===========================================================================

float LaborMarketModule::get_npc_skill(uint32_t npc_id, SkillDomain domain) const {
    auto it = npc_skills_.find(npc_id);
    if (it == npc_skills_.end())
        return 0.0f;

    for (const auto& entry : it->second) {
        if (entry.domain == domain) {
            return entry.level;
        }
    }
    return 0.0f;
}

// ===========================================================================
// LaborMarketModule — pool size computation
// ===========================================================================

uint32_t LaborMarketModule::effective_pool_size(HiringChannel channel, float reputation) {
    uint32_t base_size = 0;
    switch (channel) {
        case HiringChannel::public_board:
            base_size = LaborModuleConfig{}.pool_size_public;
            break;
        case HiringChannel::professional_network:
            base_size = LaborModuleConfig{}.pool_size_professional;
            break;
        case HiringChannel::personal_referral:
            base_size = LaborModuleConfig{}.pool_size_referral;
            break;
    }

    // Low reputation penalty: reduce pool size.
    if (reputation < LaborModuleConfig{}.reputation_threshold) {
        float penalty = (LaborModuleConfig{}.reputation_threshold - reputation) *
                        LaborModuleConfig{}.reputation_pool_penalty_scale;
        uint32_t reduction = static_cast<uint32_t>(std::round(penalty));
        if (reduction >= base_size) {
            return 1;  // At least 1 applicant.
        }
        return base_size - reduction;
    }

    return base_size;
}

// ===========================================================================
// LaborMarketModule — salary expectation computation
// ===========================================================================

float LaborMarketModule::compute_salary_expectation(float regional_wage, float money_motivation,
                                                    float employer_reputation) {
    // Base expectation.
    float expectation = regional_wage * (1.0f + money_motivation * 0.3f);

    // Low reputation premium.
    if (employer_reputation < LaborModuleConfig{}.reputation_threshold) {
        float premium = 1.0f + (LaborModuleConfig{}.reputation_threshold - employer_reputation) *
                                   LaborModuleConfig{}.salary_premium_per_rep_point;
        expectation *= premium;
    }

    return expectation;
}

// ===========================================================================
// LaborMarketModule — lookup helpers
// ===========================================================================

EmploymentRecord* LaborMarketModule::find_employment(uint32_t npc_id) {
    for (auto& rec : employment_records_) {
        if (rec.npc_id == npc_id) {
            return &rec;
        }
    }
    return nullptr;
}

const EmploymentRecord* LaborMarketModule::find_employment(uint32_t npc_id) const {
    for (const auto& rec : employment_records_) {
        if (rec.npc_id == npc_id) {
            return &rec;
        }
    }
    return nullptr;
}

const NPC* LaborMarketModule::find_npc(const WorldState& state, uint32_t npc_id) {
    for (const auto& npc : state.significant_npcs) {
        if (npc.id == npc_id) {
            return &npc;
        }
    }
    for (const auto& npc : state.named_background_npcs) {
        if (npc.id == npc_id) {
            return &npc;
        }
    }
    return nullptr;
}

const NPCBusiness* LaborMarketModule::find_business(const WorldState& state, uint32_t business_id) {
    for (const auto& biz : state.npc_businesses) {
        if (biz.id == business_id) {
            return &biz;
        }
    }
    return nullptr;
}

}  // namespace econlife
