#include "src/engine/arena.h"
#include <iostream>

Arena::Arena(std::shared_ptr<IBot> bot0, std::shared_ptr<IBot> bot1) 
    : bot0_(bot0), bot1_(bot1) {}

void Arena::GenerateMap() {
    laps_ = 3;
    
    std::vector<std::vector<Vec2>> maps = {
        { Vec2(14572, 7679), Vec2(10575, 5049), Vec2(13085, 2313), Vec2(4558, 2166), Vec2(7325, 4955), Vec2(3299, 7205) },
        { Vec2(5404, 2849), Vec2(10346, 3341), Vec2(11212, 5420), Vec2(7269, 6652) },
        { Vec2(4087, 7396), Vec2(13489, 2345), Vec2(12935, 7211), Vec2(5644, 2597) },
        { Vec2(10317, 3394), Vec2(11204, 5427), Vec2(7259, 6675), Vec2(5415, 2851) },
        { Vec2(7275, 6659), Vec2(5409, 2859), Vec2(10306, 3365), Vec2(11212, 5452) }
    };
    
    int map_idx = FastRandInt(0, maps.size() - 1);
    cps_ = maps[map_idx];
    cp_count_ = cps_.size();

    pods_.resize(4);
    for (int i = 0; i < 4; ++i) {
        pods_[i] = Pod();
        pods_[i].id = i;
        pods_[i].team = i / 2;
        
        // Place pods near CP 0, spread out a bit
        double offset_x = (i == 0 || i == 1) ? -400 : 400;
        double offset_y = (i == 0 || i == 2) ? -400 : 400;
        
        pods_[i].pos.x = cps_[0].x + offset_x;
        pods_[i].pos.y = cps_[0].y + offset_y;
        
        Vec2 dir = cps_[1].Sub(pods_[i].pos);
        pods_[i].angle = std::round(GameEngine::NormalizeAngle(GameEngine::RadToDeg(std::atan2(dir.y, dir.x))));
        pods_[i].next_cp_id = 1;
    }
}

ArenaResult Arena::PlayGame(bool verbose) {
    GenerateMap();

    bot0_->Initialize(laps_, cp_count_, cps_, 0);
    bot1_->Initialize(laps_, cp_count_, cps_, 1);

    int turn = 0;
    while (true) {
        turn++;
        if (verbose) std::cout << "--- Turn " << turn << " ---" << std::endl;

        std::vector<PodAction> actions0 = bot0_->GetActions(pods_);
        std::vector<PodAction> actions1 = bot1_->GetActions(pods_);

        // Apply actions
        pods_[0].ApplyServerAction(actions0[0].tx, actions0[0].ty, actions0[0].thrust);
        pods_[1].ApplyServerAction(actions0[1].tx, actions0[1].ty, actions0[1].thrust);
        pods_[2].ApplyServerAction(actions1[0].tx, actions1[0].ty, actions1[0].thrust);
        pods_[3].ApplyServerAction(actions1[1].tx, actions1[1].ty, actions1[1].thrust);

        // Simulate physics
        PhysicsSimulator::SimulateTurn(pods_);

        // Check CPs, timeouts, and wins
        bool team0_won = false;
        bool team1_won = false;
        bool team0_eliminated = true;
        bool team1_eliminated = true;

        for (int i = 0; i < 4; ++i) {
            pods_[i].timeout++;
            
            // Reached CP?
            if (pods_[i].pos.DistanceSq(cps_[pods_[i].next_cp_id]) <= 360000) { // 600 radius
                pods_[i].next_cp_id++;
                pods_[i].timeout = 0;
                
                if (pods_[i].next_cp_id >= cp_count_) {
                    pods_[i].next_cp_id = 0;
                    pods_[i].laps_completed++;
                    if (pods_[i].laps_completed == laps_) {
                        if (pods_[i].team == 0) team0_won = true;
                        else team1_won = true;
                    }
                }
            }

            if (pods_[i].timeout < 100) {
                if (pods_[i].team == 0) team0_eliminated = false;
                else team1_eliminated = false;
            }
        }

        if (team0_won && team1_won) return {-1, turn, "Draw (both teams finished on same turn)"};
        if (team0_won) return {0, turn, "Team 0 finished the race"};
        if (team1_won) return {1, turn, "Team 1 finished the race"};
        
        if (team0_eliminated && team1_eliminated) return {-1, turn, "Draw (both eliminated by timeout)"};
        if (team0_eliminated) return {1, turn, "Team 1 won (Team 0 eliminated by timeout)"};
        if (team1_eliminated) return {0, turn, "Team 0 won (Team 1 eliminated by timeout)"};

        // Safety limit to avoid infinite loops in case of very bad bots
        if (turn >= 1000) {
            return {-1, turn, "Draw (Max turns reached)"};
        }
    }
}
