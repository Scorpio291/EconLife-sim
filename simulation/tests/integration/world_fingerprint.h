#pragma once

// World fingerprint — SAMPLED-STATE parity (EL-SLICE-01 AC-01, AC-05).
//
// Per-layer hashes over a chosen set of WorldState fields: provinces (conditions,
// community, population, natural capital), NPCs (status, role, capital, province,
// motivations, full memory log), businesses, facilities, markets, recipes,
// nations, era and the player. Floats are hashed by bit pattern, so the sampled
// fields are compared bit for bit.
//
// What it does NOT prove: equality of fields that are not listed here, or of
// module parameters. Two worlds with equal fingerprints are equal in the
// sampled fields only. Module-parameter equality is tested behaviourally (the
// worlds must stay equal through several ticks), not field by field. This is
// deliberately not a second serialiser; a full-state comparison belongs to the
// persistence layer (save files), not here.

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "core/world_state/player.h"
#include "core/world_state/world_state.h"

namespace econlife::fingerprint {

class Fnv {
   public:
    template <typename T>
    void pod(const T& v) {
        unsigned char buf[sizeof(T)];
        std::memcpy(buf, &v, sizeof(T));
        bytes(buf, sizeof(T));
    }
    void str(const std::string& s) {
        pod(s.size());
        bytes(reinterpret_cast<const unsigned char*>(s.data()), s.size());
    }
    uint64_t value() const { return h_; }

   private:
    void bytes(const unsigned char* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            h_ ^= p[i];
            h_ *= 1099511628211ull;
        }
    }
    uint64_t h_ = 1469598103934665603ull;
};

struct WorldFingerprint {
    uint64_t provinces = 0, npcs = 0, businesses = 0, facilities = 0, markets = 0, recipes = 0,
             nations = 0, player = 0, scalars = 0;
    std::size_t n_provinces = 0, n_npcs = 0, n_businesses = 0, n_facilities = 0, n_markets = 0,
                n_recipes = 0;

    bool operator==(const WorldFingerprint& o) const {
        return provinces == o.provinces && npcs == o.npcs && businesses == o.businesses &&
               facilities == o.facilities && markets == o.markets && recipes == o.recipes &&
               nations == o.nations && player == o.player && scalars == o.scalars &&
               n_provinces == o.n_provinces && n_npcs == o.n_npcs &&
               n_businesses == o.n_businesses && n_facilities == o.n_facilities &&
               n_markets == o.n_markets && n_recipes == o.n_recipes;
    }

    std::string describe() const {
        std::ostringstream o;
        o << std::hex << "prov=" << provinces << " npc=" << npcs << " biz=" << businesses
          << " fac=" << facilities << " mkt=" << markets << " rcp=" << recipes << " nat=" << nations
          << " ply=" << player << " sc=" << scalars << std::dec << " | counts prov=" << n_provinces
          << " npc=" << n_npcs << " biz=" << n_businesses << " fac=" << n_facilities
          << " mkt=" << n_markets << " rcp=" << n_recipes;
        return o.str();
    }
};

inline WorldFingerprint take(const WorldState& w) {
    WorldFingerprint f;
    {
        Fnv h;
        for (const auto& p : w.provinces) {
            h.pod(p.id);
            h.pod(p.conditions.stability_score);
            h.pod(p.conditions.inequality_index);
            h.pod(p.community.grievance_level);
            h.pod(p.community.cohesion);
            h.pod(p.agricultural_productivity);
            h.pod(p.geography.arable_land_fraction);
            h.pod(p.geography.forest_coverage);
            if (p.cohort_stats) {
                h.pod(p.cohort_stats->total_population);
                h.pod(p.cohort_stats->unemployment_rate);
                h.pod(p.cohort_stats->crime_rate);
            }
        }
        f.provinces = h.value();
        f.n_provinces = w.provinces.size();
    }
    {
        Fnv h;
        for (const auto& n : w.significant_npcs) {
            h.pod(n.id);
            h.pod(n.status);
            h.pod(n.role);
            h.pod(n.capital);
            h.pod(n.current_province_id);
            h.pod(n.home_province_id);
            for (float wgt : n.motivations.weights)
                h.pod(wgt);
            h.pod(n.memory_log.size());
            for (const auto& m : n.memory_log) {
                h.pod(m.tick_timestamp);
                h.pod(m.type);
                h.pod(m.subject_id);
                h.pod(m.emotional_weight);
                h.pod(m.decay);
                h.pod(m.is_actionable);
            }
        }
        f.npcs = h.value();
        f.n_npcs = w.significant_npcs.size();
    }
    {
        Fnv h;
        for (const auto& b : w.npc_businesses) {
            h.pod(b.id);
            h.pod(b.owner_id);
            h.pod(b.province_id);
            h.pod(b.sector);
            h.pod(b.cash);
            h.pod(b.revenue_per_tick);
            h.pod(b.cost_per_tick);
            h.pod(b.criminal_sector);
        }
        f.businesses = h.value();
        f.n_businesses = w.npc_businesses.size();
    }
    {
        Fnv h;
        for (const auto& fc : w.facilities) {
            h.pod(fc.id);
            h.pod(fc.business_id);
            h.pod(fc.province_id);
            h.str(fc.recipe_id);
            h.pod(fc.tech_tier);
            h.pod(fc.output_rate_modifier);
            h.pod(fc.worker_count);
            h.pod(fc.max_workers);
            h.pod(fc.is_operational);
        }
        f.facilities = h.value();
        f.n_facilities = w.facilities.size();
    }
    {
        Fnv h;
        for (const auto& m : w.regional_markets) {
            h.pod(m.good_id);
            h.pod(m.province_id);
            h.pod(m.spot_price);
            h.pod(m.equilibrium_price);
            h.pod(m.supply);
            h.pod(m.demand_buffer);
        }
        f.markets = h.value();
        f.n_markets = w.regional_markets.size();
    }
    {
        Fnv h;
        for (const auto& r : w.loaded_recipes) {
            h.str(r.id);
            h.str(r.facility_type_key);
            h.pod(r.era_available);
            h.pod(r.inputs.size());
            h.pod(r.outputs.size());
        }
        f.recipes = h.value();
        f.n_recipes = w.loaded_recipes.size();
    }
    {
        Fnv h;
        for (const auto& n : w.nations) {
            h.pod(n.government_type);
            h.pod(n.political_cycle.national_legitimacy);
        }
        f.nations = h.value();
    }
    {
        Fnv h;
        if (w.player) {
            h.pod(w.player->id);
            h.pod(w.player->wealth);
            h.pod(w.player->current_province_id);
        }
        f.player = h.value();
    }
    {
        Fnv h;
        h.pod(w.current_tick);
        h.pod(w.world_seed);
        h.pod(w.technology.current_era);
        h.pod(w.goods_catalog ? w.goods_catalog->size() : std::size_t{0});
        h.pod(w.evidence_pool.size());
        h.pod(w.consequence_queue.size());
        h.pod(w.pending_scene_cards.size());
        f.scalars = h.value();
    }
    return f;
}

}  // namespace econlife::fingerprint
