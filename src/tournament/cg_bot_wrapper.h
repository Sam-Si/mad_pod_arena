#pragma once

#include <vector>
#include <string>
#include <memory>
#include "src/engine/bot.h"

// =============================================================================
// CG Bot Wrapper
//
// Adapts the cg_bot.cpp GA bot to the engine's IBot interface for benchmarks.
// Since cg_bot.cpp now uses the shared engine types (via #include "engine.h"),
// we only need to rename 'main' to avoid symbol conflicts.
// =============================================================================

#define main cg_main
#include "cg/cg_bot.cpp"
#undef main

#include <cstdlib>

class CGBotWrapper : public IBot {
    GABot bot_;

public:
    CGBotWrapper(double time_budget_ms = 7.5) {
        InitLUT();
        BotConfig config;
        const char* env_threads = std::getenv("BOT_THREADS");
        if (env_threads) {
            config.num_threads = std::max(1, std::atoi(env_threads));
        } else {
            config.num_threads = 1;  // Single-threaded for safe parallel benchmarking
        }
        config.turn_time_limit_ms = time_budget_ms;
        config.first_turn_time_limit_ms = std::min(1000.0, time_budget_ms * 20.0);
        bot_ = GABot(config);
    }

    std::string GetName() const override {
        return "CGBot";
    }

    void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps,
                    int team_id) override {
        bot_.Initialize(laps, cp_count, cps, team_id);
    }

    std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override {
        return bot_.GetActions(pods);
    }
};
