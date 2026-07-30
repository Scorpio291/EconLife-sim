#pragma once

// Shared stub types — types that are needed by core headers (world_state.h,
// delta_buffer.h, player.h) but are properly defined in module headers that
// haven't been implemented yet. Each stub contains the minimum fields needed
// for the core to compile and for initial integration tests.
//
// As modules are implemented, these stubs will be expanded with full fields
// from INTERFACE.md specs. The canonical definitions remain here to avoid
// circular dependencies between core and module headers.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace econlife {

// Canonical simulation calendar: one tick per in-game day (root CLAUDE.md). Annual
// modules gate on this constant — do not re-hardcode 365 per module.
inline constexpr uint32_t kTicksPerYear = 365;

// ---------------------------------------------------------------------------
// ConsequenceType — used by calendar_types.h (DeadlineConsequence)
// Will be expanded during evidence/consequence module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class ConsequenceType : uint8_t {
    legal_proceeding = 0,
    regulatory_action = 1,
    financial_penalty = 2,
    reputation_impact = 3,
    relationship_change = 4,
    physical_harm = 5,
    property_damage = 6,
    investigation_event = 7,
};

// ---------------------------------------------------------------------------
// EvidenceType — classification of evidence tokens
// Will be expanded during evidence module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class EvidenceType : uint8_t {
    financial = 0,    // money trail, account records, transaction logs
    physical = 1,     // material evidence, forensics, contraband
    testimonial = 2,  // witness statement, informant disclosure
    documentary = 3,  // documents, contracts, communications
    digital = 4,      // electronic records, surveillance data
};

// ---------------------------------------------------------------------------
// EvidenceToken — unit of evidence in the simulation
// Used in DeltaBuffer (std::optional) and WorldState (std::vector).
// Will be expanded during evidence module implementation (Tier 6).
// ---------------------------------------------------------------------------
struct EvidenceToken {
    uint32_t id;
    EvidenceType type;
    uint32_t source_npc_id;  // NPC who generated or holds this evidence
    uint32_t target_npc_id;  // NPC this evidence is about (0 = general)
    float actionability;     // 0.0-1.0; how actionable by investigators
    float decay_rate;        // per-tick decay of actionability
    uint32_t created_tick;
    uint32_t province_id;  // where the evidence originated
    bool is_active;        // false = retired/suppressed
};

// ---------------------------------------------------------------------------
// FavorType — classification of obligation/favor types
// Will be expanded during obligation_network module implementation (Tier 6).
// ---------------------------------------------------------------------------
enum class FavorType : uint8_t {
    financial_loan = 0,
    political_support = 1,
    evidence_suppressed = 2,
    whistleblower_silenced = 3,
    business_favor = 4,
    personal_favor = 5,
    criminal_cooperation = 6,
};

// ---------------------------------------------------------------------------
// ObligationNode — directed obligation between two actors
// Used in DeltaBuffer (std::vector) and WorldState (std::vector).
// Will be expanded during obligation_network module implementation (Tier 6).
// ---------------------------------------------------------------------------
struct ObligationNode {
    uint32_t id;
    uint32_t creditor_npc_id;  // who is owed
    uint32_t debtor_npc_id;    // who owes
    FavorType favor_type;
    float weight;  // 0.0-1.0; significance of the obligation
    uint32_t created_tick;
    bool is_active;  // false = fulfilled or expired
};

// ---------------------------------------------------------------------------
// InfluenceNetworkHealth — summary of player's influence network
// Used in PlayerCharacter. Computed by tick step 22.
// Will be expanded during influence_network module implementation (Tier 10).
// ---------------------------------------------------------------------------
struct InfluenceNetworkHealth {
    float overall_score;    // 0.0-1.0; composite health metric
    float network_reach;    // 0.0-1.0; how many actors player can influence
    float network_density;  // 0.0-1.0; interconnectedness of contacts
    float vulnerability;    // 0.0-1.0; how exposed the network is
};

// ---------------------------------------------------------------------------
// TechStage — lifecycle stage of a technology holding.
// ---------------------------------------------------------------------------
enum class TechStage : uint8_t {
    researched = 0,      // actor has capability; internal production only
    commercialized = 1,  // product on open market; observable by competitors
};

// ---------------------------------------------------------------------------
// TechHolding — per-actor ownership of a technology node.
// Each business that has researched or acquired a node holds one.
// ---------------------------------------------------------------------------
struct TechHolding {
    std::string node_key;
    uint32_t holder_id = 0;  // business_id
    TechStage stage = TechStage::researched;
    float maturation_level = 0.0f;    // 0.0-1.0; rises with continued investment
    float maturation_ceiling = 0.0f;  // era-gated max; recomputed each tick
    uint32_t researched_tick = 0;
    uint32_t commercialized_tick = 0;  // 0 if still Stage 1
    bool has_patent = false;
    bool internal_use_only = false;  // true = no market listing despite commercialized
};

// ---------------------------------------------------------------------------
// ActorTechnologyState — per-actor technology portfolio.
// Used in NPCBusiness. See R&D and Technology doc Part 3.5.
// ---------------------------------------------------------------------------
struct ActorTechnologyState {
    float effective_tech_tier = 0.0f;  // derived; max over active facilities

    // Per-node technology holdings. Key = node_key.
    std::map<std::string, TechHolding> holdings;

    // Query methods (inline for header-only availability).
    bool has_researched(const std::string& node_key) const { return holdings.count(node_key) > 0; }

    bool has_commercialized(const std::string& node_key) const {
        auto it = holdings.find(node_key);
        if (it == holdings.end())
            return false;
        return it->second.stage == TechStage::commercialized;
    }

    // Returns maturation_level for the given node, or 0.0 if not held.
    float maturation_of(const std::string& node_key) const {
        auto it = holdings.find(node_key);
        if (it == holdings.end())
            return 0.0f;
        return it->second.maturation_level;
    }
};

// ---------------------------------------------------------------------------
// DialogueLine — NPC dialogue line in a scene card
// Used in SceneCard (std::vector).
// Will be expanded during scene_cards module implementation (Tier 1).
// ---------------------------------------------------------------------------
struct DialogueLine {
    uint32_t speaker_npc_id;
    std::string text;
    float emotional_tone;  // -1.0 (hostile) to 1.0 (warm)
};

// ---------------------------------------------------------------------------
// PlayerChoice — player choice option in a scene card
// Used in SceneCard (std::vector).
// Will be expanded during scene_cards module implementation (Tier 1).
// ---------------------------------------------------------------------------
struct PlayerChoice {
    uint32_t id;
    std::string label;
    std::string description;
    uint32_t consequence_id;  // links to consequence system (0 = no consequence)
};

// ---------------------------------------------------------------------------
// DemographicGroup — background-population segmentation for the population_aging
// module. 12 groups (canonical model; mirrored by population_aging_types.h).
// Enum order is the canonical iteration order for deterministic floating-point
// accumulation.
// ---------------------------------------------------------------------------
enum class DemographicGroup : uint8_t {
    youth_urban = 0,
    youth_rural = 1,
    working_urban_low = 2,
    working_urban_mid = 3,
    working_urban_high = 4,
    working_rural_low = 5,
    working_rural_mid = 6,
    working_rural_high = 7,
    retiree_urban = 8,
    retiree_rural = 9,
    student = 10,
    unemployed = 11,
};
inline constexpr uint8_t kDemographicGroupCount = 12;

// ---------------------------------------------------------------------------
// PopulationCohort — one background-population segment within a province.
// Owned and evolved by the population_aging module (income/employment
// convergence, education drift, births/deaths/aging). Per-cohort skill_supply
// (and the province aggregate_skill_supply) are deferred in V1.
// ---------------------------------------------------------------------------
struct PopulationCohort {
    DemographicGroup group = DemographicGroup::youth_urban;
    uint32_t size = 0;                    // headcount
    float median_income = 0.0f;           // currency per tick
    float education_level = 0.0f;         // 0.0-1.0
    float employment_rate = 0.0f;         // 0.0-1.0
    float political_lean = 0.0f;          // -1.0 to 1.0
    float grievance_contribution = 0.0f;  // 0.0-1.0
    float addiction_prevalence = 0.0f;    // 0.0-1.0
};

// ---------------------------------------------------------------------------
// RegionCohortStats — aggregated demographic statistics per region.
// Single home for all population-fraction monitors. Fields previously
// scattered across `RegionConditions` (addiction_rate, crime_rate,
// formal_employment_rate, criminal_dominance_index) were consolidated here
// in schema v5; new monitors (sick_rate, homeless_rate, unemployment_rate)
// were added at the same time. The denominator for every *_rate field is
// the base population (`total_population`), not the named-NPC sample.
//
// Updated each tick by domain modules through `RegionDelta` (the field-name
// mapping is identical: `addiction_rate_delta` writes into `addiction_rate`,
// `sick_rate_delta` writes into `sick_rate`, etc.). Modules emit additive
// deltas; `apply_region_deltas` accumulates and clamps to [0,1].
// ---------------------------------------------------------------------------
struct RegionCohortStats {
    // --- Population scalar ---
    uint32_t total_population = 0;

    // --- Background-population cohorts (population_aging module) ---
    // The 12 DemographicGroups; seeded at world gen, evolved monthly/annually.
    // `total_population` is recomputed as sum(cohort.size) after any change.
    std::map<DemographicGroup, PopulationCohort> cohorts;
    float mean_income = 0.0f;           // size-weighted mean of cohort median_income
    float gini_coefficient = 0.0f;      // income inequality across cohorts [0,1]
    float regional_wage_anchor = 0.0f;  // income-convergence target for cohorts
                                        //   (proxy for the labor wage market, which
                                        //   is module-private; seeded at world gen)

    // --- Age structure ---
    float median_age = 0.0f;
    float working_age_fraction = 0.0f;  // 0.0-1.0; fraction of population 18-65
    float dependency_ratio = 0.0f;      // dependents / working_age

    // --- Problem-state monitors (population fractions, 0.0-1.0) ---
    // Migrated from RegionConditions in schema v5:
    float addiction_rate = 0.0f;            // dependent + active + terminal stages
    float crime_rate = 0.0f;                // adults engaged in criminal activity
    float criminal_dominance_index = 0.0f;  // criminal economy's share of activity
    float formal_employment_rate = 0.0f;    // working-age in formal/taxed employment

    // New in schema v5:
    float sick_rate = 0.0f;          // fraction whose health is below the
                                     // illness threshold (config.healthcare.
                                     // illness_health_threshold, default 0.4).
                                     // Updated by healthcare module.
    float homeless_rate = 0.0f;      // fraction without stable housing.
                                     // Owner module TBD; field allocated so
                                     // consumers can read without re-plumbing.
    float unemployment_rate = 0.0f;  // working-age neither in formal nor
                                     // informal employment. Updated by
                                     // labor_market; distinct from
                                     // `formal_employment_rate` (which
                                     // excludes the informal economy).

    // War casualties for this province this year, as an EXTRA annual death fraction
    // of the population (real units: battle dead / people). Published by the warfare
    // module and consumed by population_aging in the SAME annual tick (warfare
    // declares runs_before population_aging — the ordering is a contract, not a
    // tie-break accident). The dead are debited to the provinces that raised the
    // levy, each bounded by the soldiers it fielded, so this stays within
    // [0, levy_fraction]. 0 = at peace. The publisher resets it on regime exit.
    // Not persisted, and it does not need to be: it is produced and consumed
    // inside one tick, so no save can ever fall between the two.
    float war_death_fraction = 0.0f;

    // Per-province territorial-conflict intensity (0-5), the demand-relevant
    // maximum of TerritorialConflictStage across the criminal organizations
    // operating in this province. Published each tick by criminal_operations
    // from the real org conflict_state (resolution maps to 0 — post-conflict, no
    // demand). Read by weapons_trafficking as the grounded conflict-demand driver
    // (replaces the criminal_dominance_index proxy). Transient: recomputed every
    // tick from the persisted orgs, so it is NOT serialized (defaults to 0 = none
    // on a fresh load, repopulated on the first tick).
    uint8_t territorial_conflict_stage = 0;

    // Subsistence food surplus: produced / needed for the whole province
    // population this tick, written by the subsistence (commons) module in
    // pre-market eras. 1.0 = exactly fed; >1.0 = a surplus that frees labor for
    // specialists/trade; <1.0 = a deficit. Defaults to 1.0 ("fed") so the field
    // is behaviour-neutral when the module is inert (modern, market-fed eras).
    float subsistence_surplus_ratio = 1.0f;

    // --- Grain logistics (the tyranny of the ox; medieval band §3.5) ---
    // Absolute haulable grain surplus this tick (output - need), published by the
    // subsistence module — the grain available to move / feed non-farmers. Zero
    // without real surplus. Transient (recomputed each tick; not persisted).
    float grain_surplus = 0.0f;
    // Per-province NET FEEDABLE SURPLUS: own grain_surplus plus what neighbours can
    // deliver across ProvinceLinks before the draft teams eat it (water >> land).
    // Published by grain_logistics; the catchment surplus a town/castle can draw on.
    // Transient (recomputed each tick; not persisted).
    float net_feedable_surplus = 0.0f;
    // URBAN CARRYING CAPACITY: how many townsfolk the catchment could feed
    // (= net_feedable_surplus / per-capita food, bounded by total_population),
    // published by grain_logistics — river hubs can grow towns; stranded inland
    // cannot. This is a CAPACITY, not a headcount: it sets how hard the town pulls
    // migrants off the land, and the town's actual size (urban_population) sits
    // BELOW it because towns bury more people than they christen. Transient
    // (recomputed each tick; not persisted).
    float urban_capacity = 0.0f;
    // Share of the population NOT working the land — the stratum the food surplus
    // frees (published by subsistence, which computes it from population minus the
    // farmers the harvest needs, ceilinged by the regime). A real located stratum of
    // people, not a sample: the learned/knowledge-keeping share is drawn from it, so
    // a society's capacity to advance scales with the people it can spare.
    float specialist_fraction = 0.0f;

    // The province's WRITTEN CORPUS: knowledge committed to records that outlive the
    // people who wrote them. Held per province because dispersion is what actually
    // preserves knowledge — a single-copy text dies when one archive burns, while a
    // work in many houses survives. Roughly 90% of classical Latin literature was lost
    // between 500 and 900 CE; what survived did so by being copied widely.
    //
    // This is the RATCHET. Tacit knowledge dies with the learned stratum (see
    // knowledge_module), but records do not, so each cycle of rise and fall can start
    // from what the last one wrote down rather than from nothing.
    float codified_knowledge = 0.0f;

    // Fertility of the land the commons works, as a fraction of pristine (1.0 = never
    // worked out). A REAL stock: continuous cropping strips nutrients faster than they
    // return, fallow and low pressure let them rebuild. This is the one thing that can
    // make the carrying ceiling FALL — knowledge only ever raises it — and it is why
    // societies that grow into their land and keep pressing it collapse. Rome's grain
    // provinces and the Maya lowlands are the cases.
    float soil_health = 1.0f;

    // Productive capital the province has BUILT: tools, ploughs, granaries, kilns,
    // cleared and drained land, workshops, roads. A real accumulated stock, not a
    // signal — it is invested out of the food surplus at a physical rate and wears
    // out every year. This is the difference between knowing something and being
    // able to do it: knowledge can spike (information is cheap to copy) while the
    // capacity to USE it can only be built. Era advancement gates on both.
    float productive_capital = 0.0f;

    // The town's ACTUAL size: sum of the urban cohorts (youth_urban, working_urban_*,
    // retiree_urban). A real headcount of located people, derived from the cohorts
    // whenever population_aging republishes them — not a capacity estimate.
    //
    // THE URBAN GRAVEYARD. Before sanitation, towns buried more people than they
    // christened: crowding turned wells into sewers and every child met every disease.
    // London's burials exceeded its baptisms in almost every year of the 17th and 18th
    // centuries, yet it grew — entirely on migrants walking in from the countryside.
    // So a town is a standing flow, not a stock that accumulates: it holds its size
    // only while the land around it has both spare grain and spare people, and it
    // empties when either fails. That is why pre-industrial urbanisation sat near 10%
    // however rich the society got, and why it could only break out once medicine and
    // sewers closed the grave.
    float urban_population = 0.0f;

    // Signed net grain flowing into this province's granary per tick, published by
    // grain_logistics: the same conserved diffusion as the food_store delta, exposed so
    // the food balance can see it. Positive means fed from elsewhere, negative means
    // feeding elsewhere. Transient (recomputed each tick).
    float grain_import_rate = 0.0f;

    // How much of what this province eats came from somewhere else (R3D). Sea and river
    // transport decoupled cities from their own hinterland — Egypt shipped on the order of
    // 130,000 tonnes of grain a year to Rome, and moving grain 70 miles by road cost more
    // than sailing it 1,400 — and that is a bargain with a bill attached. A province fed
    // by its neighbours prospers beyond its own land right up until the route fails, and
    // then starves in proportion to how far beyond it had grown.
    //
    // The Late Bronze Age collapse is the case: a small-world trade network in which
    // severing Ugarit cut Cyprus off from tin and copper, and the whole eastern
    // Mediterranean system came down within a generation. Transient (recomputed each tick).
    float import_dependence = 0.0f;

    // WHAT THIS PLACE KNOWS (R6). Accumulated practical knowledge held HERE — technique,
    // geometry, the calendar, letters, metallurgy — as distinct from the frontier of what
    // anyone in the world has achieved.
    //
    // Knowledge was a single global number, and that turned out to be the deepest reason
    // no civilisation could ever fall. With one number for the whole world there is no
    // such thing as one society collapsing while another rises: there is one society with
    // six provinces, and the only trajectory available to it is the world's. But every
    // fall the record actually contains is REGIONAL — Mycenaean Greece lost literacy for
    // four centuries while Egypt and Assyria carried on writing; the Maya lowlands emptied
    // while the highlands did not; Rome's west fell and its east did not.
    //
    // Knowledge is NOT CONSERVED, unlike grain: a province learns from its neighbour
    // without the neighbour forgetting. So it diffuses along the same links that carry
    // trade and administration, which is how a dark region relearns — Greek texts came
    // back to Europe through Arabic.
    //
    // A real stock, persisted (schema v30). `technology.knowledge_level` is now the MAXIMUM
    // over provinces: the frontier, which is what an era is dated by in the first place.
    float knowledge_level = 0.0f;

    // What people here are USED TO eating — a slow average of the real wage over living
    // memory, about a generation. Immiseration is measured against THIS, not against bare
    // subsistence, and the distinction turns out to decide whether a society can fall at
    // all.
    //
    // A population whose numbers track its food supply is never absolutely starving: the
    // wage valve sees to that, so measured against subsistence the mobilisation term was
    // exactly zero for the entire climb and the Political Stress Index with it. But
    // Turchin's variable is the real wage against trend, and the standard finding — Davies'
    // J-curve — is that revolutions follow REVERSALS after improvement rather than steady
    // poverty. A people whose living standards have halved in fifty years is in exactly
    // the political condition that brings a state down, however well fed it would have
    // looked to its own grandparents.
    //
    // A real slow stock, persisted (schema v29). Published by structural_demography.
    float wage_reference = 1.0f;

    // Signed net people arriving here this year because somewhere else failed (R5).
    // Positive means refugees arriving, negative means the province emptying. Conserved
    // across the world: what leaves one place arrives at another.
    //
    // This is how a collapse crosses a border. A province in famine exports its crisis to
    // its neighbours as people; their surplus falls under the extra mouths, their own
    // stress rises, and they begin exporting in turn. The Migration Period, the Sea
    // Peoples, the Irish famine. Published by structural_demography, applied to the
    // cohorts by population_aging. Transient (recomputed each tick).
    float refugee_flow = 0.0f;

    // Which polity this province currently belongs to (R3F). Emergent and published by
    // warfare, which owns the political map: a province that has never been conquered is
    // its own polity, conquest absorbs whole polities, and hold-failure secedes them back.
    //
    // Exposed here because POLITICAL FRAGMENTATION IS AN INPUT TO SURVIVAL, not just a
    // military fact. An idea suppressed or burned in one jurisdiction survives in the
    // next: Tyndale printed in Antwerp, Galileo circulated in the Netherlands, and the
    // reason Europe's scientific revolution could not be stopped is that there was nobody
    // in a position to stop it everywhere at once. A unified empire has no such refuge.
    // Transient (recomputed each tick).
    uint32_t polity_id = 0;

    // ASABIYA — the capacity of a people to act together (R3C). Ibn Khaldun's
    // observation, and the one Turchin turned into a model: solidarity is FORGED AT
    // FRONTIERS, where a group lives against an out-group and has to hold together or
    // die, and it DECAYS IN THE INTERIOR, where safety makes it unnecessary. That is why
    // frontier peoples repeatedly conquer settled empires, and why the empires they
    // build then soften in the same place their founders came from.
    //
    // It is what a polity fights with, over and above headcount, so it flows into
    // everything warfare already does: who attacks, who wins, what holds together and
    // what secedes. Mechanically it matters because it is a SECOND OSCILLATOR with its
    // own period, driven by geography rather than by the harvest — so provinces stop
    // rising and falling in unison and the world gets a history instead of one global
    // sawtooth.
    //
    // A real slow stock, persisted (schema v28). Seeded above zero because the growth
    // term is logistic: a people with exactly no solidarity could never develop any.
    float asabiya = 0.25f;

    // The share of the population that has never met the plague and would take it if it
    // came. A REAL STOCK, and the thing that turns one epidemic into a century and a
    // half of suppression.
    //
    // England fell from 4.8M in 1348 to 2.6M by 1351 and then kept falling, reaching its
    // nadir of 1.9M around 1450 — a hundred years AFTER the Black Death. Recovery took
    // that long not because one plague was so severe but because plague came back: 1361,
    // 1369, 1375, 1390, 1400, and on into the 17th century. Each wave was less lethal
    // than the last because it found fewer people who had never had it, and each
    // interval was long enough for a new generation of susceptibles to be born.
    //
    // So the recurrence interval and the declining lethality are not modelled directly —
    // they fall out of one stock being drawn down by outbreaks and refilled by
    // population turnover. Persisted (schema v27): losing it on load would hand a
    // society a clean slate its history says it should not have.
    float plague_susceptible_fraction = 1.0f;

    // The non-farming stratum THIS YEAR'S HARVEST can support, before the generational
    // inertia that makes `specialist_fraction` lag it (R2C). Published by subsistence
    // alongside the held stratum, because the GAP between the two is what structural
    // demography measures: the people in it were raised to expect a place the land no
    // longer provides. Transient (recomputed each tick).
    float supported_specialist_fraction = 0.0f;

    // POLITICAL STRESS INDEX (R2D): Turchin's structural-demographic measure of how
    // close a society is to coming apart for reasons that have nothing to do with the
    // weather. Multiplicative in popular immiseration, elite overproduction and fiscal
    // exhaustion — all three must be elevated at once, which is why most bad years are
    // just bad years. Published by structural_demography; transient.
    float political_stress = 0.0f;
    // Extra annual death fraction from FACTIONAL CONFLICT — surplus claimants and their
    // retinues fighting each other. Real units (dead / population), consumed by
    // population_aging as an independent competing risk exactly like war_death_fraction,
    // from which it is deliberately separate: one is a war between polities, this is a
    // polity coming apart from the inside. Published by structural_demography.
    float faction_death_fraction = 0.0f;

    // GHOST ACRES (R1B): the extra land, as a fraction of the province's own surface,
    // that burning coal stands in for this year. An organic economy is bounded by
    // photosynthesis on finite acres — food, fodder, firewood and charcoal all compete
    // for the same ground — and coal breaks that bound by substituting a stock for the
    // flow. England and Wales drew 4.3M acre-equivalents from coal in 1750 and 48.1M by
    // 1850, more than their entire ~37M-acre land surface.
    //
    // This is the only channel that can raise the carrying ceiling without limit, and so
    // the only escape from a fixed peak height. It is also temporary by construction:
    // the coal is a finite located deposit, so a society that industrialises is spending
    // something, and when the seam is worked out the ceiling falls back to what the sun
    // puts on its fields. Published by energy_base; transient (recomputed each tick).
    float ghost_land_fraction = 0.0f;
    // Tonnes of coal the province actually raised and burned in the last year — the flow
    // behind the ghost acres, and exactly what came out of its deposits. Published by
    // energy_base; transient.
    float coal_burned_per_year = 0.0f;

    // Granary: the province's stored food (a conserved, located resource). The
    // commons food economy banks each year's production surplus here, up to a finite
    // capacity, and draws it down in deficit years — so a bad harvest doesn't
    // immediately starve the population (or its non-farming specialists). It is the
    // grounded buffer that lets a knowledge-elite survive lean years instead of a
    // hand-placed floor. Units: food (same as one tick's production/consumption);
    // empty (0) means no reserves. Behaviour-neutral in market eras (not updated).
    float food_store = 0.0f;

    // Population hardiness: how adapted this people is to its world's hazards
    // (Earth-normalized, 1.0 = Earth-adapted). A people native to a harsh world has
    // high hardiness; one bred on a gentle world, low. It DRIFTS over generations
    // toward the world's hazard level. Mortality from hazards scales with how far
    // hardiness falls short of what the world demands — so a soft people transplanted
    // onto a hard world suffer until they adapt, while natives cope.
    float hardiness = 1.0f;

    // Zero all fields. Use after construction or when resetting a province.
    void reset() { *this = RegionCohortStats{}; }
};

}  // namespace econlife
