#pragma once
#include "src/engine/engine.h"
#include <vector>
#include <string>

// GA-specific BotConfig lives with the bot product: src/cg/bot_config.h
// Engine only defines the thin IBot contract (Fowler: Move Field / SRP).

class IBot {
public:
    virtual ~IBot() = default;
    virtual std::string GetName() const = 0;

    virtual void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) = 0;

    virtual std::vector<PodAction> GetActions(const std::vector<Pod>& pods) = 0;
    virtual void SetRoles(int runner_idx, int blocker_idx) {}
};
