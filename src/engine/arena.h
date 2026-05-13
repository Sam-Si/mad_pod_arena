#pragma once
#include "engine.h"
#include "bot.h"
#include <memory>
#include <vector>

struct ArenaResult {
    int winner_team; // 0 or 1. -1 for draw.
    int turns;
    std::string reason;
};

class Arena {
    int laps_;
    int cp_count_;
    std::vector<Vec2> cps_;
    std::vector<Pod> pods_; // 0,1 for team 0. 2,3 for team 1.
    
    std::shared_ptr<IBot> bot0_;
    std::shared_ptr<IBot> bot1_;

    void GenerateMap();

public:
    Arena(std::shared_ptr<IBot> bot0, std::shared_ptr<IBot> bot1);
    ArenaResult PlayGame(bool verbose = false);
};
