#pragma once

// Labor Market Module — province-parallel tick module that processes wage
// payments, hiring decisions, firing/departure, and monthly wage adjustment.
//
// See docs/interfaces/labor_market/INTERFACE.md for the canonical specification.

#include <unordered_map>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/labor_market/labor_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;
struct NPCBusiness;
class DeterministicRNG;

// ---------------------------------------------------------------------------
// LaborMarketModule — ITickModule implementation for labor market
// ---------------------------------------------------------------------------
class LaborMarketModule : public ITickModule {
   public:
    explicit LaborMarketModule(const LaborModuleConfig& lcfg = {}) : lcfg_(lcfg) {}

    std::string_view name() const noexcept override { return "labor_market"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }

    std::vector<std::string_view> runs_after() const override { return {"production"}; }

    std::vector<std::string_view> runs_before() const override { return {"price_engine"}; }

    bool is_province_parallel() const noexcept override { return true; }
    bool has_global_post_pass() const noexcept override { return true; }

    void init_for_tick(const WorldState& state) override;

    void execute_province(uint32_t province_idx, const WorldState& state,
                          DeltaBuffer& province_delta) override;

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // --- State access (for test injection and module initialization) ---

    // Active job postings. Postings are created externally (by NPC business
    // decision or player action) and stored here for processing.
    std::vector<JobPosting>& job_postings() { return job_postings_; }
    const std::vector<JobPosting>& job_postings() const { return job_postings_; }

    // Employment records. Tracks which NPC is employed at which business.
    std::vector<EmploymentRecord>& employment_records() { return employment_records_; }
    const std::vector<EmploymentRecord>& employment_records() const { return employment_records_; }

    // NPC skill levels. Keyed by npc_id. Each NPC may have multiple skill entries.
    std::unordered_map<uint32_t, std::vector<NPCSkillEntry>>& npc_skills() { return npc_skills_; }
    const std::unordered_map<uint32_t, std::vector<NPCSkillEntry>>& npc_skills() const {
        return npc_skills_;
    }

    // Regional wage map. Per province per SkillDomain wage rate.
    RegionalWageMap& regional_wages() { return regional_wages_; }
    const RegionalWageMap& regional_wages() const { return regional_wages_; }

    // Worker applications per posting. Keyed by JobPosting.id.
    std::unordered_map<uint32_t, std::vector<WorkerApplication>>& applications() {
        return applications_;
    }
    const std::unordered_map<uint32_t, std::vector<WorkerApplication>>& applications() const {
        return applications_;
    }

    // --- Utility functions exposed for testing ---

    // Compute employer reputation from worker memory logs.
    // Returns a value in [0.0, 1.0]. Unknown employers default to 0.5.
    static float compute_employer_reputation(uint32_t business_id, const WorldState& state);

    // Compute worker satisfaction from NPC memory log.
    // Returns a value in [0.0, 1.0].
    static float compute_worker_satisfaction(const NPC& npc);

    // Get NPC skill level in a given domain.
    // Returns 0.0 if NPC has no recorded skill in that domain.
    float get_npc_skill(uint32_t npc_id, SkillDomain domain) const;

    // Get the pool size for a given channel, adjusted for employer reputation.
    static uint32_t effective_pool_size(HiringChannel channel, float reputation);

    // Compute salary expectation for an applicant, including reputation premium.
    static float compute_salary_expectation(float regional_wage, float money_motivation,
                                            float employer_reputation);

    // Find employment record for an NPC. Returns nullptr if not employed.
    EmploymentRecord* find_employment(uint32_t npc_id);
    const EmploymentRecord* find_employment(uint32_t npc_id) const;

   private:
    LaborModuleConfig lcfg_;
    std::vector<JobPosting> job_postings_;
    std::vector<EmploymentRecord> employment_records_;
    // npc_id -> index into employment_records_. Rebuilt lazily when its
    // size diverges from employment_records_ (covers both internal pushes
    // and external pushes via the employment_records() accessor used by
    // tests). Mutable so const find_employment can refresh on demand.
    mutable std::unordered_map<uint32_t, std::size_t> employment_index_;
    std::unordered_map<uint32_t, std::vector<NPCSkillEntry>> npc_skills_;
    RegionalWageMap regional_wages_;
    std::unordered_map<uint32_t, std::vector<WorkerApplication>> applications_;

    // Rebuild employment_index_ from employment_records_. Idempotent.
    void rebuild_employment_index() const;

    // --- Per-province processing ---

    // Step 1: Pay wages for all employed NPCs in this province.
    void process_wage_payments(uint32_t province_id, const WorldState& state, DeltaBuffer& delta);

    // Step 2: Process hiring decisions for postings in this province.
    void process_hiring_decisions(uint32_t province_id, const WorldState& state,
                                  DeltaBuffer& delta);

    // Step 3: Evaluate voluntary departures for workers in this province
    // (monthly, when current_tick % 30 == 0).
    void process_voluntary_departures(uint32_t province_id, const WorldState& state,
                                      DeltaBuffer& delta, DeterministicRNG& rng);

    // Step 4: Close expired postings.
    void close_expired_postings(uint32_t province_id, uint32_t current_tick);

    // --- Monthly global processing ---

    // Update regional wages based on labor supply/demand ratio.
    void update_regional_wages(const WorldState& state);

    // Find the NPC by id in world state. Returns nullptr if not found.
    static const NPC* find_npc(const WorldState& state, uint32_t npc_id);

    // Find business by id. Returns nullptr if not found.
    static const NPCBusiness* find_business(const WorldState& state, uint32_t business_id);
};

}  // namespace econlife
