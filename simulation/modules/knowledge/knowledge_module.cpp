#include "modules/knowledge/knowledge_module.h"

#include <algorithm>
#include <cmath>

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/era_catalog.h"
#include "core/world_gen/occupation_catalog.h"
#include "core/world_gen/world_class.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/world_state.h"

namespace econlife {

namespace {
}

bool KnowledgeModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

void KnowledgeModule::execute(const WorldState& state, DeltaBuffer& delta) {
    // Annual cadence (knowledge accrues slowly); skip the t=0 snapshot tick.
    if (state.current_tick == 0 || state.current_tick % kTicksPerYear != 0)
        return;

    // Pre-market only: in market eras the technology module owns advancement, and
    // there are no scholar livelihoods anyway. Leaves knowledge_level untouched.
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    // WHO ADVANCES A SOCIETY: a SHARE OF THE POPULATION, not a handful of tracked
    // individuals. The food surplus frees a stratum from the land (subsistence
    // publishes it as cohort_stats->specialist_fraction — real people, computed as
    // population minus the farmers the harvest needs), and a few percent of that
    // stratum are the knowledge-keepers: elders, then scribes, then scholars as the
    // eras allow. Their number therefore grows with the population and with the
    // surplus, which is why history accelerates as societies get larger and better fed.
    //
    // This replaced a sum over significant_npcs' occupations. That sum was a fixed
    // ~200-person sample: its members aged and died with nothing to replace them, so
    // the living corps drained to zero within a century and the climb died with it
    // (it had only ever "worked" because dead NPCs kept producing — occupations
    // persist on dead records). A stratum of the living population cannot die out
    // while the population lives.
    //
    // Per-worker output is era-gated by the occupation catalog: the best
    // knowledge-bearing livelihood the era has unlocked (elder 0.2 at the dawn,
    // scribe 0.6 with writing, scholar 1.0 with formal scholarship).
    float per_worker_output = 0.0f;
    for (uint16_t i = 1;; ++i) {
        const OccupationDefinition* o = state.occupation_catalog.by_index(i);
        if (o == nullptr)
            break;
        if (o->knowledge_output > 0.0f && o->min_era <= state.technology.current_era)
            per_worker_output = std::max(per_worker_output, o->knowledge_output);
    }

    double knowledge_workers = 0.0;
    for (const auto& p : state.provinces) {
        if (!p.cohort_stats)
            continue;
        const double pop = static_cast<double>(p.cohort_stats->total_population);
        const double freed = static_cast<double>(p.cohort_stats->specialist_fraction);
        knowledge_workers += pop * freed * static_cast<double>(cfg_.learned_share_of_specialists);
    }
    double specialist_term = knowledge_workers * static_cast<double>(per_worker_output);
    specialist_term *= static_cast<double>(cfg_.production_scalar);

    // Adversity drives invention (Boserup intensification + the Deathworlders premise):
    // under pressure the WHOLE population innovates, not just a thin elite. Pressure
    // rises with the world's hazard (a hard world forges capability) and with food
    // scarcity (a population pressing on its supply intensifies — necessity is the
    // mother of invention). This scales with total population (more minds) and gives a
    // grounded escape from the Malthusian wall, and makes harsher worlds out-innovate
    // comfortable gardens.
    double total_population = 0.0;
    double weighted_surplus = 0.0;
    for (const auto& p : state.provinces) {
        if (!p.cohort_stats)
            continue;
        const double pop = static_cast<double>(p.cohort_stats->total_population);
        total_population += pop;
        weighted_surplus += pop * static_cast<double>(p.cohort_stats->subsistence_surplus_ratio);
    }
    const double avg_surplus = total_population > 0.0 ? weighted_surplus / total_population : 1.0;
    const float world_hazard = hazard_mortality_from_settings(state.hazard_settings);
    const float scarcity = std::clamp(1.0f - static_cast<float>(avg_surplus), 0.0f, 1.0f);
    const float pressure = std::clamp(
        cfg_.adversity_base +
            cfg_.adversity_hazard_weight * std::max(0.0f, world_hazard - cfg_.adversity_garden_hazard) +
            cfg_.adversity_scarcity_weight * scarcity,
        0.0f, cfg_.adversity_pressure_cap);
    const double population_term =
        static_cast<double>(cfg_.population_innovation_rate) * total_population;

    // Total: dedicated work + diffuse population innovation, both lifted by pressure and
    // compounded by the tech tree (writing/printing/scientific-method "learning to learn").
    double production = (specialist_term + population_term) * static_cast<double>(pressure);
    production *= static_cast<double>(
        state.tech_effects_for_era(state.technology.current_era).knowledge_mult);

    // EXCEPTIONAL INDIVIDUALS — the Socrates/Newton/Einstein term. Ordinary knowledge
    // work is incremental; rare minds make LEAPS. The chance one arises in a given year
    // scales with how many people are doing knowledge work (more minds, more chances,
    // which is why geniuses cluster in large literate societies), and arrives as a
    // physical first-arrival probability p = 1 - exp(-rate) — never a flat die roll.
    //
    // A leap is ONE MIND's work: genius_equivalent_workers ordinary keepers for
    // genius_leap_years, at this era's per-worker output. It is therefore lifted by the
    // era's institutions and by the accumulated tech multiplier, but NOT by the size of
    // the society — a genius is one person however large the civilisation, so leaps
    // matter enormously in a small scholarly community and are a smaller share of a
    // vast one. Seeded by YEAR so the draw is stable at any tick resolution.
    const uint32_t year = state.current_tick / kTicksPerYear;
    double leap = 0.0;
    uint32_t leap_npc_id = 0;
    if (knowledge_workers > 0.0 && production > 0.0 && cfg_.genius_rate_per_worker_year > 0.0f) {
        const double rate =
            static_cast<double>(cfg_.genius_rate_per_worker_year) * knowledge_workers;
        const double p_leap = 1.0 - std::exp(-rate);
        DeterministicRNG genius_rng(state.world_seed ^
                                    (static_cast<uint64_t>(year) * 0x9E3779B97F4A7C15ull) ^
                                    0x9E17A5ull);
        if (static_cast<double>(genius_rng.next_float()) < p_leap) {
            leap = static_cast<double>(cfg_.genius_equivalent_workers) *
                   static_cast<double>(per_worker_output) *
                   static_cast<double>(cfg_.production_scalar) *
                   static_cast<double>(cfg_.genius_leap_years) * static_cast<double>(pressure);
            // Attribute it to a LIVING tracked individual when there is one — the leap
            // belongs to a named person, not to an anonymous aggregate. Deterministic
            // pick: the lowest-id living knowledge-keeper, else the lowest-id living
            // adult. The leap itself comes from the learned population, so it still
            // happens when the tracked layer holds nobody suitable.
            for (const auto& npc : state.significant_npcs) {
                const bool gone = npc.status == NPCStatus::dead ||
                                  npc.status == NPCStatus::fled ||
                                  npc.status == NPCStatus::imprisoned;
                if (gone)
                    continue;
                const OccupationDefinition* o =
                    npc.occupation != 0 ? state.occupation_catalog.by_index(npc.occupation) : nullptr;
                const bool keeper = o != nullptr && o->knowledge_output > 0.0f;
                if (keeper) {
                    leap_npc_id = npc.id;
                    break;
                }
                if (leap_npc_id == 0)
                    leap_npc_id = npc.id;  // fallback: any living tracked person
            }
        }
    }
    production += leap * static_cast<double>(
                             state.tech_effects_for_era(state.technology.current_era).knowledge_mult);

    const float level = state.technology.knowledge_level;

    // IDEAS GET HARDER TO FIND. Everything above — the stratum's work, the diffuse
    // population innovation, and the leaps — buys less the more a society already knows,
    // because the easy discoveries are made first and every one made leaves the next one
    // harder. American research productivity has fallen roughly 41-fold since the 1930s
    // while the number of researchers rose more than twenty-fold; sustaining Moore's law
    // now takes eighteen times the effort it took in 1971. This is why producing vastly
    // more data than fifty years ago has not bought flying cars: knowledge and the
    // difficulty of the next step both grow.
    //
    // It applies to the leaps too. Newton had harder problems available to him than
    // Archimedes did, and Einstein harder ones than Newton — a genius is one mind
    // working at the frontier of the day, and the frontier keeps receding.
    //
    // This is the natural limiter on advancement speed, and it is a mechanism, not a
    // brake: nothing here caps anything, the next discovery simply costs more than the
    // last. Without it the whole climb happened in a single near-vertical spike.
    production /= discovery_difficulty(level, cfg_);

    // WHAT THE SOCIETY CAN CARRY. Knowledge lives in people and, later, in records: a
    // society holds only what its learned stratum and institutions can sustain, and
    // forgets the rest. This is how civilisations REGRESS — Rome's aqueducts outlived
    // the engineers who could maintain them, and the Maya cities outlived the surplus
    // that fed their scribes. A healthy, growing society sustains far more than it
    // holds and forgets nothing; only a collapse in population or surplus pushes
    // holdings above the people left to carry them.
    //
    // Writing is the ratchet: the per-worker term rises elder -> scribe -> scholar, so
    // a literate society keeps far more through a collapse than an oral one, and each
    // cycle of rise and fall can start higher than the last.
    const double stratum_sustains = knowledge_workers * static_cast<double>(per_worker_output) *
                                    static_cast<double>(cfg_.knowledge_sustained_per_output_unit);

    // WRITTEN RECORDS. What a society has committed to durable media does not die with
    // the people who wrote it, so the corpus is a FLOOR under forgetting: a collapse can
    // scatter the scholars and empty the cities without erasing the books. This is the
    // ratchet — each cycle of rise and fall starts from what the last one wrote down.
    double codified = 0.0;
    for (const auto& p : state.provinces)
        if (p.cohort_stats)
            codified += static_cast<double>(p.cohort_stats->codified_knowledge);

    const double sustainable = std::max(stratum_sustains, codified);
    const double unsustainable = std::max(0.0, static_cast<double>(level) - sustainable);
    const double forgetting = unsustainable * static_cast<double>(cfg_.forgetting_rate_per_year);

    const double decay = static_cast<double>(cfg_.decay_per_year) * static_cast<double>(level);
    const float net = static_cast<float>(production - decay - forgetting);

    TechnologyDelta td{};
    if (net != 0.0f)
        td.knowledge_delta = net;

    // ERA ADVANCEMENT needs TWO things, and they are not the same thing.
    //
    // Knowing how to make bronze is not the Bronze Age; having the smelters, the ore
    // trade and the smiths is. A society advances only when it BOTH knows enough
    // (accumulated knowledge) AND has built enough to use what it knows (productive
    // capital per head: tools, kilns, cleared land, workshops). Knowledge is
    // information — it can spike, and historically does; capital is matter and labour,
    // accumulated out of a real food surplus at a physical rate, and it wears out.
    // A society can know far more than it can build, which is exactly why enormous
    // modern data output has not produced flying cars.
    //
    // Both gates are data-driven per era (eras.csv). A zero threshold means that era
    // is not gated on that axis.
    double capital_stock = 0.0;
    for (const auto& p : state.provinces)
        if (p.cohort_stats)
            capital_stock += static_cast<double>(p.cohort_stats->productive_capital);
    const double capital_per_head =
        total_population > 0.0 ? capital_stock / total_population : 0.0;

    const bool knows_enough =
        era->knowledge_to_advance <= 0.0f || level >= era->knowledge_to_advance;
    const bool can_build_it =
        era->capital_to_advance <= 0.0f || capital_per_head >= era->capital_to_advance;
    if ((era->knowledge_to_advance > 0.0f || era->capital_to_advance > 0.0f) && knows_enough &&
        can_build_it) {
        const uint8_t max_era = state.era_catalog.max_era();
        if (state.technology.current_era < max_era)
            td.new_era = static_cast<uint8_t>(state.technology.current_era + 1);
    } else if (state.technology.current_era > 1) {
        // THE FALL. A society that can no longer carry what it took to get here loses
        // the era: the works stand but nobody can build or maintain them any more.
        // Compared against the threshold that was needed to ENTER this era, with
        // hysteresis so a society on the edge does not flap year to year.
        //
        // This is what makes the climb a sawtooth rather than a ramp — civilisations
        // rise, build, overreach or are broken by famine, plague or war, and fall back,
        // and the next one starts from what survived. Forward-only advancement could
        // only ever model the first half of that.
        const EraDefinition* entered_from =
            state.era_catalog.by_index(static_cast<uint8_t>(state.technology.current_era - 1));
        if (entered_from != nullptr && entered_from->knowledge_to_advance > 0.0f &&
            static_cast<double>(level) <
                static_cast<double>(entered_from->knowledge_to_advance) *
                    static_cast<double>(cfg_.era_regression_hysteresis)) {
            td.new_era = static_cast<uint8_t>(state.technology.current_era - 1);
        }
    }

    if (td.knowledge_delta.has_value() || td.new_era.has_value())
        delta.technology_deltas.push_back(td);

    // SCRIBES AT WORK. Once an era has writing, the learned stratum commits part of what
    // the society knows to records, and the existing corpus decays slowly in keeping.
    // Copying is bounded by what is actually KNOWN — a scribe cannot write down more
    // than the civilisation has — and by how many scribes there are to do it.
    if (per_worker_output >= cfg_.writing_output_threshold) {
        // POLYCENTRISM (R3F). Count the INDEPENDENT jurisdictions that actually hold
        // records. An idea burned or suppressed in one survives in the next, so a
        // fragmented culture-area keeps what a unified one loses — and an empire that
        // absorbs its neighbours gains their levies and loses their refuges.
        std::vector<uint32_t> sheltering;
        for (const auto& p : state.provinces) {
            if (!p.cohort_stats || p.cohort_stats->codified_knowledge <= 0.0f)
                continue;
            const uint32_t pid = p.cohort_stats->polity_id;
            if (std::find(sheltering.begin(), sheltering.end(), pid) == sheltering.end())
                sheltering.push_back(pid);
        }
        const double shelter_divisor =
            shelter_loss_divisor(static_cast<uint32_t>(sheltering.size()), cfg_);

        for (const auto& p : state.provinces) {
            if (!p.cohort_stats)
                continue;
            const double pop = static_cast<double>(p.cohort_stats->total_population);
            const double freed = static_cast<double>(p.cohort_stats->specialist_fraction);
            const double local_keepers =
                pop * freed * static_cast<double>(cfg_.learned_share_of_specialists);
            const double held = static_cast<double>(p.cohort_stats->codified_knowledge);
            // THE PRESS (R3E). The same keepers, copying orders of magnitude faster
            // once movable type exists — which is what turns the corpus from a floor a
            // dark age can erode into one it cannot.
            const double copying =
                local_keepers * static_cast<double>(per_worker_output) *
                static_cast<double>(cfg_.codify_rate_per_worker_year) *
                printing_copy_mult(level, cfg_);
            // Cannot record what is not known: the province's corpus tends toward the
            // society's living knowledge, never past it.
            const double room = std::max(0.0, static_cast<double>(level) - held);
            const double added = std::min(copying, room);
            const double lost =
                held * static_cast<double>(cfg_.record_loss_per_year) / shelter_divisor;
            const float net_records = static_cast<float>(added - lost);
            if (net_records != 0.0f) {
                RegionDelta rd{};
                rd.region_id = p.region_id;
                rd.codified_knowledge_delta = net_records;
                delta.region_deltas.push_back(rd);
            }
        }
    } else {
        // No writing: the corpus decays with nobody able to recopy it. An oral culture
        // holding inherited records loses them.
        for (const auto& p : state.provinces) {
            if (!p.cohort_stats || p.cohort_stats->codified_knowledge <= 0.0f)
                continue;
            RegionDelta rd{};
            rd.region_id = p.region_id;
            rd.codified_knowledge_delta =
                -p.cohort_stats->codified_knowledge * cfg_.record_loss_per_year;
            delta.region_deltas.push_back(rd);
        }
    }

    // Record the leap on the person who made it, so a named individual is visibly
    // responsible for it in the historical record rather than it appearing as an
    // unexplained jump in the aggregate.
    if (leap > 0.0 && leap_npc_id != 0) {
        NPCDelta nd{};
        nd.npc_id = leap_npc_id;
        MemoryEntry m{};
        m.tick_timestamp = state.current_tick;
        m.type = MemoryType::event;
        m.subject_id = leap_npc_id;
        m.emotional_weight = 1.0f;  // the defining achievement of a life
        m.decay = 0.0f;             // a discovery is not forgotten
        m.is_actionable = false;
        nd.new_memory_entry = m;
        delta.npc_deltas.push_back(nd);
    }
}

}  // namespace econlife
