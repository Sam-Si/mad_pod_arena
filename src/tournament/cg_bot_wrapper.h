#pragma once

#include <memory>
#include <vector>
#include <string>
#include <cstdlib>
#include <algorithm>
#include "src/engine/bot.h"
#include "src/cg/ga_bot.h"

// Adapts CreateGABot() to IBot for benchmarks (links //src/cg:ga_bot).
class CGBotWrapper : public IBot {
    std::unique_ptr<IBot> bot_;

public:
    CGBotWrapper(double time_budget_ms = 7.5) {
        InitLUT();
        bot_ = CreateGABot(time_budget_ms);
    }

    std::string GetName() const override { return bot_->GetName(); }

    void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps,
                    int team_id) override {
        bot_->Initialize(laps, cp_count, cps, team_id);
    }

    std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override {
        return bot_->GetActions(pods);
    }

    void SetRoles(int runner_idx, int blocker_idx) override {
        bot_->SetRoles(runner_idx, blocker_idx);
    }
};
