#include "modules/warfare/warfare_module.h"

#include <algorithm>
#include <cstdint>
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
constexpr uint32_t kTicksPerYear = 365;
}

float WarfareModule::military_power(uint64_t population, float surplus_ratio,
                                   const WarfareConfig& cfg) {
    const float fed =
        cfg.power_surplus_floor + (1.0f - cfg.power_surplus_floor) * std::clamp(surplus_ratio, 0.0f, 2.0f);
    return static_cast<float>(population) * std::max(0.0f, fed);
}

bool WarfareModule::regime_active(std::string_view regime) const {
    for (const auto& r : cfg_.active_regimes) {
        if (r == regime)
            return true;
    }
    return false;
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

void WarfareModule::execute(const WorldState& state, DeltaBuffer& delta) {
    const EraDefinition* era = state.era_catalog.by_index(state.technology.current_era);
    if (era == nullptr || !regime_active(era->economic_regime))
        return;

    const uint32_t n = static_cast<uint32_t>(state.provinces.size());
    if (n == 0)
        return;

    // Per-polity military power, resident wealth (the war prize), and an h3 -> index
    // map for resolving neighbours.
    std::vector<float> power(n, 0.0f);
    std::vector<double> avail_capital(n, 0.0);  // resident proto-capital; the plunder prize
    std::unordered_map<H3Index, uint32_t> h3_to_idx;
    h3_to_idx.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        h3_to_idx[state.provinces[i].h3_index] = i;
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
            adjacent_pairs.insert(pair_key(a, b));
            // Coalition defence (the balance of power): the defender's allies (its
            // warm-relation neighbours) add their power, so a hegemon must out-match the
            // whole coalition, not just its target.
            float defender_power = power[b];
            for (const auto& blink : state.provinces[b].links) {
                auto bit = h3_to_idx.find(blink.neighbor_h3);
                if (bit == h3_to_idx.end())
                    continue;
                const uint32_t c = bit->second;
                if (c == a)
                    continue;  // the attacker is not counted among the defender's allies
                if (relation(b, c) >= cfg_.ally_threshold)
                    defender_power += power[c];
            }
            if (power[a] < cfg_.aggression_ratio * defender_power)
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
            // Spoils: the victor plunders a share of the loser's remaining wealth
            // (sequential depletion keeps it conserved across multiple attackers).
            const double plunder = cfg_.plunder_fraction * avail_capital[b];
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
    }

    // Distribute the conserved plunder to residents: debit the loser's residents
    // proportional to their wealth (never below zero), credit the victor's equally.
    for (uint32_t i = 0; i < n; ++i) {
        if (plundered_from[i] <= 0.0 && looted_to[i] <= 0.0)
            continue;
        if (i >= state.npc_indices_by_home_province.size())
            continue;
        const auto& residents = state.npc_indices_by_home_province[i];
        if (residents.empty())
            continue;
        const double credit_each = looted_to[i] / static_cast<double>(residents.size());
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
