#pragma once

// Shared harness for player-loop integration tests — the player-facing
// counterpart to emergence_harness.h.
//
// The emergence suite proves the WORLD behaves: loops close, conditions move,
// civilisations rise and fall. Nothing proved that a PLAYER could participate
// in it. That gap is why a year-long run in which the player earned nothing,
// aged not at all and accumulated unreadable cards passed every one of ~1,600
// tests: they assert that modules work in isolation.
//
// This harness boots a V1-scale generated world, drives a scripted player
// through the real base-game tick orchestrator, and records what a human would
// actually notice — money, time, the state of their business, what the world
// put in front of them and whether they could answer it.
//
// Used by player_loop_observe.cpp (dumps the trace) and player_loop_test.cpp
// (asserts the MVP ratchets).

#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_gen/world_generator.h"
#include "core/world_state/player.h"
#include "core/world_state/player_action_queue.h"
#include "core/world_state/world_state.h"
#include "modules/register_base_game_modules.h"

namespace econlife::player_loop {

inline std::string find_goods_dir() {
    namespace fs = std::filesystem;
    static const char* candidates[] = {
        "packages/base_game/goods",
        "../packages/base_game/goods",
        "../../packages/base_game/goods",
        "../../../packages/base_game/goods",
    };
    for (const auto* c : candidates) {
        if (fs::exists(c) && fs::is_directory(c))
            return fs::canonical(c).string();
    }
    return "";
}

// ---------------------------------------------------------------------------
// What a human would notice, sampled each tick.
// ---------------------------------------------------------------------------
struct PlayerSnapshot {
    uint32_t tick = 0;

    // Money and the character themselves.
    float wealth = 0.0f;
    float age = 0.0f;
    float health = 0.0f;
    float exhaustion = 0.0f;
    float rep_business = 0.0f, rep_political = 0.0f, rep_social = 0.0f, rep_street = 0.0f;
    float max_skill = 0.0f;
    std::size_t skills_exercised = 0;  // skills with level > 0

    // The business they run.
    std::size_t owned_businesses = 0;
    double owned_revenue_per_tick = 0.0;
    double owned_cost_per_tick = 0.0;
    double owned_cash = 0.0;
    std::size_t owned_facilities = 0;

    // What the world put in front of them.
    std::size_t pending_cards = 0;
    std::size_t calendar_entries = 0;
    std::size_t evidence_awareness = 0;
    std::size_t obligations = 0;
};

// ---------------------------------------------------------------------------
// Whole-run telemetry. A ratchet that fails should say WHY, not just fail, so
// the run records the shape of the play session and not only its endpoints.
// ---------------------------------------------------------------------------
struct PlayerRun {
    std::vector<PlayerSnapshot> series;

    // Scene-card flow across the whole run.
    std::size_t cards_created = 0;    // distinct card ids ever seen
    std::size_t cards_resolved = 0;   // cards the player (or a default) answered
    std::size_t cards_retired = 0;    // cards that left the queue
    std::size_t cards_unanswerable = 0;  // cards with no choices — must stay 0
    std::size_t cards_with_zero_id = 0;  // unaddressable — must stay 0
    std::size_t max_pending_cards = 0;

    // Calendar provenance: entries the player did not put there themselves.
    std::size_t calendar_entries_created = 0;
    std::size_t calendar_entries_world_created = 0;

    const PlayerSnapshot& first() const { return series.front(); }
    const PlayerSnapshot& last() const { return series.back(); }
};

inline PlayerSnapshot capture(const WorldState& w) {
    PlayerSnapshot s{};
    s.tick = w.current_tick;
    if (!w.player)
        return s;
    const PlayerCharacter& p = *w.player;

    s.wealth = p.wealth;
    s.age = p.age;
    s.health = p.health.current_health;
    s.exhaustion = p.health.exhaustion_accumulator;
    s.rep_business = p.reputation.public_business;
    s.rep_political = p.reputation.public_political;
    s.rep_social = p.reputation.public_social;
    s.rep_street = p.reputation.street;
    for (const auto& sk : p.skills) {
        if (sk.level > 0.0f)
            ++s.skills_exercised;
        s.max_skill = std::max(s.max_skill, sk.level);
    }

    std::set<uint32_t> owned_ids;
    for (const auto& biz : w.npc_businesses) {
        if (biz.owner_id != p.id)
            continue;
        ++s.owned_businesses;
        owned_ids.insert(biz.id);
        s.owned_revenue_per_tick += biz.revenue_per_tick;
        s.owned_cost_per_tick += biz.cost_per_tick;
        s.owned_cash += biz.cash;
    }
    for (const auto& f : w.facilities) {
        if (owned_ids.count(f.business_id) != 0)
            ++s.owned_facilities;
    }

    s.pending_cards = w.pending_scene_cards.size();
    s.calendar_entries = w.calendar.size();
    s.evidence_awareness = p.evidence_awareness_map.size();
    s.obligations = p.obligation_node_ids.size();
    return s;
}

// A script may enqueue player actions on any tick. It is called BEFORE the
// tick executes, with the world as the player would see it.
using ActionScript = std::function<void(WorldState&, uint32_t /*tick*/)>;

struct RunConfig {
    uint64_t seed = 42;
    uint32_t npc_count = 500;
    uint32_t province_count = 6;
    uint32_t ticks = 365;
    ActionScript script{};
    // Answer every answerable card with its first choice, the tick after it
    // appears. A real player clears their queue; without this the run cannot
    // tell "cards are resolvable" from "cards are never touched".
    bool auto_answer_cards = true;
};

// ---------------------------------------------------------------------------
// run — drive a scripted player through the real orchestrator.
// ---------------------------------------------------------------------------
inline PlayerRun run(const RunConfig& cfg) {
    WorldGeneratorConfig gen{};
    gen.seed = cfg.seed;
    gen.province_count = cfg.province_count;
    gen.npc_count = cfg.npc_count;
    gen.goods_directory = find_goods_dir();

    auto [world, player] = WorldGenerator::generate_with_player(gen);
    world.player = std::make_unique<PlayerCharacter>(std::move(player));

    TickOrchestrator orch;
    register_base_game_modules(orch);
    orch.finalize_registration();
    ThreadPool pool(1);

    PlayerRun out;
    out.series.reserve(cfg.ticks + 1);
    out.series.push_back(capture(world));

    std::set<uint32_t> ever_seen;      // card ids that have existed
    std::set<uint32_t> ever_resolved;  // card ids observed with a choice recorded
    std::set<uint32_t> live;           // card ids in the queue last tick
    std::set<uint32_t> calendar_ever;  // calendar entry ids that have existed
    std::set<uint32_t> player_scheduled;  // entries the script asked for

    for (uint32_t t = 0; t < cfg.ticks; ++t) {
        if (cfg.script)
            cfg.script(world, world.current_tick);

        if (cfg.auto_answer_cards && world.player) {
            for (const auto& card : world.pending_scene_cards) {
                if (card.chosen_choice_id != 0 || card.choices.empty())
                    continue;
                enqueue_player_action(world, PlayerActionType::scene_card_choice,
                                      SceneCardChoiceAction{card.id, card.choices.front().id});
            }
        }

        orch.execute_tick(world, pool);

        // Card accounting. Ids are monotonic and never reused, so set
        // membership is a sound way to count the flow.
        std::set<uint32_t> now;
        for (const auto& card : world.pending_scene_cards) {
            now.insert(card.id);
            ever_seen.insert(card.id);
            if (card.id == 0)
                ++out.cards_with_zero_id;
            if (card.choices.empty())
                ++out.cards_unanswerable;
            if (card.chosen_choice_id != 0)
                ever_resolved.insert(card.id);
        }
        for (uint32_t id : live) {
            if (now.count(id) == 0)
                ++out.cards_retired;
        }
        live = std::move(now);
        out.max_pending_cards = std::max(out.max_pending_cards, world.pending_scene_cards.size());

        for (const auto& e : world.calendar) {
            if (calendar_ever.insert(e.id).second)
                ++out.calendar_entries_created;
        }

        out.series.push_back(capture(world));
    }

    out.cards_created = ever_seen.size();
    out.cards_resolved = ever_resolved.size();
    // Provenance is decided by the caller marking its own scheduling; by
    // default every entry that exists came from the world, because the
    // scripted player here never schedules anything.
    out.calendar_entries_world_created = out.calendar_entries_created;
    return out;
}

// ---------------------------------------------------------------------------
// Convenience: the standard MVP play session. The player buys the cheapest
// going concern in their province that they can pay cash for, then runs it.
// ---------------------------------------------------------------------------
inline ActionScript buy_a_business_at_tick(uint32_t at_tick, float offer_multiple = 7.0f) {
    return [at_tick, offer_multiple](WorldState& w, uint32_t tick) {
        if (tick != at_tick || !w.player)
            return;
        const NPCBusiness* best = nullptr;
        float best_price = 0.0f;
        for (const auto& biz : w.npc_businesses) {
            if (biz.owner_id == w.player->id)
                continue;
            if (biz.province_id != w.player->current_province_id)
                continue;
            if (biz.revenue_per_tick <= 0.0f)
                continue;
            if (biz.criminal_sector)
                continue;  // the MVP career path is the legitimate one
            const float price = biz.revenue_per_tick * 30.0f * offer_multiple;
            if (price > w.player->wealth)
                continue;  // cash purchase only, so the loop is not confounded
                           // by debt service
            if (best == nullptr || price < best_price) {
                best = &biz;
                best_price = price;
            }
        }
        if (best == nullptr)
            return;
        AcquireBusinessAction a{};
        a.business_id = best->id;
        a.offer_multiple = offer_multiple;
        a.payment_method = PaymentMethod::cash;
        a.down_payment_fraction = 1.0f;
        enqueue_player_action(w, PlayerActionType::acquire_business, a);
    };
}

}  // namespace econlife::player_loop
