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
