#include "modules/warfare/warfare_module.h"

#include <algorithm>
#include <cmath>
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
#include "modules/grain_logistics/grain_logistics_module.h"

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

float WarfareModule::p_attacker_wins(float attacker_strength, float defender_strength) {
    // Lanchester square law: concentrated force wins disproportionately. The contest
    // probability is the square-strength share — grounded in the classic attrition
    // model, naturally in [0, 1] with no clamp.
    const double sa = std::max(0.0f, attacker_strength);
    const double sb = std::max(0.0f, defender_strength);
    const double denom = sa * sa + sb * sb;
    if (denom <= 0.0)
        return 0.0f;
    return static_cast<float>((sa * sa) / denom);
}

float WarfareModule::campaign_fed_factor(float rations_needed, float rations_drawn,
                                         const WarfareConfig& cfg) {
    // Forage covers cfg.forage_share of the rations off the land (pillage / home
    // fields); the rest must come from the granary. An army with an empty commissary
    // still fights — at forage strength. Bounded by structure: [forage_share, 1].
    if (rations_needed <= 0.0f)
        return 1.0f;
    const float share = std::clamp(cfg.forage_share, 0.0f, 1.0f);
    return std::min(1.0f, share + std::max(0.0f, rations_drawn) / rations_needed);
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

bool WarfareModule::has_leader(uint32_t seat, uint32_t year) const {
    auto it = leader_until_.find(seat);
    return it != leader_until_.end() && year < it->second;
}

void WarfareModule::serialize_state(std::vector<uint8_t>& out) const {
    put_u32(out, 3u);  // version (3: + conqueror state; 2: + polities/win ledger)
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
    put_u32(out, static_cast<uint32_t>(leader_until_.size()));
    for (const auto& [seat, until] : leader_until_) {
        put_u32(out, seat);
        put_u32(out, until);
    }
    put_u32(out, static_cast<uint32_t>(member_since_.size()));
    for (const auto& [prov, since] : member_since_) {
        put_u32(out, prov);
        put_u32(out, since);
    }
}

bool WarfareModule::deserialize_state(const uint8_t* data, size_t size) {
    Reader r{data, size};
    const uint32_t version = r.u32();
    if (version < 1u || version > 3u)
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
    leader_until_.clear();
    member_since_.clear();
    if (version >= 3u) {
        const uint32_t leaders = r.u32();
        for (uint32_t i = 0; i < leaders && r.ok; ++i) {
            const uint32_t seat = r.u32();
            const uint32_t until = r.u32();
            leader_until_[seat] = until;
        }
        const uint32_t members = r.u32();
        for (uint32_t i = 0; i < members && r.ok; ++i) {
            const uint32_t prov = r.u32();
            const uint32_t since = r.u32();
            member_since_[prov] = since;
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
        // Regime exit: war_death_fraction is only ever written by this module, so a
        // last active-year value would persist forever once we stop publishing.
        // Publish a one-time 0 reset so the field really is transient.
        if (war_state_dirty_) {
            for (uint32_t i = 0; i < n; ++i) {
                RegionDelta rd{};
                rd.region_id = state.provinces[i].region_id;
                rd.war_death_fraction_replacement = 0.0f;
                delta.region_deltas.push_back(rd);
            }
            war_state_dirty_ = false;
        }
        return;
    }

    // Annual cadence: every war parameter is a per-YEAR rate and the attack RNG is
    // seeded by year. One decision pass per year.
    if (state.current_tick % kTicksPerYear != 0)
        return;

    // Seed by YEAR so a war is consistent across the year at any tick resolution.
    const uint32_t year = state.current_tick / kTicksPerYear;
    const float gravity_g = state.hazard_settings.gravity_g;
    const auto h3_to_idx = build_h3_to_province_index(state.provinces);

    // Grounded inputs per province: the LEVY (people who can campaign), the GRANARY
    // (rations they can draw), and resident wealth (the coin prize).
    std::vector<double> pop(n, 0.0);
    std::vector<float> levy(n, 0.0f);
    std::vector<double> avail_store(n, 0.0);  // granary available this year (mutable)
    std::vector<double> store_delta(n, 0.0);  // net grain change to publish
    std::vector<double> avail_capital(n, 0.0);
    for (uint32_t i = 0; i < n; ++i) {
        if (state.provinces[i].cohort_stats) {
            pop[i] = static_cast<double>(state.provinces[i].cohort_stats->total_population);
            levy[i] = cfg_.levy_fraction * static_cast<float>(pop[i]);
            avail_store[i] = std::max(0.0f, state.provinces[i].cohort_stats->food_store);
        }
        if (i < state.npc_indices_by_home_province.size()) {
            for (uint32_t idx : state.npc_indices_by_home_province[i]) {
                if (idx < state.significant_npcs.size())
                    avail_capital[i] += std::max(0.0f, state.significant_npcs[idx].capital);
            }
        }
    }
    const std::vector<double> orig_capital = avail_capital;  // for proportional debit

    // Military supply moves on the same carts as grain — ONE logistics law. The
    // delivered fraction of a link bounds both rations to the front and loot home.
    auto link_df = [&](uint32_t from, uint32_t to) -> float {
        float best = 0.0f;
        for (const auto& link : state.provinces[from].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it != h3_to_idx.end() && it->second == to) {
                best = std::max(best, GrainLogisticsModule::delivered_fraction(
                                          link.type, link.transit_terrain_cost,
                                          link.infrastructure_bonus, gravity_g, grain_cfg_));
            }
        }
        return best;
    };

    // Polity aggregates (M6c-5): members pool their levy — internal peace, external
    // weight. Steppe share classifies the cavalry polities (Genghis).
    std::map<uint32_t, float> polity_levy;
    std::map<uint32_t, float> steppe_levy;
    // ASABIYA (R3C) — what a people can do together, over and above its numbers. Ibn
    // Khaldun's observation is why a frontier tribe takes an empire that outnumbers it.
    //
    // It multiplies STRENGTH, never the headcount. `levy` stays a count of people,
    // because it is also what casualties are apportioned over and what a seceding member
    // takes with it: folding the multiplier into it made a beaten polity lose 12.5% of
    // its population when only 10% had been mustered — more dead than there were
    // soldiers. Strength and bodies are different quantities and the model keeps them so.
    std::vector<float> asabiya_mult(n, 1.0f);
    std::map<uint32_t, float> polity_strength;
    for (uint32_t i = 0; i < n; ++i) {
        if (state.provinces[i].cohort_stats)
            asabiya_mult[i] = asabiya_strength_mult(state.provinces[i].cohort_stats->asabiya, cfg_);
    }
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t pid = polity_of(i);
        polity_levy[pid] += levy[i];
        polity_strength[pid] += levy[i] * asabiya_mult[i];
        const auto& geo = state.provinces[i].geography;
        if (geo.arable_land_fraction < cfg_.steppe_arable_max &&
            geo.forest_coverage < cfg_.steppe_forest_max)
            steppe_levy[pid] += levy[i];
    }
    std::map<uint32_t, bool> cavalry;
    for (const auto& [pid, lv] : polity_levy)
        cavalry[pid] = lv > 0.0f && steppe_levy[pid] >= cfg_.cavalry_polity_min_share * lv;

    // ALEXANDER (M6c-6): rarely, a polity seat produces a great commander whose
    // tenure multiplies the polity's fighting quality while he lives.
    for (auto it = leader_until_.begin(); it != leader_until_.end();) {
        if (year >= it->second)
            it = leader_until_.erase(it);
        else
            ++it;
    }
    std::map<uint32_t, float> leader_mult;
    for (const auto& [pid, lv] : polity_levy) {
        if (lv <= 0.0f)
            continue;
        if (!leader_until_.count(pid) && cfg_.leadership_rate > 0.0f) {
            DeterministicRNG lead_rng(state.world_seed ^
                                      (static_cast<uint64_t>(year) * 0xA24BAED4963EE407ull) ^
                                      (static_cast<uint64_t>(pid) << 13) ^ 0xA1E7ull);
            if (lead_rng.next_float() < cfg_.leadership_rate)
                leader_until_[pid] = year + cfg_.leadership_tenure_years;
        }
        leader_mult[pid] = leader_until_.count(pid) ? cfg_.leadership_power_mult : 1.0f;
    }

    // Granary availability and draws at polity scope (members provision the army).
    auto polity_store = [&](uint32_t pid) {
        double s = 0.0;
        for (uint32_t p = 0; p < n; ++p)
            if (polity_of(p) == pid)
                s += avail_store[p];
        return s;
    };
    auto draw_from_polity = [&](uint32_t pid, double amount) {
        // Deterministic sequential draw across members in index order; conserved:
        // exactly `amount` leaves the granaries (callers bound amount by availability).
        for (uint32_t p = 0; p < n && amount > 0.0; ++p) {
            if (polity_of(p) != pid)
                continue;
            const double take = std::min(amount, avail_store[p]);
            avail_store[p] -= take;
            store_delta[p] -= take;
            amount -= take;
        }
    };

    std::vector<double> war_death(n, 0.0);       // extra annual death fraction, per province
    std::vector<double> plundered_from(n, 0.0);  // coin seized FROM this province
    std::vector<double> looted_to(n, 0.0);       // coin seized BY this province
    std::set<uint64_t> considered_pairs;         // for relation drift
    std::set<uint64_t> warred_pairs;
    std::set<uint32_t> betrayers;

    // Each polity considers striking targets within supply reach: 1-hop neighbours
    // (the border), and 2-hop targets THROUGH an intermediate — where the supply line
    // pays the ox law (path = product of link delivered-fractions) unless the army is
    // herd-fed cavalry (path 1). Distant campaigns are not forbidden; they are
    // expensive and weak, so infeasible ones fail the strength gate naturally.
    for (uint32_t a = 0; a < n; ++a) {
        if (levy[a] <= 0.0f)
            continue;
        const uint32_t pid_a = polity_of(a);
        // target -> supply path fraction (best route; deterministic tie-break by
        // enumeration order: ordered map keeps ascending target order).
        std::map<uint32_t, float> targets;
        for (const auto& link : state.provinces[a].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end())
                continue;
            const uint32_t b1 = it->second;
            // Border war: the front is home's edge; supply carries directly (path 1).
            targets[b1] = std::max(targets[b1], 1.0f);
            for (const auto& l2 : state.provinces[b1].links) {
                auto it2 = h3_to_idx.find(l2.neighbor_h3);
                if (it2 == h3_to_idx.end() || it2->second == a)
                    continue;
                const uint32_t b2 = it2->second;
                const float path = cavalry[pid_a]
                                       ? 1.0f
                                       : link_df(a, b1) * link_df(b1, b2);
                if (path > 0.0f) {
                    auto t = targets.find(b2);
                    if (t == targets.end() || path > t->second)
                        targets[b2] = path;
                }
            }
        }
        for (const auto& [b, path_fraction] : targets) {
            if (pop[b] <= 0.0)
                continue;
            if (pid_a == polity_of(b))
                continue;  // members of the same polity are at internal peace
            const uint32_t pid_b = polity_of(b);
            considered_pairs.insert(pair_key(a, b));

            // ATTACKER: the polity's levy marches; rations for the season must cross
            // the supply path, so the granary need scales 1/path (the ox law).
            const float army_a = polity_levy[pid_a];
            if (army_a <= 0.0f)
                continue;
            const float rations_a =
                army_a * cfg_.campaign_days * cfg_.soldier_ration_mult / std::max(path_fraction, 1e-3f);
            const float store_need_a = rations_a * (1.0f - cfg_.forage_share);
            const float drawn_a = static_cast<float>(
                std::min(static_cast<double>(store_need_a), polity_store(pid_a)));
            const float fed_a = campaign_fed_factor(rations_a, drawn_a, cfg_);
            // Soldiers eat as bodies (army_a) and fight as a people (polity_strength):
            // asabiya makes men fight above their number, not eat below it.
            const float S_a = polity_strength[pid_a] * fed_a * leader_mult[pid_a] * path_fraction;

            // DEFENDER: home mobilization (shorter season, home granaries), plus
            // warm-relation allied neighbours marching without organized supply
            // (they fight at forage strength).
            const float army_b = polity_levy[pid_b];
            const float rations_b = army_b * cfg_.defense_days * cfg_.soldier_ration_mult;
            const float store_need_b = rations_b * (1.0f - cfg_.forage_share);
            const float drawn_b = static_cast<float>(
                std::min(static_cast<double>(store_need_b), polity_store(pid_b)));
            const float fed_b = campaign_fed_factor(rations_b, drawn_b, cfg_);
            float S_b = polity_strength[pid_b] * fed_b * leader_mult[pid_b];
            for (const auto& blink : state.provinces[b].links) {
                auto bit = h3_to_idx.find(blink.neighbor_h3);
                if (bit == h3_to_idx.end())
                    continue;
                const uint32_t c = bit->second;
                if (c == a || polity_of(c) == pid_b)
                    continue;  // attacker excluded; polity members already counted
                if (polity_of(c) == pid_a)
                    continue;  // the attacker's own members never defend its target
                if (relation(b, c) >= cfg_.ally_threshold)
                    S_b += levy[c] * asabiya_mult[c] * cfg_.forage_share;
            }

            // The decision gate: a rational polity attacks only with a clear edge
            // (risk policy), preferring rich targets, deterred by warm relations.
            if (S_a <= 0.0f || S_a < cfg_.aggression_ratio * S_b)
                continue;
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

            // WAR. Both armies eat their campaign rations from the granaries —
            // conserved: the grain is consumed by soldiers' mouths (the sink is real).
            draw_from_polity(pid_a, drawn_a);
            draw_from_polity(pid_b, drawn_b);
            warred_pairs.insert(pair_key(a, b));
            if (relation(a, b) >= cfg_.ally_threshold)
                betrayers.insert(a);  // attacking a warm ally is a betrayal

            // BATTLE (Lanchester): each side's dead are proportional to the ENEMY's
            // effective strength; the outcome is the square-law contest. Real units:
            // people.
            //
            // The dead are the SOLDIERS THE POLITY FIELDED, so they are attributed
            // across the member provinces that raised the levy, in proportion to
            // what each contributed — and each province can lose at most the
            // soldiers it actually sent. That per-province bound is physical (you
            // cannot bury more men than you mustered), which is what keeps the
            // published fraction inside [0, levy_fraction] on its own.
            //
            // Before this, a whole polity's pooled dead were charged to the single
            // border province of the pair: an empire fighting through a small
            // frontier province produced a death fraction above 1.0, the [0,1]
            // clamp in apply_deltas pinned it at 1.0, and population_aging wiped
            // every cohort in that province in one year while the surplus dead
            // vanished and the provinces that supplied the army lost nobody.
            auto attribute_casualties = [&](uint32_t pid, float dead) {
                const float mustered = polity_levy[pid];
                if (mustered <= 0.0f || dead <= 0.0f)
                    return;
                for (uint32_t i = 0; i < n; ++i) {
                    if (polity_of(i) != pid || levy[i] <= 0.0f || pop[i] <= 0.0)
                        continue;
                    const float share = dead * (levy[i] / mustered);
                    const float fallen = std::min(share, levy[i]);  // physical bound
                    war_death[i] += fallen / static_cast<float>(pop[i]);
                }
            };
            // Allied contingents add strength at forage rates but are not yet
            // debited casualties of their own (documented simplification: allies
            // send men, not their whole levy). Nothing is minted by it — the
            // attacker's dead are still drawn from the attacker's own muster.
            attribute_casualties(pid_a, cfg_.battle_lethality * S_b);
            attribute_casualties(pid_b, cfg_.battle_lethality * S_a);
            const bool attacker_won = rng.next_float() < p_attacker_wins(S_a, S_b);
            if (!attacker_won)
                continue;  // repelled: the attacker paid rations and blood for nothing

            // SACK (victor only): grain plunder is carry-limited and pays the ox law
            // on the way home; what is sacked but not delivered is BURNED — an
            // explicit destruction sink, conserved.
            const double sack = cfg_.sack_fraction * avail_store[b];
            if (sack > 0.0) {
                const double carry = static_cast<double>(army_a) * cfg_.carry_per_soldier;
                const double delivered = std::min(sack, carry) * path_fraction;
                avail_store[b] -= sack;
                store_delta[b] -= sack;
                avail_store[a] += delivered;
                store_delta[a] += delivered;
            }

            // Consolidation up the ladder (M6c-5, §5.4): repeated decisive VICTORIES
            // absorb the loser's whole polity into the victor's.
            const uint64_t directed = (static_cast<uint64_t>(a) << 32) | b;
            if (cfg_.absorb_after_wins > 0 && ++win_counts_[directed] >= cfg_.absorb_after_wins) {
                win_counts_[directed] = 0;
                const uint32_t beaten_pid = polity_of(b);
                float moved = 0.0f, moved_strength = 0.0f;
                for (uint32_t p = 0; p < n; ++p) {
                    if (polity_of(p) == beaten_pid) {
                        polity_of_[p] = pid_a;
                        member_since_[p] = year;  // integration (cohesion) starts now
                        moved += levy[p];
                        moved_strength += levy[p] * asabiya_mult[p];
                    }
                }
                polity_levy[pid_a] += moved;
                polity_levy[beaten_pid] = 0.0f;
                // The strength aggregate follows the bodies: a conquered people brings
                // whatever solidarity it had into its conqueror's armies.
                polity_strength[pid_a] += moved_strength;
                polity_strength[beaten_pid] = 0.0f;
                leader_until_.erase(beaten_pid);  // a dissolved polity has no seat
            }

            // Coin plunder (portable wealth), conserved — only when the victor has a
            // valid resident to receive it.
            bool victor_can_receive = false;
            if (a < state.npc_indices_by_home_province.size()) {
                for (uint32_t idx : state.npc_indices_by_home_province[a]) {
                    if (idx < state.significant_npcs.size()) {
                        victor_can_receive = true;
                        break;
                    }
                }
            }
            const double coin =
                victor_can_receive ? cfg_.plunder_fraction * avail_capital[b] : 0.0;
            if (coin > 0.0) {
                avail_capital[b] -= coin;
                plundered_from[b] += coin;
                looted_to[a] += coin;
            }
        }
    }

    // Relations drift: a war damages the pair's relation; a peaceful considered year
    // heals it. Deterministic (ordered sets).
    for (uint64_t key : considered_pairs) {
        float rel = relations_.count(key) ? relations_[key] : 0.0f;
        if (warred_pairs.count(key))
            rel -= cfg_.relation_war_hit;
        else
            rel += cfg_.relation_peace_heal;
        relations_[key] = std::clamp(rel, -1.0f, 1.0f);
    }

    // The hold problem (M6c-5/§5.5): a polity keeps a member only while the rest can
    // overawe it. ROME (M6c-6): integration grows with tenure AND with the quality of
    // the routes that carry administration — the same links that carry grain (roads
    // and rivers integrate; a member with no route to its polity never integrates).
    // Saturating on the assimilation timescale; the asymptote is the mechanism.
    for (uint32_t m = 0; m < n; ++m) {
        const uint32_t pid = polity_of(m);
        if (pid == m)
            continue;  // the seat is the polity
        // The centre overawes with STRENGTH, not headcount: a living great
        // commander (Alexander) holds what raw numbers could not — and his death
        // is what lets the members go.
        // Both sides of the hold are strengths: a cohesive member pulls away from a
        // centre that could hold a softer one of the same size, which is how a frontier
        // march breaks off an empire it is nominally part of.
        float rest = std::max(0.0f, polity_strength[pid] - levy[m] * asabiya_mult[m]) *
                     (leader_mult.count(pid) ? leader_mult[pid] : 1.0f);
        // A STATE IN CRISIS CANNOT HOLD WHAT A HEALTHY ONE COULD (R7). Structural stress
        // is what an empire comes apart FROM: Rome's third-century crisis nearly
        // fragmented it and the Gallic and Palmyrene empires actually did break away.
        // Without this the stress killed people and ate grain but never loosened anyone's
        // grip, so a polity that had absorbed its neighbours held them forever — measured,
        // an earthlike world unified around year 1,500 and never fragmented again in the
        // remaining 11,250 years, which left nobody to fight, no frontier for asabiya, one
        // shelter for the corpus, and one treasury carrying everybody.
        //
        // PSI is published per province with its POLITY's value, so the member's own copy
        // is its state's stress. One tick stale (structural_demography runs after this),
        // which is nothing on a variable that moves over centuries.
        if (state.provinces[m].cohort_stats != nullptr) {
            const float psi = std::max(0.0f, state.provinces[m].cohort_stats->political_stress);
            rest /= 1.0f + std::max(0.0f, cfg_.psi_hold_weight) * psi;
        }
        uint32_t years_held = 0;
        auto since = member_since_.find(m);
        if (since != member_since_.end() && year > since->second)
            years_held = year - since->second;
        float admin_route = 0.0f;  // best link quality to a fellow polity member
        for (const auto& link : state.provinces[m].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end() || polity_of(it->second) != pid || it->second == m)
                continue;
            admin_route = std::max(admin_route, link_df(m, it->second));
        }
        const float eff_years = static_cast<float>(years_held) * admin_route;
        const float cohesion =
            1.0f + cfg_.cohesion_gain_max * eff_years /
                       (eff_years + std::max(1.0f, cfg_.cohesion_halfsat_years));

        // IMPERIAL OVERSTRETCH (R10). An empire cannot bring its whole army against one
        // rebellious province: it has to hold all of them at once, so the force actually
        // available against any single member is what is left after garrisoning the rest.
        // The bigger the empire, the thinner that is everywhere — which is why empires
        // fray at their edges rather than shattering at the centre.
        //
        // Without this, secession was ARITHMETICALLY IMPOSSIBLE for any polity of three or
        // more evenly-sized provinces, at ANY cohesion: a member holds 1/N of the strength
        // and had to out-muscle 0.8 x cohesion x (N-1)/N of it, which for N >= 3 exceeds
        // anything a member can ever hold. Measured, that is exactly why an earthlike
        // world unified permanently around year 1,500 and never fragmented again in the
        // remaining 11,250 years — leaving nobody to fight, no frontier for asabiya to be
        // forged at, one shelter for the written corpus, and one treasury carrying every
        // province. Europe never unified; here it could not do otherwise.
        //
        // Dividing by the number of members held restores the right shape: a fresh
        // conquest in a large empire can break away, while a province with generations of
        // integration and a good road cannot (cohesion carries that). A two-province
        // polity is unaffected — there is only one member to hold, so the whole army is
        // available, which is what lets the Alexander arc still work.
        uint32_t members_held = 0;
        for (uint32_t q = 0; q < n; ++q)
            if (q != pid && polity_of(q) == pid)
                ++members_held;
        const float projected = rest / static_cast<float>(std::max(1u, members_held));
        if (levy[m] * asabiya_mult[m] > cfg_.secession_power_ratio * cohesion * projected) {
            polity_of_[m] = m;
            member_since_.erase(m);
            // Store the remaining HEADCOUNT, not `rest` — rest is a strength (the
            // headcount already multiplied by the leader bonus). Writing a strength
            // into the levy aggregate made every later member of the same polity in
            // this pass re-apply the multiplier, seeing up to mult^2 the true
            // remaining levy and suppressing the cascade that should follow the
            // first secession.
            polity_levy[pid] = std::max(0.0f, polity_levy[pid] - levy[m]);
            polity_levy[m] += levy[m];
            polity_strength[pid] =
                std::max(0.0f, polity_strength[pid] - levy[m] * asabiya_mult[m]);
            polity_strength[m] += levy[m] * asabiya_mult[m];
        }
    }

    // ASABIYA (R3C): one year of Ibn Khaldun's law, applied AFTER this year's conquests
    // and secessions, so a province that has just been absorbed into a larger realm
    // begins softening immediately and one just cut loose begins hardening.
    //
    // `frontier` is the share of a province's neighbours under another polity. A people
    // surrounded by strangers coheres; one deep inside its own realm forgets how. That is
    // the whole mechanism, and it is why an empire's founders come from its edge and its
    // successors from someone else's.
    //
    // It matters most as a SECOND OSCILLATOR: geography drives it, not the harvest, so it
    // has its own period and provinces stop rising and falling in unison. Without it the
    // world moves as one sawtooth.
    for (uint32_t i = 0; i < n; ++i) {
        if (!state.provinces[i].cohort_stats)
            continue;
        const uint32_t mine = polity_of(i);
        uint32_t neighbours = 0, strangers = 0;
        for (const auto& link : state.provinces[i].links) {
            auto it = h3_to_idx.find(link.neighbor_h3);
            if (it == h3_to_idx.end() || it->second == i)
                continue;
            ++neighbours;
            if (polity_of(it->second) != mine)
                ++strangers;
        }
        // An isolated province faces nobody, so nothing forges its solidarity and it
        // softens like any interior. That is the correct reading, not a special case.
        const float frontier =
            neighbours > 0 ? static_cast<float>(strangers) / static_cast<float>(neighbours) : 0.0f;
        RegionDelta ad{};
        ad.region_id = state.provinces[i].region_id;
        ad.asabiya_replacement =
            asabiya_year(state.provinces[i].cohort_stats->asabiya, frontier, cfg_);
        // The political map itself, exposed (R3F): how many independent jurisdictions a
        // world contains is what decides whether an idea suppressed in one survives in
        // another.
        ad.polity_id_replacement = mine;
        delta.region_deltas.push_back(ad);
    }

    // Reputation economy: a betrayer's relations with ALL its neighbours sour.
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

    // Publish: war deaths (real units: extra annual death fraction) and the conserved
    // grain flows (rations eaten, plunder delivered, sack burned).
    for (uint32_t i = 0; i < n; ++i) {
        RegionDelta rd{};
        rd.region_id = state.provinces[i].region_id;
        rd.war_death_fraction_replacement = static_cast<float>(war_death[i]);
        if (store_delta[i] != 0.0)
            rd.food_store_delta = static_cast<float>(store_delta[i]);
        delta.region_deltas.push_back(rd);
        if (war_death[i] > 0.0)
            war_state_dirty_ = true;  // a spike is out; regime exit must reset it
    }

    // Distribute the conserved coin plunder to residents: debit the loser's residents
    // proportional to their wealth (never below zero), credit the victor's equally.
    for (uint32_t i = 0; i < n; ++i) {
        if (plundered_from[i] <= 0.0 && looted_to[i] <= 0.0)
            continue;
        if (i >= state.npc_indices_by_home_province.size())
            continue;
        const auto& residents = state.npc_indices_by_home_province[i];
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
