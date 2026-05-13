#include "src/engine/arena.h"
#include <iostream>

// All 18 maps captured from real CodinGame server
static const std::vector<std::vector<Vec2>> ALL_MAPS = {
    // Map 0: Cross variant
    { Vec2(12929, 7191), Vec2(5614, 2557), Vec2(4114, 7440), Vec2(13515, 2340) },
    // Map 1: Hostile territories
    { Vec2(13584, 7626), Vec2(12449, 1355), Vec2(10519, 6003), Vec2(3593, 5174) },
    // Map 2: Tramway (6 CPs)
    { Vec2(14075, 7765), Vec2(13888, 1202), Vec2(10257, 4931), Vec2(6104, 2204), Vec2(3049, 5211), Vec2(6260, 7743) },
    // Map 3: Dalton
    { Vec2(9426, 7247), Vec2(5962, 4254), Vec2(14674, 1436), Vec2(3442, 7215) },
    // Map 4: Triangle
    { Vec2(5033, 5264), Vec2(11472, 6064), Vec2(9081, 1865) },
    // Map 5: Tramway variant (6 CPs)
    { Vec2(13095, 2313), Vec2(4579, 2152), Vec2(7377, 4920), Vec2(3303, 7243), Vec2(14551, 7688), Vec2(10577, 5043) },
    // Map 6: Makbilit
    { Vec2(2645, 7027), Vec2(10065, 5940), Vec2(13925, 1916), Vec2(8004, 3244) },
    // Map 7: Arrow
    { Vec2(14633, 1420), Vec2(3428, 7230), Vec2(9449, 7224), Vec2(5962, 4253) },
    // Map 8: Reverse Tramway (6 CPs)
    { Vec2(3031, 5179), Vec2(6271, 7752), Vec2(14096, 7753), Vec2(13873, 1231), Vec2(10258, 4890), Vec2(6128, 2203) },
    // Map 9: Diamond
    { Vec2(11202, 5412), Vec2(7244, 6630), Vec2(5403, 2840), Vec2(10293, 3376) },
    // Map 10: Triangle variant
    { Vec2(6000, 5375), Vec2(11322, 2825), Vec2(7508, 6916) },
    // Map 11: Diamond variant
    { Vec2(5406, 2811), Vec2(10302, 3339), Vec2(11231, 5436), Vec2(7267, 6667) },
    // Map 12: Tilted square
    { Vec2(9547, 1383), Vec2(3654, 4439), Vec2(7977, 7904), Vec2(13322, 5535) },
    // Map 13: Tilted square (reverse)
    { Vec2(13310, 5555), Vec2(9561, 1374), Vec2(3636, 4433), Vec2(7981, 7891) },
    // Map 14: Tilted square variant
    { Vec2(13283, 5513), Vec2(9560, 1394), Vec2(3652, 4444), Vec2(7997, 7872) },
    // Map 15: Tramway variant 2 (6 CPs)
    { Vec2(6306, 7766), Vec2(14117, 7743), Vec2(13885, 1197), Vec2(10229, 4926), Vec2(6102, 2199), Vec2(2991, 5197) },
    // Map 16: Tilted square variant 2
    { Vec2(13311, 5519), Vec2(9585, 1426), Vec2(3615, 4419), Vec2(7974, 7919) },
    // Map 17: Hostile territories variant
    { Vec2(12435, 1353), Vec2(10563, 5965), Vec2(3558, 5170), Vec2(13579, 7616) },
};

Arena::Arena(std::shared_ptr<IBot> bot0, std::shared_ptr<IBot> bot1) 
    : bot0_(bot0), bot1_(bot1) {}

int Arena::GetMapCount() { return ALL_MAPS.size(); }

void Arena::GenerateMap(int map_idx) {
    laps_ = 3;
    
    if (map_idx < 0 || map_idx >= (int)ALL_MAPS.size()) {
        map_idx = FastRandInt(0, ALL_MAPS.size() - 1);
    }
    cps_ = ALL_MAPS[map_idx];
    cp_count_ = cps_.size();

    pods_.resize(4);
    for (int i = 0; i < 4; ++i) {
        pods_[i] = Pod();
        pods_[i].id = i;
        pods_[i].team = i / 2;
        
        // Place pods near CP 0, spread out
        double offset_x = (i == 0 || i == 1) ? -400 : 400;
        double offset_y = (i == 0 || i == 2) ? -400 : 400;
        
        pods_[i].pos.x = cps_[0].x + offset_x;
        pods_[i].pos.y = cps_[0].y + offset_y;
        
        Vec2 dir = cps_[1].Sub(pods_[i].pos);
        pods_[i].angle = std::round(GameEngine::NormalizeAngle(GameEngine::RadToDeg(std::atan2(dir.y, dir.x))));
        pods_[i].next_cp_id = 1;
    }
}

ArenaResult Arena::PlayGame(bool verbose, int map_idx) {
    GenerateMap(map_idx);

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
            
            if (pods_[i].pos.DistanceSq(cps_[pods_[i].next_cp_id]) <= 360000) {
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

        if (turn >= 1000) {
            return {-1, turn, "Draw (Max turns reached)"};
        }
    }
}
