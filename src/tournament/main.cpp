#include "src/engine/arena.h"
#include "src/bot/ga_bot.h"
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    InitLUT();
    
    // We can play multiple games to see which one performs better.
    // For now, let's pit two identical GABots against each other.
    // In practice, the user can duplicate GABot into GABotV2 with different parameters.
    
    int games = 2;
    int team0_wins = 0;
    int team1_wins = 0;
    int draws = 0;
    
    std::cout << "Starting Arena simulation with " << games << " games..." << std::endl;
    
    for (int i = 0; i < games; ++i) {
        std::shared_ptr<IBot> bot0 = std::make_shared<GABot>();
        std::shared_ptr<IBot> bot1 = std::make_shared<GABot>();
        
        Arena arena(bot0, bot1);
        ArenaResult result = arena.PlayGame(false); // set to true for turn-by-turn logs
        
        if (result.winner_team == 0) team0_wins++;
        else if (result.winner_team == 1) team1_wins++;
        else draws++;
        
        std::cout << "Game " << (i + 1) << " | Winner: Team " << result.winner_team 
                  << " | Turns: " << result.turns 
                  << " | Reason: " << result.reason << std::endl;
    }
    
    std::cout << "--- RESULTS ---" << std::endl;
    std::cout << "Team 0 Wins: " << team0_wins << std::endl;
    std::cout << "Team 1 Wins: " << team1_wins << std::endl;
    std::cout << "Draws: " << draws << std::endl;
    
    return 0;
}
