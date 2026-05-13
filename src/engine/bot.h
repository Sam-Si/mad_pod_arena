#pragma once
#include "src/engine/engine.h"
#include <vector>

struct BotConfig {
    int horizon = 6;
    int population = 50;
    double dist_weight = 1.0;
    double align_weight = 3.0;
    double block_weight = 0.0;
    double shield_penalty = 0.0;
    std::string name = "DefaultGA";
};

class IBot {
public:
    virtual ~IBot() = default;
    virtual std::string GetName() const = 0;
    
    // Called once per game before the first turn
    virtual void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) = 0;
    
    // Called every turn. Return exactly 2 PodActions (one for each of your pods)
    virtual std::vector<PodAction> GetActions(const std::vector<Pod>& pods) = 0;
};
