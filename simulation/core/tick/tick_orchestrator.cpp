// TickOrchestrator — topological sort + tick execution engine.
// Modules register via ITickModule interface with runs_after()/runs_before().
// After finalize_registration(), modules are sorted in dependency order.
// Province-parallel modules dispatch to ThreadPool for concurrent execution.

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

    // Resolve config pointers once for this tick.
    const SafetyCeilingsConfig* sc = config_ ? &config_->safety_ceilings : nullptr;

    // Step 0: Apply cross-province deltas from previous tick (one-tick propagation delay).
    apply_cross_province_deltas(state);

    // Step 0.5: Reset per-tick flow fields and apply supply decay.
    // demand_buffer is a per-tick flow: NPC spending fills it fresh each tick,
    // and the price engine reads it. Without reset, demand accumulates across
    // ticks, breaking the supply/demand ratio.
    //
    // Supply decay prevents unbounded accumulation when production exceeds
    // consumption. The decay rate is configurable (default 2% per tick).
    // This models spoilage, obsolescence, and wastage.
    const float surplus_decay_rate =
        config_ ? config_->supply_chain.surplus_decay_rate : 0.02f;
    for (auto& m : state.regional_markets) {
        m.demand_buffer = 0.0f;
        if (m.supply > 0.0f) {
            m.supply *= (1.0f - surplus_decay_rate);
        }
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
        apply_deltas(state, dwq_delta, sc, config_);
    }

    for (auto& module : modules_) {
        DeltaBuffer delta;

        if (module->is_province_parallel()) {
            // Province-parallel execution.
            // Each province writes to its own DeltaBuffer; results are merged
            // in ascending province index order to maintain determinism.
            const uint32_t province_count = static_cast<uint32_t>(state.provinces.size());
            std::vector<DeltaBuffer> province_deltas(province_count);

            // Pre-parallel initialization: runs on main thread before dispatch.
            // Modules pre-populate mutable records so execute_province() only
            // accesses records for entities in its own province.
            module->init_for_tick(state);

            // ThreadPool::parallel_for dispatches to worker threads when
            // num_threads > 1, or runs inline when num_threads == 1.
            // state is passed as const ref — safe for concurrent reads.
            thread_pool.parallel_for(province_count, [&](uint32_t p) {
                module->execute_province(p, state, province_deltas[p]);
            });

            // Merge province deltas in ascending index order (deterministic).
            // For PlayerDelta replacement fields this means highest province_id
            // wins; vector deltas concatenate in ascending province order.
            for (uint32_t p = 0; p < province_count; ++p) {
                delta.merge_from(std::move(province_deltas[p]));
            }

            // Apply province-parallel deltas before global post-pass so the
            // post-pass sees the accumulated province effects.
            apply_deltas(state, delta, sc, config_);

            // Global post-pass: execute() runs after all province deltas are
            // merged and applied, allowing global coordination (transit arrivals,
            // wage equilibration, LOD 1 imports, etc.).
            if (module->has_global_post_pass()) {
                DeltaBuffer post_delta;
                module->execute(state, post_delta);
                apply_deltas(state, post_delta, sc, config_);
            }
        } else {
            // Sequential execution on main thread.
            module->execute(state, delta);

            // Apply this module's deltas to WorldState immediately.
            // Each module sees the effects of all prior modules in this tick.
            apply_deltas(state, delta, sc, config_);
        }
    }

    // Garbage collect evidence tokens to prevent unbounded pool growth:
    // (a) Inactive tokens — retired/suppressed via decay or retirement deltas.
    // (b) Age-expired tokens — older than evidence_max_age_ticks regardless of decay_rate.
    //     Tokens with decay_rate=0 never self-retire; hard expiry caps the pool size.
    {
        const uint32_t max_age =
            config_ ? config_->consequence_delays.evidence_max_age_ticks : 1000u;
        state.evidence_pool.erase(
            std::remove_if(state.evidence_pool.begin(), state.evidence_pool.end(),
                           [&](const EvidenceToken& t) {
                               return !t.is_active ||
                                      (state.current_tick > t.created_tick &&
                                       state.current_tick - t.created_tick > max_age);
                           }),
            state.evidence_pool.end());
    }

    state.current_tick++;
}

}  // namespace econlife
