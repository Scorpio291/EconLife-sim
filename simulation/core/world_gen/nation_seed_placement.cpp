// Nation seed placement (WorldGen §9.5.1). See nation_seed_placement.h.

#include "core/world_gen/nation_seed_placement.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <queue>

namespace econlife {

namespace {

// Candidates (by candidate index) within `radius` hops of province `origin`,
// counting hops through every province. Ascending.
std::vector<uint32_t> candidates_within(const std::vector<std::vector<uint32_t>>& adjacency,
                                        const std::vector<int32_t>& candidate_of, uint32_t origin,
                                        uint32_t radius) {
    std::vector<uint32_t> found;
    std::vector<uint32_t> dist(adjacency.size(), std::numeric_limits<uint32_t>::max());
    std::queue<uint32_t> frontier;
    dist[origin] = 0;
    frontier.push(origin);
    while (!frontier.empty()) {
        const uint32_t cur = frontier.front();
        frontier.pop();
        if (candidate_of[cur] >= 0)
            found.push_back(static_cast<uint32_t>(candidate_of[cur]));
        if (dist[cur] == radius)
            continue;
        for (uint32_t next : adjacency[cur]) {
            if (dist[next] != std::numeric_limits<uint32_t>::max())
                continue;
            dist[next] = dist[cur] + 1;
            frontier.push(next);
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

bool conflicts_with(const std::vector<std::vector<uint32_t>>& conflicts, uint32_t a, uint32_t b) {
    return std::binary_search(conflicts[a].begin(), conflicts[a].end(), b);
}

// A small fixed-width bitset over candidate indices, sized at run time.
struct Bits {
    std::vector<uint64_t> w;
    explicit Bits(uint32_t n = 0) : w((n + 63) / 64, 0) {}
    void set(uint32_t i) { w[i >> 6] |= uint64_t{1} << (i & 63); }
    void reset(uint32_t i) { w[i >> 6] &= ~(uint64_t{1} << (i & 63)); }
    bool test(uint32_t i) const { return (w[i >> 6] >> (i & 63)) & 1u; }
    bool none() const {
        for (uint64_t x : w)
            if (x != 0)
                return false;
        return true;
    }
    uint32_t count() const {
        uint32_t c = 0;
        for (uint64_t x : w)
            c += static_cast<uint32_t>(std::popcount(x));
        return c;
    }
    uint32_t count_and(const Bits& o) const {
        uint32_t c = 0;
        for (size_t i = 0; i < w.size(); ++i)
            c += static_cast<uint32_t>(std::popcount(w[i] & o.w[i]));
        return c;
    }
    void and_not(const Bits& o) {
        for (size_t i = 0; i < w.size(); ++i)
            w[i] &= ~o.w[i];
    }
};

uint32_t first_bit(const Bits& b) {
    for (size_t wi = 0; wi < b.w.size(); ++wi)
        if (b.w[wi] != 0)
            return static_cast<uint32_t>(wi * 64 + std::countr_zero(b.w[wi]));
    return std::numeric_limits<uint32_t>::max();
}

// Exact maximum independent set on the conflict graph (branch and bound).
// `closed[v]` is v plus everything it conflicts with. The bound is a greedy
// clique cover: a clique holds at most one seed. Vertices of degree <= 1 in the
// remaining graph are always in some maximum set and are taken without
// branching. The search is capped by a node budget so a pathological graph
// cannot stall world generation; an exhausted search is reported, never guessed.
struct MisSearch {
    const std::vector<Bits>& closed;
    uint64_t nodes = 0;
    bool exhausted = false;

    uint32_t clique_cover(Bits rest) const {
        uint32_t cliques = 0;
        for (uint32_t v = first_bit(rest); v != std::numeric_limits<uint32_t>::max();
             v = first_bit(rest)) {
            rest.reset(v);
            Bits cand = closed[v];
            cand.reset(v);
            for (size_t i = 0; i < cand.w.size(); ++i)
                cand.w[i] &= rest.w[i];
            for (uint32_t u = first_bit(cand); u != std::numeric_limits<uint32_t>::max();
                 u = first_bit(cand)) {
                rest.reset(u);
                cand.reset(u);
                for (size_t i = 0; i < cand.w.size(); ++i)
                    cand.w[i] &= closed[u].w[i];
            }
            ++cliques;
        }
        return cliques;
    }

    void run(Bits mask, uint32_t taken, uint32_t& best, uint32_t stop_at) {
        while (!mask.none()) {
            if (exhausted || best >= stop_at)
                return;
            if (++nodes > kExactSearchNodeBudget) {
                exhausted = true;
                return;
            }
            if (taken + clique_cover(mask) <= best)
                return;
            uint32_t pick = std::numeric_limits<uint32_t>::max();
            uint32_t pivot = 0;
            uint32_t pivot_degree = 0;
            for (size_t wi = 0; wi < mask.w.size() && pick == std::numeric_limits<uint32_t>::max();
                 ++wi) {
                for (uint64_t m = mask.w[wi]; m != 0; m &= m - 1) {
                    const uint32_t v = static_cast<uint32_t>(wi * 64 + std::countr_zero(m));
                    const uint32_t degree = closed[v].count_and(mask) - 1;
                    if (degree <= 1) {
                        pick = v;
                        break;
                    }
                    if (degree > pivot_degree) {
                        pivot_degree = degree;
                        pivot = v;
                    }
                }
            }
            if (pick == std::numeric_limits<uint32_t>::max()) {
                Bits without = mask;
                without.reset(pivot);
                run(without, taken, best, stop_at);
                pick = pivot;
            }
            mask.and_not(closed[pick]);
            ++taken;
        }
        best = std::max(best, taken);
    }

    // Largest separated set within `mask`, searched only until `stop_at`.
    uint32_t max_separated(const Bits& mask, uint32_t stop_at) {
        uint32_t best = 0;
        run(mask, 0, best, stop_at);
        return best;
    }
};

}  // namespace

NationSeedPlacementResult place_nation_seeds(const NationSeedPlacementInput& input,
                                             DeterministicRNG& rng) {
    NationSeedPlacementResult result;
    NationSeedReport& report = result.report;
    const uint32_t n = static_cast<uint32_t>(input.candidates.size());
    report.requested = input.requested;
    report.candidate_count = n;
    report.separation_hops = input.separation_hops;
    if (n == 0) {
        report.outcome = NationSeedOutcome::no_candidates;
        return result;
    }

    const uint32_t prov_count = static_cast<uint32_t>(input.adjacency.size());
    std::vector<int32_t> candidate_of(prov_count, -1);
    for (uint32_t k = 0; k < n; ++k)
        candidate_of[input.candidates[k]] = static_cast<int32_t>(k);

    // Conflict graph: two candidates conflict when they are within
    // separation_hops of each other.
    std::vector<std::vector<uint32_t>> conflicts(n);
    for (uint32_t k = 0; k < n; ++k) {
        conflicts[k] = candidates_within(input.adjacency, candidate_of, input.candidates[k],
                                         input.separation_hops);
        conflicts[k].erase(std::remove(conflicts[k].begin(), conflicts[k].end(), k),
                           conflicts[k].end());
    }

    // Weighted random order without replacement (Efraimidis-Spirakis): one draw
    // per candidate in ascending province order, key = ln(u) / w, highest first.
    // Equivalent to drawing one at a time with probability proportional to
    // weight, without a running total that drifts.
    std::vector<double> key(n);
    for (uint32_t k = 0; k < n; ++k) {
        const double u = 1.0 - rng.next_double();  // (0, 1]
        const double w = input.weights[k];
        key[k] = w > 0.0 ? std::log(u) / w : -std::numeric_limits<double>::infinity();
    }
    std::vector<uint32_t> order(n);
    for (uint32_t k = 0; k < n; ++k)
        order[k] = k;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (key[a] != key[b])
            return key[a] > key[b];
        return input.candidates[a] < input.candidates[b];
    });
    std::vector<uint32_t> rank(n);
    for (uint32_t r = 0; r < n; ++r)
        rank[order[r]] = r;

    std::vector<bool> chosen(n, false);
    uint32_t placed = 0;

    bool exact_done = false;
    uint32_t clique_bound = n;
    if (n <= kExactCandidateLimit) {
        // Small enough to know the exact maximum. Seeds are taken in priority order,
        // and a candidate is passed over only when taking it would leave fewer
        // separated seeds reachable than the geography allows. When the plain
        // greedy pass would reach the goal this is exactly that pass; when it
        // would fall short, the shortfall was the order's, and it is not kept.
        std::vector<Bits> closed(n, Bits(n));
        for (uint32_t k = 0; k < n; ++k) {
            closed[k].set(k);
            for (uint32_t c : conflicts[k])
                closed[k].set(c);
        }
        Bits remaining(n);
        for (uint32_t k = 0; k < n; ++k)
            remaining.set(k);
        MisSearch search{closed};
        const uint32_t alpha = search.max_separated(remaining, n);
        const uint32_t goal = std::min(input.requested, alpha);
        for (uint32_t k : order) {
            if (placed >= goal)
                break;
            if (!remaining.test(k))
                continue;
            Bits after = remaining;
            after.and_not(closed[k]);
            const uint32_t need = goal - placed - 1;
            if (need == 0 || search.max_separated(after, need) >= need) {
                chosen[k] = true;
                ++placed;
                remaining = std::move(after);
            } else {
                remaining.reset(k);
            }
        }
        if (!search.exhausted) {
            report.max_feasible = alpha;
            report.max_feasible_exact = true;
            exact_done = true;
        } else {
            std::fill(chosen.begin(), chosen.end(), false);
            placed = 0;
            Bits all(n);
            for (uint32_t k = 0; k < n; ++k)
                all.set(k);
            clique_bound = search.clique_cover(all);
        }
    }
    if (!exact_done) {
        // Too many candidates, or a graph that exhausted the search budget, for an
        // exact search at generation time (a documented scaleback): greedy in priority order, then
        // 1-for-2 exchanges — a seed whose removal frees two mutually separated candidates is
        // replaced by them. Each exchange adds a seed, so this ends.
        std::vector<uint32_t> blockers(n, 0);  // chosen seeds within separation of k
        auto add = [&](uint32_t k) {
            chosen[k] = true;
            ++placed;
            for (uint32_t c : conflicts[k])
                ++blockers[c];
        };
        auto remove = [&](uint32_t k) {
            chosen[k] = false;
            --placed;
            for (uint32_t c : conflicts[k])
                --blockers[c];
        };
        auto fill = [&]() {
            for (uint32_t k : order) {
                if (placed >= input.requested)
                    return;
                if (!chosen[k] && blockers[k] == 0)
                    add(k);
            }
        };
        fill();
        while (placed < input.requested) {
            bool improved = false;
            for (uint32_t s : order) {
                if (!chosen[s])
                    continue;
                std::vector<uint32_t> freed;  // blocked by s alone
                for (uint32_t c : conflicts[s])
                    if (!chosen[c] && blockers[c] == 1)
                        freed.push_back(c);
                std::sort(freed.begin(), freed.end(),
                          [&](uint32_t a, uint32_t b) { return rank[a] < rank[b]; });
                for (size_t i = 0; i < freed.size() && !improved; ++i) {
                    for (size_t j = i + 1; j < freed.size(); ++j) {
                        if (conflicts_with(conflicts, freed[i], freed[j]))
                            continue;
                        remove(s);
                        add(freed[i]);
                        add(freed[j]);
                        improved = true;
                        break;
                    }
                }
                if (improved)
                    break;
            }
            if (!improved)
                break;
            fill();
        }

        // Ball cover bound. Any two candidates within floor(s/2) hops of one centre
        // are at most s hops apart, so each ball holds at most one seed.
        const uint32_t radius = input.separation_hops / 2;
        std::vector<std::vector<uint32_t>> balls;
        for (uint32_t v = 0; v < prov_count; ++v) {
            auto ball = candidates_within(input.adjacency, candidate_of, v, radius);
            if (!ball.empty())
                balls.push_back(std::move(ball));
        }
        std::vector<bool> covered(n, false);
        uint32_t uncovered = n;
        uint32_t cover = 0;
        while (uncovered > 0) {
            size_t best_ball = 0;
            uint32_t best_gain = 0;
            for (size_t b = 0; b < balls.size(); ++b) {
                uint32_t gain = 0;
                for (uint32_t k : balls[b])
                    gain += covered[k] ? 0u : 1u;
                if (gain > best_gain) {
                    best_gain = gain;
                    best_ball = b;
                }
            }
            for (uint32_t k : balls[best_ball]) {
                if (!covered[k]) {
                    covered[k] = true;
                    --uncovered;
                }
            }
            ++cover;
        }
        report.max_feasible = std::max(std::min({cover, clique_bound, n}), placed);
        report.max_feasible_exact = report.max_feasible == placed;
    }

    for (uint32_t k : order)
        if (chosen[k])
            result.seeds.push_back(input.candidates[k]);
    report.placed = placed;

    if (placed >= input.requested)
        report.outcome = NationSeedOutcome::achieved;
    else if (placed == report.max_feasible)
        report.outcome = NationSeedOutcome::geography_limited;
    else
        report.outcome = NationSeedOutcome::undetermined;
    return result;
}

}  // namespace econlife
