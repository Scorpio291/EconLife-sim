#include "modules/warfare/warfare_module.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <unordered_map>
#include <vector>

#include "core/rng/deterministic_rng.h"
#include "core/world_gen/era_catalog.h"
#include "core/world_state/delta_buffer.h"
#include "core/world_state/geography.h"
#include "core/world_state/world_state.h"

namespace econlife {

namespace {
void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}
void put_u64(std::vector<uint8_t>& out, uint64_t v) {
    put_u32(out, static_cast<uint32_t>(v));
    put_u32(out, static_cast<uint32_t>(v >> 32));
}
void put_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(out, bits);
}
struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;
    uint32_t u32() {
        if (pos + 4 > size) {
            ok = false;
            return 0;
        }
        uint32_t v = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
                     (static_cast<uint32_t>(data[pos + 2]) << 16) |
                     (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint64_t u64() {
        const uint64_t lo = u32();
        return lo | (static_cast<uint64_t>(u32()) << 32);
    }
    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    uint8_t u8() {
        if (pos + 1 > size) {
            ok = false;
            return 0;
        }
        return data[pos++];
    }
};
}  // namespace

float WarfareModule::military_power(uint64_t population, float surplus_ratio,
                                   const WarfareConfig& cfg) {
    const float fed =
        cfg.power_surplus_floor + (1.0f - cfg.power_surplus_floor) * std::clamp(surplus_ratio, 0.0f, 2.0f);
    return static_cast<float>(population) * std::max(0.0f, fed);
}

bool WarfareModule::regime_active(std::string_view regime) const {
    return regime_in(cfg_.active_regimes, regime);
}

uint64_t WarfareModule::pair_key(uint32_t a, uint32_t b) {
    const uint32_t lo = a < b ? a : b;
    const uint32_t hi = a < b ? b : a;
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

float WarfareModule::relation(uint32_t a, uint32_t b) const {
    auto it = relations_.find(pair_key(a, b));
    return it == relations_.end() ? 0.0f : it->second;
}

uint32_t WarfareModule::polity_of(uint32_t province) const {
    auto it = polity_of_.find(province);
    return it == polity_of_.end() ? province : it->second;
}

void WarfareModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 2u);  // version (2: + polities and the decisive-win ledger)
    out.push_back(war_state_dirty_ ? 1u : 0u);
    put_u32(out, static_cast<uint32_t>(relations_.size()));
    for (const auto& [key, rel] : relations_) {  // std::map: deterministic key order
        put_u64(out, key);
        put_f32(out, rel);
    }
    put_u32(out, static_cast<uint32_t>(polity_of_.size()));
    for (const auto& [prov, pid] : polity_of_) {
        put_u32(out, prov);
        put_u32(out, pid);
    }
    put_u32(out, static_cast<uint32_t>(win_counts_.size()));
    for (const auto& [key, wins] : win_counts_) {
        put_u64(out, key);
        put_u32(out, wins);
    }
}

bool WarfareModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    const uint32_t version = r.u32();
    if (version < 1u || version > 2u)
        return false;
    war_state_dirty_ = (r.u8() != 0u);
    const uint32_t count = r.u32();
    relations_.clear();
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        const uint64_t key = r.u64();
        const float rel = r.f32();
        relations_[key] = rel;
    }
    polity_of_.clear();
    win_counts_.clear();
    if (version >= 2u) {
        const uint32_t polities = r.u32();
        for (uint32_t i = 0; i < polities && r.ok; ++i) {
            const uint32_t prov = r.u32();
            const uint32_t pid = r.u32();
            polity_of_[prov] = pid;
        }
        const uint32_t wins = r.u32();
        for (uint32_t i = 0; i < wins && r.ok; ++i) {
            const uint64_t key = r.u64();
            const uint32_t w = r.u32();
            win_counts_[key] = w;
        }
    }
    return r.ok;
}

void WarfareModule::execute(const WorldState& state, DeltaBuffer& delta) {
    const uint32_t n = static_cast<uint32_t>(state.provinces.size());
    if (n == 0)
        return;

    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime)) {
        // Regime exit: war_mortality is only ever written by this module, so if the
        // last active year left a war spike (> 1.0) it would persist forever once we
        // stop publishing. Publish a one-time 1.0 reset so the field really is
        // transient (review finding: the stale phantom-war multiplier).
        if (war_state_dirty_) {
            for (uint32_t i = 0; i < n; ++i) {
                RegionDelta rd{};
                rd.region_id = state.provinces[i].region_id;
                rd.war_mortality_replacement = 1.0f;
                delta.region_deltas.push_back(rd);
            }
            war_state_dirty_ = false;
        }
        return;
    }

    // Annual cadence: every war parameter (attack probability, plunder fraction,
    // relation drift) is a per-YEAR rate, and the attack RNG is seeded by year — so
    // running per tick would re-fire the same war 365x (compounding plunder and
    // racing diplomacy 365x too fast; review finding). One decision pass per year.
    if (state.current_tick % kTicksPerYear != 0)
        return;

    // Per-polity military power, resident wealth (the war prize), and an h3 -> index
    // map for resolving neighbours.
    std::vector<float> power(n, 0.0f);
    std::vector<double> avail_capital(n, 0.0);  // resident proto-capital; the plunder prize
    const auto h3_to_idx = build_h3_to_province_index(state.provinces);
    for (uint32_t i = 0; i < n; ++i) {
        if (state.provinces[i].cohort_stats) {
            power[i] = military_power(state.provinces[i].cohort_stats->total_population,
                                     state.provinces[i].cohort_stats->subsistence_surplus_ratio, cfg_);
        }
        if (i < state.npc_indices_by_home_province.size()) {
            for (uint32_t idx : state.npc_indices_by_home_province[i]) {
                if (idx < state.significant_npcs.size())
                    avail_capital[i] += std::max(0.0f, state.significant_npcs[idx].capital);
            }
        }
    }
    const std::vector<double> orig_capital = avail_capital;  // for proportional debit distribution

    // Polity power (M6c-5): members pool their power — internal peace, external
    // weight. A polity's power is the sum over its member provinces.
    std::map<uint32_t, float> polity_power;
    for (uint32_t i = 0; i < n; ++i)
        polity_power[polity_of(i)] += power[i];

    // Seed by YEAR so a war is consistent across the year at any tick resolution.
    const uint32_t year = state.current_tick / kTicksPerYear;

    // war_mortality accumulates per province; plundered[]/looted[] track the conserved
    // proto-capital transfers (loser -> victor) for distribution to residents below.
    std::vector<float> war_mortality(n, 1.0f);
    std::vector<double> plundered_from(n, 0.0);  // wealth seized FROM this province
    std::vector<double> looted_to(n, 0.0);       // wealth seized BY this province
    std::set<uint64_t> adjacent_pairs;           // every reachable pair (for relation drift)
    std::set<uint64_t> warred_pairs;             // pairs that went to war this year
    std::set<uint32_t> betrayers;                // polities that attacked a warm-relation ally

    // Each polity considers attacking each REACHABLE neighbour (adjacency = ox-cart
    // reach). It attacks a weaker neighbour — strike where you can win — and prefers a
    // RICH one (the EV: weak AND rich), but WARM RELATIONS deter it (allies don't
    // fight). Directional and deterministic. On a win it plunders a share of the
    // loser's wealth (conserved).
    for (uint32_t a = 0; a < n; ++a) {
        if (power[a] <= 0.0f)
            continue;
        for (const auto& link : state.provinces[a].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            const uint32_t b = it->second;
            if (power[b] <= 0.0f)
                continue;
            // Members of the same polity are at internal peace (the empire's pax).
            if (polity_of(a) == polity_of(b))
                continue;
            adjacent_pairs.insert(pair_key(a, b));
            // Coalition defence (the balance of power): the defender fields its WHOLE
            // polity's power, plus warm-relation allied neighbours outside it — a
            // hegemon must out-match the coalition, not just the border province.
            float defender_power = polity_power[polity_of(b)];
            for (const auto& blink : state.provinces[b].links) {
                auto bit = h3_to_idx.find(blink.neighbor_h3);
                if (bit == h3_to_idx.end())
                    continue;
                const uint32_t c = bit->second;
                if (c == a || polity_of(c) == polity_of(b))
                    continue;  // attacker excluded; polity members already counted
                if (relation(b, c) >= cfg_.ally_threshold)
                    defender_power += power[c];
            }
            const float attacker_power = polity_power[polity_of(a)];
            if (attacker_power < cfg_.aggression_ratio * defender_power)
                continue;  // the coalition deters the attack
            // A rich neighbour is a more tempting target (the rational prize); warm
            // relations (a de-facto alliance) suppress the attack toward zero.
            const double prize_share =
                (avail_capital[a] + avail_capital[b] > 0.0)
                    ? avail_capital[b] / (avail_capital[a] + avail_capital[b])
                    : 0.0;
            const float deter =
                1.0f - cfg_.relation_deter_weight * std::max(0.0f, relation(a, b));
            const float attack_prob = std::clamp(
                cfg_.base_aggression_prob *
                    (1.0f + cfg_.prize_weight * static_cast<float>(prize_share)) * deter,
                0.0f, 1.0f);
            DeterministicRNG rng(state.world_seed ^
                                 (static_cast<uint64_t>(year) * 0x9E3779B97F4A7C15ull) ^
                                 (static_cast<uint64_t>(a) << 21) ^
                                 (static_cast<uint64_t>(b) << 41) ^ 0x4A1207ull);
            if (rng.next_float() >= attack_prob)
                continue;  // no war this year
            // War: a strikes b. Both bleed; the defender worse (war is worse to lose).
            war_mortality[a] = std::min(cfg_.war_mortality_cap, war_mortality[a] + cfg_.attacker_loss);
            war_mortality[b] = std::min(cfg_.war_mortality_cap, war_mortality[b] + cfg_.defender_loss);
            warred_pairs.insert(pair_key(a, b));
            if (relation(a, b) >= cfg_.ally_threshold)
                betrayers.insert(a);  // attacking a warm ally is a betrayal
            // Consolidation up the ladder (M6c-5, §5.4): repeated decisive wins absorb
            // the loser's WHOLE polity into the victor's — a beaten kingdom joins the
            // empire with all its provinces. Nesting, not per-province flipping.
            const uint64_t directed = (static_cast<uint64_t>(a) << 32) | b;
            if (cfg_.absorb_after_wins > 0 && ++win_counts_[directed] >= cfg_.absorb_after_wins) {
                win_counts_[directed] = 0;
                const uint32_t victor_pid = polity_of(a);
                const uint32_t beaten_pid = polity_of(b);
                float moved = 0.0f;
                for (uint32_t p = 0; p < n; ++p) {
                    if (polity_of(p) == beaten_pid) {
                        polity_of_[p] = victor_pid;
                        moved += power[p];
                    }
                }
                polity_power[victor_pid] += moved;
                polity_power[beaten_pid] = 0.0f;
            }
            // Spoils: the victor plunders a share of the loser's remaining wealth
            // (sequential depletion keeps it conserved across multiple attackers).
            // Only when the victor has a valid resident to receive it — otherwise the
            // debit would have no matching credit and the wealth would vanish
            // (conservation).
            bool victor_can_receive = false;
            if (a < state.npc_indices_by_home_province.size()) {
                for (uint32_t idx : state.npc_indices_by_home_province[a]) {
                    if (idx < state.significant_npcs.size()) {
                        victor_can_receive = true;
                        break;
                    }
                }
            }
            const double plunder =
                victor_can_receive ? cfg_.plunder_fraction * avail_capital[b] : 0.0;
            if (plunder > 0.0) {
                avail_capital[b] -= plunder;
                plundered_from[b] += plunder;
                looted_to[a] += plunder;
            }
        }
    }

    // Relations drift: a war damages the pair's relation; a peaceful adjacent year
    // heals it. Sustained peace warms neighbours into a de-facto alliance; feuds fester.
    // Deterministic (adjacent_pairs is ordered).
    for (uint64_t key : adjacent_pairs) {
        float rel = relations_.count(key) ? relations_[key] : 0.0f;
        if (warred_pairs.count(key))
            rel -= cfg_.relation_war_hit;
        else
            rel += cfg_.relation_peace_heal;
        relations_[key] = std::clamp(rel, -1.0f, 1.0f);
    }

    // The hold problem (M6c-5, §5.5): a polity keeps a member only while the rest of
    // the polity can overawe it. When a member's own power outgrows the remainder
    // (the centre can no longer pay for its reach), it SECEDES — the ladder drops a
    // level and the member is its own polity again. The seat (province == polity id)
    // cannot secede from itself. Deterministic (index order).
    for (uint32_t m = 0; m < n; ++m) {
        const uint32_t pid = polity_of(m);
        if (pid == m)
            continue;  // the seat is the polity
        const float rest = std::max(0.0f, polity_power[pid] - power[m]);
        if (power[m] > cfg_.secession_power_ratio * rest) {
            polity_of_[m] = m;
            polity_power[pid] = rest;
            polity_power[m] += power[m];
        }
    }

    // Reputation economy: a betrayer's word is worthless — its relations with ALL
    // neighbours sour, not just the ally it stabbed. A known backstabber can't hold
    // alliances, so it loses its coalition and gets ganged up on (self-limiting).
    for (uint32_t a : betrayers) {
        for (const auto& link : state.provinces[a].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            const uint64_t key = pair_key(a, it->second);
            relations_[key] =
                std::clamp(relations_[key] - cfg_.backstab_reputation_penalty, -1.0f, 1.0f);
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
        RegionDelta rd{};
        rd.region_id = state.provinces[i].region_id;
        rd.war_mortality_replacement = war_mortality[i];
        delta.region_deltas.push_back(rd);
        if (war_mortality[i] > 1.0f)
            war_state_dirty_ = true;  // a spike is out; regime exit must reset it
    }

    // Distribute the conserved plunder to residents: debit the loser's residents
    // proportional to their wealth (never below zero), credit the victor's equally.
    for (uint32_t i = 0; i < n; ++i) {
        if (plundered_from[i] <= 0.0 && looted_to[i] <= 0.0)
            continue;
        if (i >= state.npc_indices_by_home_province.size())
            continue;
        const auto& residents = state.npc_indices_by_home_province[i];
        // Credit is split across the VALID residents only, so a malformed index can
        // never evaporate a share of the loot (conservation).
        uint32_t valid = 0;
        for (uint32_t idx : residents) {
            if (idx < state.significant_npcs.size())
                ++valid;
        }
        if (valid == 0)
            continue;  // unreachable for looted_to > 0 (victor_can_receive gate above)
        const double credit_each = looted_to[i] / static_cast<double>(valid);
        for (uint32_t idx : residents) {
            if (idx >= state.significant_npcs.size())
                continue;
            const float cap = std::max(0.0f, state.significant_npcs[idx].capital);
            const double debit =
                (orig_capital[i] > 0.0) ? plundered_from[i] * (cap / orig_capital[i]) : 0.0;
            const double net = credit_each - debit;
            if (net != 0.0) {
                NPCDelta nd{};
                nd.npc_id = state.significant_npcs[idx].id;
                nd.capital_delta = static_cast<float>(net);
                delta.npc_deltas.push_back(nd);
            }
        }
    }
}

}  // namespace econlife
