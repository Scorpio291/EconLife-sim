#pragma once

// Criminal Operations Module — sequential tick module that manages criminal
// organization lifecycle: quarterly strategic decisions, territorial conflict
// state machine, expansion, and dominance tracking.
//
// See docs/interfaces/criminal_operations/INTERFACE.md for the canonical specification.

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/config/package_config.h"
#include "core/tick/tick_module.h"
#include "modules/criminal_operations/criminal_operations_types.h"

namespace econlife {

// Forward declarations
struct WorldState;
struct DeltaBuffer;
struct NPC;

// ---------------------------------------------------------------------------
// CriminalOperationsModule — ITickModule implementation
// ---------------------------------------------------------------------------
class CriminalOperationsModule : public ITickModule {
   public:
    explicit CriminalOperationsModule(const CriminalOperationsConfig& cfg = {}) : cfg_(cfg) {}

    std::string_view name() const noexcept override { return "criminal_operations"; }
    std::string_view package_id() const noexcept override { return "base_game"; }
    ModuleScope scope() const noexcept override { return ModuleScope::v1; }
    bool is_province_parallel() const noexcept override { return false; }

    std::vector<std::string_view> runs_after() const override {
        return {"evidence", "facility_signals"};
    }

    std::vector<std::string_view> runs_before() const override {
        return {"investigator_engine", "media_system"};
    }

    void execute(const WorldState& state, DeltaBuffer& delta) override;

    // Persistence (schema v7): organizations_ and active_expansions_ carry
    // the entire criminal-org topology — leadership, member rosters, per-
    // province dominance, conflict state, and in-flight expansion teams.
    // Without persistence the world's crime structure resets on load. See
    // ITickModule.
    void serialize_state(std::vector<uint8_t>& out) const override;
    bool deserialize_state(const uint8_t* data, size_t size) override;

    // --- Module-internal org storage ---
    std::vector<CriminalOrganization>& organizations() { return organizations_; }
    const std::vector<CriminalOrganization>& organizations() const { return organizations_; }

    std::vector<ExpansionTeam>& active_expansions() { return active_expansions_; }
    const std::vector<ExpansionTeam>& active_expansions() const { return active_expansions_; }

    // --- Static utility functions (exposed for testing) ---

    // Compute territory_pressure: sum of competing org dominance in shared provinces.
    static float compute_territory_pressure(const CriminalOrganization& org,
                                            const std::vector<CriminalOrganization>& all_orgs);

    // Compute cash_level: cash / (monthly_cost_estimate * comfortable_months).
    static float compute_cash_level(float cash, float monthly_cost, float comfortable_months);

    // Compute law_enforcement_heat: proxy from LE NPC data in org territories.
    // V1: returns max InvestigatorMeter proxy across org provinces.
    static float compute_le_heat(const CriminalOrganization& org, const std::vector<NPC>& npcs);

    // Evaluate strategic decision from priority matrix.
    static CriminalStrategicDecision evaluate_decision(float le_heat, float territory_pressure,
                                                       float cash_level,
                                                       const CriminalOperationsConfig& cfg);

    // Compute decision_day_offset from org id.
    static uint8_t compute_decision_offset(uint32_t org_id, uint32_t quarterly_interval);

    // Advance conflict stage by one step (max one per cycle).
    static TerritorialConflictStage advance_conflict_stage(TerritorialConflictStage current);

    // Compute initial dominance seed on territory establishment.
    static float initial_dominance_seed(float expansion_initial_dominance);

   private:
    CriminalOperationsConfig cfg_;
    std::vector<CriminalOrganization> organizations_;
    std::vector<ExpansionTeam> active_expansions_;
    // One-shot guard for the formation bootstrap. Not persisted: after a load
    // it resets to false and form_organizations() runs once more, but the
    // per-province idempotency check makes that pass a no-op for provinces
    // already covered by a loaded org.
    bool formed_ = false;

    // Assemble one CriminalOrganization per province that contains criminal
    // NPCs (criminal_operator / criminal_enforcer / fixer), from world-gen
    // data, so the criminal subsystem is live in a fresh game rather than
    // depending on orgs only ever arriving via save load. Deterministic;
    // idempotent per province (skips provinces already covered by an org).
    // For each newly formed org it also emits a RacketSeedDelta targeting the
    // lowest-id legitimate business in the province, which protection_rackets
    // drains the same tick.
    void form_organizations(const WorldState& state, DeltaBuffer& delta);

    // Process one org's quarterly decision.
    void process_strategic_decision(CriminalOrganization& org, const WorldState& state,
                                    DeltaBuffer& delta);

    // Advance conflict state machine for orgs with active conflicts.
    void process_conflict_states(const WorldState& state, DeltaBuffer& delta);

    // Process dormant orgs (zero active members).
    void process_dormant_orgs(const WorldState& state);

    // Publish each province's territorial-conflict intensity (the demand-relevant
    // max conflict_state over orgs operating there) to cohort_stats via RegionDelta,
    // so weapons_trafficking (and others) read the real conflict signal rather than
    // a dominance proxy.
    void publish_territorial_conflict(const WorldState& state, DeltaBuffer& delta) const;
};

}  // namespace econlife
