// player_loop_observe — the human-readable trace of one play session.
//
// The ratchets in player_loop_test.cpp say whether the player can play. This
// says what actually happened to them, so a failing ratchet can be explained
// rather than merely reported. Hidden from the default run (leading dot); run
// it explicitly:
//
//     ./econlife_player_loop_tests "[.player-loop-observe]"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>

#include "player_loop_harness.h"

using namespace econlife;
using namespace econlife::player_loop;

TEST_CASE("player loop: one year in the life", "[.player-loop-observe]") {
    RunConfig cfg{};
    cfg.seed = 42;
    cfg.npc_count = 500;
    cfg.province_count = 6;
    cfg.ticks = 365;
    const char* env = std::getenv("PLAYER_LOOP_BUY_TICK");
    const uint32_t buy_tick = (env != nullptr) ? static_cast<uint32_t>(std::atoi(env)) : 60u;
    cfg.script = buy_a_business_at_tick(buy_tick);

    const PlayerRun r = run(cfg);

    std::printf("\n=== PLAYER LOOP — seed %llu, %u NPCs, %u provinces, %u ticks ===\n\n",
                static_cast<unsigned long long>(cfg.seed), cfg.npc_count, cfg.province_count,
                cfg.ticks);

    std::printf("%6s %11s %7s %7s %5s %5s %9s %9s %11s %6s %6s %5s\n", "tick", "wealth", "age",
                "health", "skl", "biz", "rev/tick", "cost/tick", "biz cash", "cards", "cal",
                "evid");
    std::printf("%6s %11s %7s %7s %5s %5s %9s %9s %11s %6s %6s %5s\n", "----", "------", "---",
                "------", "---", "---", "--------", "---------", "--------", "-----", "---",
                "----");

    for (const auto& s : r.series) {
        // Sample: every tick would be 365 lines of mostly nothing.
        if (s.tick % 30 != 0 && s.tick != r.last().tick)
            continue;
        std::printf("%6u %11.0f %7.2f %7.3f %5.2f %5zu %9.1f %9.1f %11.0f %6zu %6zu %5zu\n", s.tick,
                    static_cast<double>(s.wealth), static_cast<double>(s.age),
                    static_cast<double>(s.health), static_cast<double>(s.max_skill),
                    s.owned_businesses, s.owned_revenue_per_tick, s.owned_cost_per_tick,
                    s.owned_cash, s.pending_cards, s.calendar_entries, s.evidence_awareness);
    }

    std::printf("\n--- the world around them (comparable to the CLI's metrics line) ---\n");
    std::printf("  businesses %zu, facilities %zu, npcs %zu\n", r.world_businesses,
                r.world_facilities, r.world_npcs);
    std::printf("  avg npc capital %.6f\n", r.avg_npc_capital);
    std::printf("  firms with a facility ....... %zu, of which earning %zu\n",
                r.firms_with_facility, r.firms_with_facility_earning);
    std::printf("  firms without one ........... %zu, of which earning %zu\n",
                r.firms_without_facility, r.firms_without_facility_earning);

    std::printf("\n--- what the world put in front of the player ---\n");
    std::printf("  scene cards created ......... %zu\n", r.cards_created);
    std::printf("  ... resolved ................ %zu\n", r.cards_resolved);
    std::printf("  ... retired from the queue .. %zu\n", r.cards_retired);
    std::printf("  ... unanswerable (must be 0)  %zu\n", r.cards_unanswerable);
    std::printf("  ... unaddressable (must be 0) %zu\n", r.cards_with_zero_id);
    std::printf("  peak pending ................ %zu\n", r.max_pending_cards);
    std::printf("  calendar entries created .... %zu\n", r.calendar_entries_created);
    std::printf("  ... not scheduled by player . %zu\n", r.calendar_entries_world_created);

    std::printf("\n--- the character, start to end ---\n");
    std::printf("  wealth ...... %11.0f -> %11.0f\n", static_cast<double>(r.first().wealth),
                static_cast<double>(r.last().wealth));
    std::printf("  age ......... %11.2f -> %11.2f\n", static_cast<double>(r.first().age),
                static_cast<double>(r.last().age));
    std::printf("  health ...... %11.3f -> %11.3f\n", static_cast<double>(r.first().health),
                static_cast<double>(r.last().health));
    std::printf("  exhaustion .. %11.3f -> %11.3f\n", static_cast<double>(r.first().exhaustion),
                static_cast<double>(r.last().exhaustion));
    std::printf("  max skill ... %11.3f -> %11.3f\n", static_cast<double>(r.first().max_skill),
                static_cast<double>(r.last().max_skill));
    std::printf("  reputation .. b %.3f / p %.3f / s %.3f / street %.3f\n",
                static_cast<double>(r.last().rep_business),
                static_cast<double>(r.last().rep_political),
                static_cast<double>(r.last().rep_social), static_cast<double>(r.last().rep_street));
    std::printf("  businesses .. %zu (facilities %zu, workers %u of %u stations)\n",
                r.last().owned_businesses, r.last().owned_facilities, r.last().owned_workers,
                r.last().owned_worker_capacity);
    std::printf("  evidence known %zu, obligations %zu\n\n", r.last().evidence_awareness,
                r.last().obligations);

    SUCCEED();
}

// ---------------------------------------------------------------------------
// The MVP arc, printed: two sittings with the program closed in between.
//
//     ./econlife_player_loop_tests "[.player-loop-two-sittings]"
// ---------------------------------------------------------------------------

#include <filesystem>

#include "core/config/package_config.h"
#include "core/tick/thread_pool.h"
#include "core/tick/tick_orchestrator.h"
#include "core/world_state/player_action_queue.h"
#include "modules/persistence/save_file.h"
#include "modules/register_base_game_modules.h"

namespace {

struct Sitting {
    PackageConfig pkg;  // must outlive the orchestrator: set_config keeps a pointer
    WorldState world;
    TickOrchestrator orch;
    ThreadPool pool{1};

    Sitting() : pkg(load_package_config(econlife::player_loop::find_package_dir("config"))) {
        WorldGeneratorConfig gen{};
        gen.seed = 42;
        gen.province_count = 6;
        gen.npc_count = 500;
        econlife::player_loop::set_content_directories(gen);
        auto result = WorldGenerator::generate_with_player(gen);
        world = std::move(result.world);
        world.player = std::make_unique<PlayerCharacter>(std::move(result.player));
        register_base_game_modules(orch, pkg);
        orch.set_config(pkg);
        orch.finalize_registration();
    }

    void play(uint32_t n, const econlife::player_loop::ActionScript& script = {}) {
        for (uint32_t i = 0; i < n; ++i) {
            if (script)
                script(world, world.current_tick);
            for (const auto& card : world.pending_scene_cards) {
                if (card.chosen_choice_id != 0 || card.choices.empty())
                    continue;
                enqueue_player_action(world, PlayerActionType::scene_card_choice,
                                      SceneCardChoiceAction{card.id, card.choices.front().id});
            }
            orch.execute_tick(world, pool);
        }
    }
};

void report(const char* label, const WorldState& w) {
    const PlayerCharacter& p = *w.player;
    float best_skill = 0.0f;
    for (const auto& sk : p.skills)
        best_skill = std::max(best_skill, sk.level);

    std::size_t firms = 0;
    uint32_t workers = 0, stations = 0;
    double revenue = 0.0, cost = 0.0, biz_cash = 0.0;
    for (const auto& biz : w.npc_businesses) {
        if (biz.owner_id != p.id)
            continue;
        ++firms;
        revenue += static_cast<double>(biz.revenue_per_tick);
        cost += static_cast<double>(biz.cost_per_tick);
        biz_cash += static_cast<double>(biz.cash);
        for (const auto& f : w.facilities) {
            if (f.business_id != biz.id)
                continue;
            workers += f.worker_count;
            stations += f.max_workers;
        }
    }

    std::printf(
        "%-22s tick %5u | wealth %9.0f | age %6.2f | skill %.3f | firms %zu"
        " | %u/%u staffed | %5.1f rev - %5.1f cost | firm cash %8.0f"
        " | cards %zu | cal %zu\n",
        label, w.current_tick, static_cast<double>(p.wealth), static_cast<double>(p.age),
        static_cast<double>(best_skill), firms, workers, stations, revenue, cost, biz_cash,
        w.pending_scene_cards.size(), w.calendar.size());
}

}  // namespace

TEST_CASE("player loop: two sittings, with the game closed in between",
          "[.player-loop-two-sittings]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "econlife_player_loop_saves";
    std::filesystem::create_directories(dir);
    const std::string save = (dir / "two_sittings_observe.econsave").string();

    std::printf("\n=== A BUSINESS CAREER, ACROSS TWO SITTINGS (seed 42, 500 NPCs) ===\n\n");

    {
        Sitting first;
        report("start", first.world);
        first.play(60);
        report("world settled", first.world);
        first.play(1, econlife::player_loop::buy_a_business_at_tick(first.world.current_tick));
        first.play(120);
        report("end of first sitting", first.world);

        const SaveResult sr = save_game(save, first.world, first.orch);
        std::printf("\n  saved %zu bytes to %s (%s)\n", sr.bytes, sr.path.c_str(),
                    sr.ok ? "ok" : sr.error.c_str());
        std::printf("  ...program closed...\n\n");
        REQUIRE(sr.ok);
    }

    Sitting second;
    const SaveResult lr = load_game(save, second.world, second.orch);
    REQUIRE(lr.ok);
    report("picked back up", second.world);
    second.play(180);
    report("end of second sitting", second.world);

    std::printf("\n");
    SUCCEED();
}
