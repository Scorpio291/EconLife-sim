// Labor Market Module — implementation.
// See labor_market_module.h for class declarations and
// docs/interfaces/labor_market/INTERFACE.md for the canonical specification.

#include "modules/labor_market/labor_market_module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/rng/deterministic_rng.h"
#include "core/world_state/apply_deltas.h"  // lookup_npc_by_id
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

// ===========================================================================
// LaborMarketModule — tick execution
// ===========================================================================

void LaborMarketModule::init_for_tick(const WorldState& state) {
    // Pre-populate employment records for every significant NPC that does not
    // yet have one.  This runs on the main thread before province-parallel
    // dispatch, so no synchronisation is needed.  After this call the vector's
    // size is stable and execute_province() never pushes new elements — it only
    // mutates fields of records that belong to NPCs in its own province.
    //
    // Maintain employment_index_ inline: rebuild once if it is out of sync
    // (e.g. tests appended directly through the employment_records() accessor),
    // then update it as we push new records so the per-NPC find is O(1).
    if (employment_index_.size() != employment_records_.size()) {
        rebuild_employment_index();
    }
    auto ensure_record = [&](uint32_t npc_id) {
        if (employment_index_.find(npc_id) != employment_index_.end())
            return;
        employment_index_[npc_id] = employment_records_.size();
        employment_records_.push_back(EmploymentRecord{npc_id, /*employer_business_id=*/0,
                                                       /*offered_wage=*/0.0f, /*hired_tick=*/0,
                                                       /*deferred_salary_ticks=*/0});
    };
    for (const auto& npc : state.significant_npcs) {
        ensure_record(npc.id);
    }
    for (const auto& npc : state.named_background_npcs) {
        ensure_record(npc.id);
    }

    // Generate formal labor demand (also runs pre-parallel, so it can safely
    // grow job_postings_/applications_).
    generate_job_postings(state);
}

void LaborMarketModule::generate_job_postings(const WorldState& state) {
    // --- Garbage-collect resolved postings every cycle so the producer below
    // does not grow job_postings_/applications_ without bound. A posting is
    // resolved once it is filled (close_expired_postings marks unfilled-expired
    // ones filled too). ---
    if (!job_postings_.empty()) {
        std::vector<JobPosting> keep;
        keep.reserve(job_postings_.size());
        for (auto& p : job_postings_) {
            const bool resolved = p.filled || p.expires_tick <= state.current_tick;
            if (resolved) {
                applications_.erase(p.id);
            } else {
                keep.push_back(std::move(p));
            }
        }
        job_postings_ = std::move(keep);
    }

    // Postings are created on the monthly hiring cadence only.
    if (state.current_tick == 0 || state.current_tick % lcfg_.monthly_tick_interval != 0)
        return;

    // Keep the id source ahead of anything loaded from a save.
    for (const auto& p : job_postings_)
        next_posting_id_ = std::max(next_posting_id_, p.id + 1);

    // Current formal headcount and open-vacancy counts per business.
    std::unordered_map<uint32_t, uint32_t> headcount;
    for (const auto& rec : employment_records_)
        if (rec.employer_business_id != 0)
            ++headcount[rec.employer_business_id];
    std::unordered_map<uint32_t, uint32_t> open_postings;
    for (const auto& p : job_postings_)
        if (!p.filled && p.expires_tick > state.current_tick)
            ++open_postings[p.business_id];

    // Unemployed (no formal employer), active NPCs per province, sorted by id
    // for deterministic applicant selection.
    std::unordered_map<uint32_t, std::vector<uint32_t>> unemployed_by_province;
    for (const auto& npc : state.significant_npcs) {
        if (npc.status != NPCStatus::active)
            continue;
        const EmploymentRecord* rec = find_employment(npc.id);
        if (rec && rec->employer_business_id != 0)
            continue;
        unemployed_by_province[npc.current_province_id].push_back(npc.id);
    }
    for (auto& [pid, pool] : unemployed_by_province)
        std::sort(pool.begin(), pool.end());
    std::unordered_map<uint32_t, std::size_t> cursor;  // per-province applicant cursor

    // Businesses in id order (determinism).
    std::vector<const NPCBusiness*> bizs;
    bizs.reserve(state.npc_businesses.size());
    for (const auto& b : state.npc_businesses)
        bizs.push_back(&b);
    std::sort(bizs.begin(), bizs.end(),
              [](const NPCBusiness* a, const NPCBusiness* b) { return a->id < b->id; });

    const float offered_wage = lcfg_.revenue_per_worker * lcfg_.wage_revenue_fraction;

    for (const NPCBusiness* b : bizs) {
        uint32_t target =
            static_cast<uint32_t>(std::clamp(b->revenue_per_tick / lcfg_.revenue_per_worker, 1.0f,
                                             static_cast<float>(lcfg_.max_workers_per_business)));
        uint32_t have = headcount[b->id] + open_postings[b->id];
        if (have >= target)
            continue;
        auto& pool = unemployed_by_province[b->province_id];
        if (pool.empty())
            continue;
        uint32_t vacancies = std::min(target - have, lcfg_.max_new_postings_per_business);

        for (uint32_t v = 0; v < vacancies; ++v) {
            JobPosting jp{};
            jp.id = next_posting_id_++;
            jp.owner_id = b->owner_id;
            jp.business_id = b->id;
            jp.province_id = b->province_id;
            jp.required_domain = SkillDomain::Trade;  // general labor; refined in slice 2
            jp.min_skill_level = 0.0f;
            jp.offered_wage = offered_wage;
            jp.channel = HiringChannel::public_board;
            jp.posted_tick = state.current_tick;
            jp.expires_tick = state.current_tick + lcfg_.job_posting_duration_ticks;
            jp.filled = false;

            // Generate applicants from the province's unemployed pool. A
            // salary_expectation at or below the offer guarantees acceptance.
            auto& apps = applications_[jp.id];
            for (uint32_t a = 0; a < lcfg_.applicants_per_posting && !pool.empty(); ++a) {
                std::size_t& cur = cursor[b->province_id];
                if (cur >= pool.size())
                    cur = 0;  // wrap; a small pool can apply to multiple postings
                uint32_t app_npc = pool[cur++];
                WorkerApplication wa{};
                wa.applicant_npc_id = app_npc;
                wa.skill_level = get_npc_skill(app_npc, jp.required_domain);
                wa.salary_expectation = offered_wage * 0.8f;
                wa.loyalty_prior = 0.0f;
                wa.background_visible = false;
                jp.applicant_ids.push_back(app_npc);
                apps.push_back(wa);
            }
            job_postings_.push_back(std::move(jp));
        }
    }
}

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

    // Step 5: Update unemployment_rate and formal_employment_rate monitors on
    // cohort_stats, sampled from this province's significant NPCs. Stored rates
    // converge toward the sample fraction (per-tick noise smooths out).
    //
    // Per RegionCohortStats: unemployment = working-age neither in formal NOR
    // INFORMAL employment. Mapping onto observable NPC state:
    //   - formal:   has an employment_record with employer_business_id != 0
    //   - informal: status == active without a formal employer — an acting NPC
    //     is out doing something (subsistence/day labor; npc_behavior's work
    //     action pays an informal floor wage). There is always some work for
    //     willing bodies, so activity without formal employment is informal
    //     work, not unemployment.
    //   - unemployed: status == waiting without a formal employer — the NPC
    //     judged no action worth taking; out of work entirely.
    // Labor force (denominator) = active + waiting. The previous version
    // excluded `waiting` NPCs from the sample and counted every non-formal
    // active NPC as unemployed, which pinned unemployment at 1.0 the moment the
    // (still unwired) formal hiring market produced zero records.
    if (province_idx < state.npc_indices_by_province.size()) {
        uint32_t labor_force = 0;
        uint32_t formal_count = 0;
        uint32_t unemployed_count = 0;
        for (uint32_t idx : state.npc_indices_by_province[province_idx]) {
            const NPC& npc = state.significant_npcs[idx];
            if (npc.status != NPCStatus::active && npc.status != NPCStatus::waiting)
                continue;  // imprisoned/dead/fled are out of the labor force
            ++labor_force;
            const EmploymentRecord* rec = find_employment(npc.id);
            const bool formal = rec && rec->employer_business_id != 0;
            if (formal) {
                ++formal_count;
            } else if (npc.status == NPCStatus::waiting) {
                ++unemployed_count;
            }
        }

        if (labor_force > 0) {
            constexpr float RATE_CONVERGENCE = 0.05f;
            const float formal_fraction =
                static_cast<float>(formal_count) / static_cast<float>(labor_force);
            const float unemp_fraction =
                static_cast<float>(unemployed_count) / static_cast<float>(labor_force);

            const auto& cs = state.provinces[province_idx].cohort_stats;
            const float cur_formal = cs ? cs->formal_employment_rate : 0.0f;
            const float cur_unemp = cs ? cs->unemployment_rate : 0.0f;

            RegionDelta rd{};
            rd.region_id = state.provinces[province_idx].region_id;
            rd.formal_employment_rate_delta = RATE_CONVERGENCE * (formal_fraction - cur_formal);
            rd.unemployment_rate_delta = RATE_CONVERGENCE * (unemp_fraction - cur_unemp);
            province_delta.region_deltas.push_back(rd);
        }
    }
}

void LaborMarketModule::execute(const WorldState& state, DeltaBuffer& /*delta*/) {
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

            // Update employment record (pre-populated by init_for_tick).
            EmploymentRecord* existing = find_employment(best->applicant_npc_id);
            if (!existing) {
                // Record was not pre-populated — skip to avoid pushing into the
                // shared vector during province-parallel execution.
                continue;
            }
            existing->employer_business_id = posting.business_id;
            existing->offered_wage = posting.offered_wage;
            existing->hired_tick = state.current_tick;
            existing->deferred_salary_ticks = 0;

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

void LaborMarketModule::rebuild_employment_index() const {
    employment_index_.clear();
    employment_index_.reserve(employment_records_.size());
    for (std::size_t i = 0; i < employment_records_.size(); ++i) {
        employment_index_[employment_records_[i].npc_id] = i;
    }
}

EmploymentRecord* LaborMarketModule::find_employment(uint32_t npc_id) {
    if (employment_index_.size() != employment_records_.size()) {
        rebuild_employment_index();
    }
    auto it = employment_index_.find(npc_id);
    if (it == employment_index_.end())
        return nullptr;
    return &employment_records_[it->second];
}

const EmploymentRecord* LaborMarketModule::find_employment(uint32_t npc_id) const {
    if (employment_index_.size() != employment_records_.size()) {
        rebuild_employment_index();
    }
    auto it = employment_index_.find(npc_id);
    if (it == employment_index_.end())
        return nullptr;
    return &employment_records_[it->second];
}

const NPC* LaborMarketModule::find_npc(const WorldState& state, uint32_t npc_id) {
    if (const NPC* npc = lookup_npc_by_id(state, npc_id))
        return npc;
    // Background NPCs are not in the id index; the labor market does maintain
    // employment records for them, so fall back to a linear scan.
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

// ─── Persistence helpers (schema v7) ────────────────────────────────────────
//
// Order of keyed maps (npc_skills_, regional_wages_, applications_) is
// canonicalised on write so byte output is deterministic regardless of
// std::unordered_map insertion order.
//
// Format (little-endian):
//   u32 schema_tag (1)
//   u32 posting_count
//   for each JobPosting:
//     u32 id, u32 owner_id, u32 business_id, u32 province_id
//     u8 required_domain
//     f32 min_skill_level, f32 offered_wage
//     u8 channel
//     u32 posted_tick, u32 expires_tick
//     u32 applicant_count, u32[applicant_count]
//     u8 filled
//   u32 employment_count
//   for each EmploymentRecord:
//     u32 npc_id, u32 employer_business_id
//     f32 offered_wage
//     u32 hired_tick, u32 deferred_salary_ticks
//   u32 npc_skill_npc_count
//   for each npc (sorted by npc_id):
//     u32 npc_id, u32 entry_count
//     for each entry: u8 domain, f32 level
//   u32 regional_wage_count
//   for each entry (sorted by province_id, domain):
//     u32 province_id, u8 domain, f32 wage
//   u32 application_posting_count
//   for each posting (sorted by posting id):
//     u32 posting_id, u32 application_count
//     for each WorkerApplication:
//       u32 applicant_npc_id, f32 skill_level, f32 salary_expectation,
//       f32 loyalty_prior, u8 background_visible

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

void LaborMarketModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 1u);

    // Job postings
    put_u32(out, static_cast<uint32_t>(job_postings_.size()));
    for (const auto& p : job_postings_) {
        put_u32(out, p.id);
        put_u32(out, p.owner_id);
        put_u32(out, p.business_id);
        put_u32(out, p.province_id);
        out.push_back(static_cast<uint8_t>(p.required_domain));
        put_f32(out, p.min_skill_level);
        put_f32(out, p.offered_wage);
        out.push_back(static_cast<uint8_t>(p.channel));
        put_u32(out, p.posted_tick);
        put_u32(out, p.expires_tick);
        put_u32(out, static_cast<uint32_t>(p.applicant_ids.size()));
        for (uint32_t a : p.applicant_ids)
            put_u32(out, a);
        out.push_back(p.filled ? 1u : 0u);
    }

    // Employment records
    put_u32(out, static_cast<uint32_t>(employment_records_.size()));
    for (const auto& e : employment_records_) {
        put_u32(out, e.npc_id);
        put_u32(out, e.employer_business_id);
        put_f32(out, e.offered_wage);
        put_u32(out, e.hired_tick);
        put_u32(out, e.deferred_salary_ticks);
    }

    // NPC skills — sort by npc_id for determinism
    {
        std::vector<uint32_t> npc_ids;
        npc_ids.reserve(npc_skills_.size());
        for (const auto& kv : npc_skills_)
            npc_ids.push_back(kv.first);
        std::sort(npc_ids.begin(), npc_ids.end());
        put_u32(out, static_cast<uint32_t>(npc_ids.size()));
        for (uint32_t npc_id : npc_ids) {
            const auto& entries = npc_skills_.at(npc_id);
            put_u32(out, npc_id);
            put_u32(out, static_cast<uint32_t>(entries.size()));
            for (const auto& s : entries) {
                out.push_back(static_cast<uint8_t>(s.domain));
                put_f32(out, s.level);
            }
        }
    }

    // Regional wages — sort by (province_id, domain) for determinism
    {
        std::vector<std::pair<ProvinceSkillKey, float>> entries;
        entries.reserve(regional_wages_.size());
        for (const auto& kv : regional_wages_)
            entries.push_back(kv);
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.first.province_id != b.first.province_id)
                return a.first.province_id < b.first.province_id;
            return static_cast<uint8_t>(a.first.domain) < static_cast<uint8_t>(b.first.domain);
        });
        put_u32(out, static_cast<uint32_t>(entries.size()));
        for (const auto& kv : entries) {
            put_u32(out, kv.first.province_id);
            out.push_back(static_cast<uint8_t>(kv.first.domain));
            put_f32(out, kv.second);
        }
    }

    // Applications — sort by posting_id for determinism
    {
        std::vector<uint32_t> posting_ids;
        posting_ids.reserve(applications_.size());
        for (const auto& kv : applications_)
            posting_ids.push_back(kv.first);
        std::sort(posting_ids.begin(), posting_ids.end());
        put_u32(out, static_cast<uint32_t>(posting_ids.size()));
        for (uint32_t posting_id : posting_ids) {
            const auto& apps = applications_.at(posting_id);
            put_u32(out, posting_id);
            put_u32(out, static_cast<uint32_t>(apps.size()));
            for (const auto& a : apps) {
                put_u32(out, a.applicant_npc_id);
                put_f32(out, a.skill_level);
                put_f32(out, a.salary_expectation);
                put_f32(out, a.loyalty_prior);
                out.push_back(a.background_visible ? 1u : 0u);
            }
        }
    }
}

bool LaborMarketModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    if (r.u32() != 1u)
        return false;

    uint32_t posting_count = r.u32();
    job_postings_.clear();
    job_postings_.reserve(posting_count);
    for (uint32_t i = 0; i < posting_count; ++i) {
        JobPosting p{};
        p.id = r.u32();
        p.owner_id = r.u32();
        p.business_id = r.u32();
        p.province_id = r.u32();
        p.required_domain = static_cast<SkillDomain>(r.u8());
        p.min_skill_level = r.f32();
        p.offered_wage = r.f32();
        p.channel = static_cast<HiringChannel>(r.u8());
        p.posted_tick = r.u32();
        p.expires_tick = r.u32();
        uint32_t app_n = r.u32();
        if (r.error)
            return false;
        p.applicant_ids.reserve(app_n);
        for (uint32_t j = 0; j < app_n; ++j)
            p.applicant_ids.push_back(r.u32());
        p.filled = (r.u8() != 0);
        if (r.error)
            return false;
        job_postings_.push_back(std::move(p));
    }

    uint32_t emp_count = r.u32();
    employment_records_.clear();
    employment_records_.reserve(emp_count);
    for (uint32_t i = 0; i < emp_count; ++i) {
        EmploymentRecord e{};
        e.npc_id = r.u32();
        e.employer_business_id = r.u32();
        e.offered_wage = r.f32();
        e.hired_tick = r.u32();
        e.deferred_salary_ticks = r.u32();
        if (r.error)
            return false;
        employment_records_.push_back(e);
    }
    // employment_index_ is rebuilt lazily on next find_employment call.
    employment_index_.clear();

    uint32_t skill_npc_count = r.u32();
    npc_skills_.clear();
    npc_skills_.reserve(skill_npc_count);
    for (uint32_t i = 0; i < skill_npc_count; ++i) {
        uint32_t npc_id = r.u32();
        uint32_t entry_n = r.u32();
        if (r.error)
            return false;
        std::vector<NPCSkillEntry> entries;
        entries.reserve(entry_n);
        for (uint32_t j = 0; j < entry_n; ++j) {
            NPCSkillEntry s{};
            s.domain = static_cast<SkillDomain>(r.u8());
            s.level = r.f32();
            entries.push_back(s);
        }
        if (r.error)
            return false;
        npc_skills_.emplace(npc_id, std::move(entries));
    }

    uint32_t wage_count = r.u32();
    regional_wages_.clear();
    regional_wages_.reserve(wage_count);
    for (uint32_t i = 0; i < wage_count; ++i) {
        ProvinceSkillKey k{};
        k.province_id = r.u32();
        k.domain = static_cast<SkillDomain>(r.u8());
        float w = r.f32();
        if (r.error)
            return false;
        regional_wages_.emplace(k, w);
    }

    uint32_t apps_posting_count = r.u32();
    applications_.clear();
    applications_.reserve(apps_posting_count);
    for (uint32_t i = 0; i < apps_posting_count; ++i) {
        uint32_t posting_id = r.u32();
        uint32_t app_n = r.u32();
        if (r.error)
            return false;
        std::vector<WorkerApplication> apps;
        apps.reserve(app_n);
        for (uint32_t j = 0; j < app_n; ++j) {
            WorkerApplication a{};
            a.applicant_npc_id = r.u32();
            a.skill_level = r.f32();
            a.salary_expectation = r.f32();
            a.loyalty_prior = r.f32();
            a.background_visible = (r.u8() != 0);
            apps.push_back(a);
        }
        if (r.error)
            return false;
        applications_.emplace(posting_id, std::move(apps));
    }

    return !r.error;
}

}  // namespace econlife
