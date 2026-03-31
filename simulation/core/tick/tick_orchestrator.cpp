// TickOrchestrator — topological sort + tick execution engine.
// Modules register via ITickModule interface with runs_after()/runs_before().
// After finalize_registration(), modules are sorted in dependency order.
// Province-parallel modules dispatch to a thread pool (future: ThreadPool integration).

#include "core/tick/tick_orchestrator.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "core/config/package_config.h"

#include "core/tick/drain_deferred_work.h"
#include "core/tick/thread_pool.h"
#include "core/world_state/apply_deltas.h"
#include "core/world_state/world_state.h"  // Complete WorldState + DeltaBuffer definitions

namespace econlife {

void TickOrchestrator::register_module(std::unique_ptr<ITickModule> module) {
    if (finalized_) {
        throw std::logic_error(
            "TickOrchestrator::register_module() called after finalize_registration()");
    }
    modules_.push_back(std::move(module));
}

void TickOrchestrator::finalize_registration() {
    if (finalized_) {
        throw std::logic_error("TickOrchestrator::finalize_registration() called twice");
    }
    resolve_and_sort();
    finalized_ = true;
}

void TickOrchestrator::resolve_and_sort() {
    const size_t n = modules_.size();
    if (n == 0)
        return;

    // Build name -> index map.
    std::unordered_map<std::string, size_t> name_to_idx;
    for (size_t i = 0; i < n; ++i) {
        std::string name(modules_[i]->name());
        if (name_to_idx.count(name)) {
            throw std::runtime_error("Duplicate module name: " + name);
        }
        name_to_idx[name] = i;
    }

    // Build adjacency list and in-degree count.
    // Edge (a -> b) means a must run before b.
    std::vector<std::vector<size_t>> adj(n);
    std::vector<size_t> in_degree(n, 0);

    for (size_t i = 0; i < n; ++i) {
        // runs_after: this module runs after those → edge from them to this
        for (auto dep : modules_[i]->runs_after()) {
            auto it = name_to_idx.find(std::string(dep));
            if (it == name_to_idx.end()) {
                throw std::runtime_error("Module '" + std::string(modules_[i]->name()) +
                                         "' declares runs_after('" + std::string(dep) +
                                         "') but no such module is registered");
            }
            adj[it->second].push_back(i);
            in_degree[i]++;
        }

        // runs_before: this module runs before those → edge from this to them
        for (auto dep : modules_[i]->runs_before()) {
            auto it = name_to_idx.find(std::string(dep));
            if (it == name_to_idx.end()) {
                // runs_before references are soft: if the target module
                // isn't registered (e.g., expansion module not loaded),
                // that's fine — skip silently.
                continue;
            }
            adj[i].push_back(it->second);
            in_degree[it->second]++;
        }
    }

    // Kahn's algorithm for topological sort.
    // Use a min-heap ordered by module name for deterministic tiebreaking
    // when multiple modules have in_degree == 0 simultaneously.
    auto cmp = [this](size_t a, size_t b) {
        return modules_[a]->name() > modules_[b]->name();  // min-heap: smallest name first
    };
    std::priority_queue<size_t, std::vector<size_t>, decltype(cmp)> ready(cmp);
    for (size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<size_t> order;
    order.reserve(n);

    while (!ready.empty()) {
        size_t curr = ready.top();
        ready.pop();
        order.push_back(curr);

        for (size_t next : adj[curr]) {
            if (--in_degree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (order.size() != n) {
        // Cycle detected — identify modules involved.
        std::string cycle_members;
        for (size_t i = 0; i < n; ++i) {
            if (in_degree[i] > 0) {
                if (!cycle_members.empty())
                    cycle_members += ", ";
                cycle_members += std::string(modules_[i]->name());
            }
        }
        throw std::runtime_error("Dependency cycle detected among modules: " + cycle_members);
    }

    // Reorder modules_ according to topological sort.
    std::vector<std::unique_ptr<ITickModule>> sorted;
    sorted.reserve(n);
    for (size_t idx : order) {
        sorted.push_back(std::move(modules_[idx]));
    }
    modules_ = std::move(sorted);
}

void TickOrchestrator::execute_tick(WorldState& state, ThreadPool& thread_pool) {
    assert(finalized_ && "execute_tick() called before finalize_registration()");

    // Step 0: Apply cross-province deltas from previous tick (one-tick propagation delay).
    apply_cross_province_deltas(state);

    // Step 0.5: Reset per-tick flow fields.
    // demand_buffer is a per-tick flow: NPC spending fills it fresh each tick,
    // and the price engine reads it. Without reset, demand accumulates across
    // ticks, breaking the supply/demand ratio.
    for (auto& m : state.regional_markets) {
        m.demand_buffer = 0.0f;
    }

    // Pre-step: Drain deferred work queue — process all items due at current_tick.
    // Items scheduled in tick N fire at the START of tick N+1 (before any module runs).
    // This matches the TDD §6 intent ("Step 2: drain queue") even though it runs before
    // the module loop. The GDD §21 step-count discrepancy (27 vs 28 steps) is an open
    // documentation ambiguity; do not change the drain position without design approval.
    {
        DeltaBuffer dwq_delta;
        DrainConfig dcfg;
        if (config_) {
            dcfg.relationship_decay_interval = config_->consequence_delays.relationship_decay_interval;
            dcfg.evidence_decay_interval = config_->consequence_delays.evidence_decay_interval;
            dcfg.trust_decay_rate_per_batch = config_->relationships.trust_decay_rate_per_batch;
            dcfg.fear_decay_rate_per_batch = config_->relationships.fear_decay_rate_per_batch;
        }
        drain_deferred_work(state, dwq_delta, dcfg);
        apply_deltas(state, dwq_delta);
    }

    for (auto& module : modules_) {
        DeltaBuffer delta;

        if (module->is_province_parallel()) {
            // Province-parallel execution.
            // Execute sequentially per province in ascending index order
            // to maintain determinism. This matches the merge order.
            const uint32_t province_count = static_cast<uint32_t>(state.provinces.size());
            for (uint32_t p = 0; p < province_count; ++p) {
                DeltaBuffer province_delta;
                module->execute_province(p, state, province_delta);
                // Merge province delta into main delta buffer.
                delta.npc_deltas.insert(delta.npc_deltas.end(), province_delta.npc_deltas.begin(),
                                        province_delta.npc_deltas.end());
                // Merge player_delta additively.
                if (province_delta.player_delta.health_delta.has_value()) {
                    delta.player_delta.health_delta =
                        delta.player_delta.health_delta.value_or(0.0f) +
                        *province_delta.player_delta.health_delta;
                }
                if (province_delta.player_delta.wealth_delta.has_value()) {
                    delta.player_delta.wealth_delta =
                        delta.player_delta.wealth_delta.value_or(0.0f) +
                        *province_delta.player_delta.wealth_delta;
                }
                if (province_delta.player_delta.exhaustion_delta.has_value()) {
                    delta.player_delta.exhaustion_delta =
                        delta.player_delta.exhaustion_delta.value_or(0.0f) +
                        *province_delta.player_delta.exhaustion_delta;
                }
                // Replacement fields: last province wins.
                if (province_delta.player_delta.skill_delta.has_value()) {
                    delta.player_delta.skill_delta = province_delta.player_delta.skill_delta;
                }
                if (province_delta.player_delta.new_evidence_awareness.has_value()) {
                    delta.player_delta.new_evidence_awareness =
                        province_delta.player_delta.new_evidence_awareness;
                }
                if (province_delta.player_delta.relationship_delta.has_value()) {
                    delta.player_delta.relationship_delta =
                        province_delta.player_delta.relationship_delta;
                }
                delta.market_deltas.insert(delta.market_deltas.end(),
                                           province_delta.market_deltas.begin(),
                                           province_delta.market_deltas.end());
                delta.evidence_deltas.insert(delta.evidence_deltas.end(),
                                             province_delta.evidence_deltas.begin(),
                                             province_delta.evidence_deltas.end());
                delta.consequence_deltas.insert(delta.consequence_deltas.end(),
                                                province_delta.consequence_deltas.begin(),
                                                province_delta.consequence_deltas.end());
                delta.business_deltas.insert(delta.business_deltas.end(),
                                             province_delta.business_deltas.begin(),
                                             province_delta.business_deltas.end());
                delta.region_deltas.insert(delta.region_deltas.end(),
                                           province_delta.region_deltas.begin(),
                                           province_delta.region_deltas.end());
                delta.currency_deltas.insert(delta.currency_deltas.end(),
                                             province_delta.currency_deltas.begin(),
                                             province_delta.currency_deltas.end());
                delta.technology_deltas.insert(delta.technology_deltas.end(),
                                               province_delta.technology_deltas.begin(),
                                               province_delta.technology_deltas.end());
                delta.new_calendar_entries.insert(delta.new_calendar_entries.end(),
                                                  province_delta.new_calendar_entries.begin(),
                                                  province_delta.new_calendar_entries.end());
                delta.new_scene_cards.insert(delta.new_scene_cards.end(),
                                             province_delta.new_scene_cards.begin(),
                                             province_delta.new_scene_cards.end());
                delta.new_obligation_nodes.insert(delta.new_obligation_nodes.end(),
                                                  province_delta.new_obligation_nodes.begin(),
                                                  province_delta.new_obligation_nodes.end());
                delta.cross_province_deltas.insert(delta.cross_province_deltas.end(),
                                                    province_delta.cross_province_deltas.begin(),
                                                    province_delta.cross_province_deltas.end());
            }

            // Apply province-parallel deltas before global post-pass so the
            // post-pass sees the accumulated province effects.
            apply_deltas(state, delta);

            // Global post-pass: execute() runs after all province deltas are
            // merged and applied, allowing global coordination (transit arrivals,
            // wage equilibration, LOD 1 imports, etc.).
            if (module->has_global_post_pass()) {
                DeltaBuffer post_delta;
                module->execute(state, post_delta);
                apply_deltas(state, post_delta);
            }
        } else {
            // Sequential execution on main thread.
            module->execute(state, delta);

            // Apply this module's deltas to WorldState immediately.
            // Each module sees the effects of all prior modules in this tick.
            apply_deltas(state, delta);
        }
    }

    // Garbage collect inactive evidence tokens to prevent unbounded pool growth.
    // Tokens are marked is_active=false by decay and retirement deltas; remove them
    // at end of tick so no module sees stale references mid-tick.
    state.evidence_pool.erase(std::remove_if(state.evidence_pool.begin(), state.evidence_pool.end(),
                                             [](const EvidenceToken& t) { return !t.is_active; }),
                              state.evidence_pool.end());

    state.current_tick++;
}

}  // namespace econlife
