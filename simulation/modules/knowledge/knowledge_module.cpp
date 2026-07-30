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

    // ------------------------------------------------------------------------------
    // KNOWLEDGE IS HELD SOMEWHERE (R6). Every province accumulates, forgets and learns
    // on its own, and the world's `knowledge_level` is the MAXIMUM over them: the
    // frontier, which is what an era is dated by in the first place — the Bronze Age is
    // dated by whoever had bronze.
    //
    // It was one global number, and that was the deepest reason no civilisation could
    // ever fall. With a single figure for the whole world there is no such thing as one
    // society collapsing while another rises: there is one society with six provinces,
    // and the only trajectory available to it is the world's. Every fall the record
    // actually contains is REGIONAL — Mycenaean Greece lost literacy for four centuries
    // while Egypt and Assyria carried on writing, the Maya lowlands emptied while the
    // highlands did not, Rome's west fell and its east did not.
    // ------------------------------------------------------------------------------
    const uint32_t n = static_cast<uint32_t>(state.provinces.size());
    double total_population = 0.0;
    for (const auto& p : state.provinces)
        if (p.cohort_stats)
            total_population += static_cast<double>(p.cohort_stats->total_population);

    const float world_hazard = hazard_mortality_from_settings(state.hazard_settings);
    const float knowledge_mult =
        state.tech_effects_for_era(state.technology.current_era).knowledge_mult;

    // Who produces knowledge HERE, and how much each of them is worth.
    std::vector<double> keepers(n, 0.0);
    std::vector<double> produced(n, 0.0);
    std::vector<float> pressure_of(n, 1.0f);
    double most_keepers = 0.0;
    uint32_t leap_province = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const auto& prov = state.provinces[i];
        if (!prov.cohort_stats)
            continue;
        const RegionCohortStats& cs = *prov.cohort_stats;
        const double pop = static_cast<double>(cs.total_population);
        const double freed = static_cast<double>(cs.specialist_fraction);
        keepers[i] = pop * freed * static_cast<double>(cfg_.learned_share_of_specialists);
        if (keepers[i] > most_keepers) {
            most_keepers = keepers[i];
            leap_province = i;
        }

        // Adversity drives invention (Boserup intensification + the Deathworlders
        // premise): under pressure the WHOLE population innovates, not just a thin elite.
        // Scarcity is the PROVINCE's own now — a place pressing on its own land
        // intensifies, and its comfortable neighbour does not.
        const float scarcity =
            std::clamp(1.0f - cs.subsistence_surplus_ratio, 0.0f, 1.0f);
        const float pressure = std::clamp(
            cfg_.adversity_base +
                cfg_.adversity_hazard_weight *
                    std::max(0.0f, world_hazard - cfg_.adversity_garden_hazard) +
                cfg_.adversity_scarcity_weight * scarcity,
            0.0f, cfg_.adversity_pressure_cap);
        pressure_of[i] = pressure;

        const double specialist_term = keepers[i] * static_cast<double>(per_worker_output) *
                                       static_cast<double>(cfg_.production_scalar);
        const double population_term =
            static_cast<double>(cfg_.population_innovation_rate) * pop;
        produced[i] = (specialist_term + population_term) * static_cast<double>(pressure) *
                      static_cast<double>(knowledge_mult);

        // IDEAS GET HARDER TO FIND, against what THIS place already knows. The easy
        // discoveries are made first and every one made leaves the next harder: American
        // research productivity has fallen roughly 41-fold since the 1930s while
        // researcher numbers rose more than twentyfold. A province at the frontier finds
        // the going harder than one still catching up — which is exactly why catching up
        // is faster than leading.
        produced[i] /= discovery_difficulty(cs.knowledge_level, cfg_);
    }

    // EXCEPTIONAL INDIVIDUALS — the Socrates/Newton/Einstein term. Ordinary knowledge
    // work is incremental; rare minds make LEAPS. The chance one arises scales with how
    // many people are doing knowledge work anywhere (more minds, more chances, which is
    // why geniuses cluster in large literate societies), and arrives as a physical
    // first-arrival probability p = 1 - exp(-rate) rather than a flat die roll.
    //
    // A leap is ONE MIND's work and it happens in ONE PLACE — the province with the most
    // knowledge workers, deterministically — so it lifts that province's knowledge and
    // reaches its neighbours only by diffusion, as Newton's work reached the continent.
    const uint32_t year = state.current_tick / kTicksPerYear;
    double world_keepers = 0.0;
    for (uint32_t i = 0; i < n; ++i)
        world_keepers += keepers[i];
    uint32_t leap_npc_id = 0;
    if (world_keepers > 0.0 && cfg_.genius_rate_per_worker_year > 0.0f && n > 0) {
        const double rate =
            static_cast<double>(cfg_.genius_rate_per_worker_year) * world_keepers;
        const double p_leap = 1.0 - std::exp(-rate);
        DeterministicRNG genius_rng(state.world_seed ^
                                    (static_cast<uint64_t>(year) * 0x9E3779B97F4A7C15ull) ^
                                    0x9E17A5ull);
        if (static_cast<double>(genius_rng.next_float()) < p_leap) {
            const auto* host = state.provinces[leap_province].cohort_stats.get();
            // Under the same necessity as everybody else: a mind works on the problems
            // its world is pressing on it, which is why hard times produce hard thinking.
            double leap = static_cast<double>(cfg_.genius_equivalent_workers) *
                          static_cast<double>(per_worker_output) *
                          static_cast<double>(cfg_.production_scalar) *
                          static_cast<double>(cfg_.genius_leap_years) *
                          static_cast<double>(pressure_of[leap_province]) *
                          static_cast<double>(knowledge_mult);
            // The frontier recedes for geniuses too: Newton had harder problems available
            // than Archimedes, and Einstein harder ones than Newton.
            if (host != nullptr)
                leap /= discovery_difficulty(host->knowledge_level, cfg_);
            produced[leap_province] += leap;

            // Attribute it to a LIVING tracked individual when there is one — the leap
            // belongs to a named person, not to an anonymous aggregate. Deterministic
            // pick: the lowest-id living knowledge-keeper, else the lowest-id living adult.
            for (const auto& npc : state.significant_npcs) {
                const bool gone = npc.status == NPCStatus::dead ||
                                  npc.status == NPCStatus::fled ||
                                  npc.status == NPCStatus::imprisoned;
                if (gone)
                    continue;
                const OccupationDefinition* o =
                    npc.occupation != 0 ? state.occupation_catalog.by_index(npc.occupation) : nullptr;
                if (o != nullptr && o->knowledge_output > 0.0f) {
                    leap_npc_id = npc.id;
                    break;
                }
                if (leap_npc_id == 0)
                    leap_npc_id = npc.id;  // fallback: any living tracked person
            }
        }
    }

    // WHAT EACH PLACE CAN CARRY. Knowledge lives in people and, later, in records: a
    // province holds only what its own learned stratum and its own archives can sustain,
    // and forgets the rest. This is how a region REGRESSES while its neighbours do not —
    // Rome's aqueducts outlived the engineers who could maintain them.
    //
    // Both the stratum and the corpus are LOCAL now. A province that empties loses what
    // it knew even if the world still knows it, and gets it back only by learning again
    // from somebody who still does.
    std::vector<double> net(n, 0.0);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& prov = state.provinces[i];
        if (!prov.cohort_stats)
            continue;
        const RegionCohortStats& cs = *prov.cohort_stats;
        const double local = static_cast<double>(cs.knowledge_level);
        const double stratum_sustains = keepers[i] * static_cast<double>(per_worker_output) *
                                        static_cast<double>(cfg_.knowledge_sustained_per_output_unit);
        const double codified = static_cast<double>(cs.codified_knowledge);
        const double sustainable = std::max(stratum_sustains, codified);
        const double unsustainable = std::max(0.0, local - sustainable);
        const double forgetting =
            unsustainable * static_cast<double>(cfg_.forgetting_rate_per_year);
        const double decay = static_cast<double>(cfg_.decay_per_year) * local;
        net[i] = produced[i] - decay - forgetting;
    }

    // KNOWLEDGE TRAVELS, AND IS NOT CONSERVED (R6). A province learns from any
    // better-informed neighbour it can reach, and the neighbour forgets nothing —
    // copying a text leaves the original. This is what lets a dark region relearn
    // rather than start from nothing: Greek mathematics came back to western Europe
    // through Arabic translation, centuries after the western libraries had gone.
    if (cfg_.knowledge_diffusion_rate_per_year > 0.0f && n > 1) {
        const auto h3_to_idx = build_h3_to_province_index(state.provinces);
        const double rate = static_cast<double>(cfg_.knowledge_diffusion_rate_per_year);
        for (uint32_t i = 0; i < n; ++i) {
            const auto& prov = state.provinces[i];
            if (!prov.cohort_stats)
                continue;
            const double mine = static_cast<double>(prov.cohort_stats->knowledge_level);
            for (const auto& link : prov.links) {
                auto it = h3_to_idx.find(link.neighbor_h3);
                if (it == h3_to_idx.end() || it->second == i)
                    continue;
                const auto& other = state.provinces[it->second];
                if (!other.cohort_stats)
                    continue;
                const double theirs = static_cast<double>(other.cohort_stats->knowledge_level);
                if (theirs > mine)
                    net[i] += (theirs - mine) * rate;  // learning, not transfer
            }
        }
    }

    // Publish what each place now knows, and set the world's frontier to the best of them.
    double frontier = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        const auto& prov = state.provinces[i];
        if (!prov.cohort_stats)
            continue;
        if (net[i] != 0.0) {
            RegionDelta rd{};
            rd.region_id = prov.region_id;
            rd.province_knowledge_delta = static_cast<float>(net[i]);
            delta.region_deltas.push_back(rd);
        }
        frontier = std::max(frontier,
                            std::max(0.0, static_cast<double>(prov.cohort_stats->knowledge_level) +
                                              net[i]));
    }

    const float level = state.technology.knowledge_level;

    TechnologyDelta td{};
    // The world's figure is the frontier — the most any single society knows. Emitted as
    // the additive step that reaches it, because that is the channel available.
    const auto frontier_step = static_cast<float>(frontier - static_cast<double>(level));
    if (frontier_step != 0.0f)
        td.knowledge_delta = frontier_step;

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
        era->knowledge_to_advance <= 0.0f || frontier >= static_cast<double>(era->knowledge_to_advance);
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
            frontier < static_cast<double>(entered_from->knowledge_to_advance) *
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
                printing_copy_mult(static_cast<float>(frontier), cfg_);
            // Cannot record what is not known HERE: a province's corpus tends toward its
            // own living knowledge, never past it. A scribe cannot copy a book that has
            // not reached his city.
            const double room =
                std::max(0.0, static_cast<double>(p.cohort_stats->knowledge_level) - held);
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
    if (leap_npc_id != 0) {
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
