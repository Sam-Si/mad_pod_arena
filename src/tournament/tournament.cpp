#include "src/engine/arena.h"
#include "src/bot/ga_bot.h"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <future>
#include <iomanip>

struct Player {
    BotConfig config;
    int elo = 1200;
    int wins = 0;
    int losses = 0;
    int draws = 0;
};

BotConfig RandomConfig(int id) {
    BotConfig c;
    c.name = "Bot_" + std::to_string(id);
    c.horizon = (rand() % 2 == 0) ? 4 : 6;
    c.population = (rand() % 2 == 0) ? 20 : 40;
    
    double dists[] = {0.5, 1.0, 2.0};
    c.dist_weight = dists[rand() % 3];
    
    double aligns[] = {1.0, 3.0, 5.0};
    c.align_weight = aligns[rand() % 3];
    
    double blocks[] = {0.0, 2.0, 5.0};
    c.block_weight = blocks[rand() % 3];
    
    double shields[] = {0.0, 50.0};
    c.shield_penalty = shields[rand() % 2];
    
    return c;
}

// Play a single match between two bots and return the winner (0 for bot A, 1 for bot B, -1 for draw)
int PlayMatch(BotConfig confA, BotConfig confB) {
    auto botA = std::make_shared<GABot>(confA);
    auto botB = std::make_shared<GABot>(confB);
    Arena arena(botA, botB);
    ArenaResult res = arena.PlayGame(false);
    return res.winner_team;
}

int main() {
    InitLUT();
    srand(42); // deterministic seed for tournament generation
    
    int num_bots = 4096; // Large simulation // Even number for pairing
    std::vector<Player> players(num_bots);
    for (int i = 0; i < num_bots; ++i) {
        players[i].config = RandomConfig(i);
    }
    
    int num_rounds = 30; // ~30 min run depending on threads
    
    std::cout << "Starting Swiss Tournament with " << num_bots << " bots over " << num_rounds << " rounds." << std::endl;
    
    for (int round = 1; round <= num_rounds; ++round) {
        std::cout << "\n--- ROUND " << round << " ---" << std::endl;
        
        // Sort by wins to pair players with similar scores
        std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
            if (a.wins != b.wins) return a.wins > b.wins;
            return a.elo > b.elo;
        });
        
        std::vector<std::future<std::pair<int, int>>> match_results;
        
        // Pair players: 0 vs 1, 2 vs 3, etc.
        for (int i = 0; i < num_bots; i += 2) {
            BotConfig cA = players[i].config;
            BotConfig cB = players[i + 1].config;
            
            match_results.push_back(std::async(std::launch::async, [cA, cB]() {
                // To be fair, they play two games, swapping sides
                int winsA = 0;
                int winsB = 0;
                
                int res1 = PlayMatch(cA, cB);
                if (res1 == 0) winsA++;
                else if (res1 == 1) winsB++;
                
                int res2 = PlayMatch(cB, cA);
                if (res2 == 0) winsB++;
                else if (res2 == 1) winsA++;
                
                return std::make_pair(winsA, winsB);
            }));
        }
        
        // Collect results
        int match_idx = 0;
        for (int i = 0; i < num_bots; i += 2) {
            auto [winsA, winsB] = match_results[match_idx].get();
            match_idx++;
            
            // Update scores
            players[i].wins += winsA;
            players[i + 1].wins += winsB;
            players[i].losses += winsB;
            players[i + 1].losses += winsA;
            players[i].draws += (2 - winsA - winsB);
            
            // Simple Elo update (K=32)
            double expectedA = 1.0 / (1.0 + std::pow(10.0, (players[i+1].elo - players[i].elo) / 400.0));
            double scoreA = (winsA + (2 - winsA - winsB)*0.5) / 2.0;
            
            int elo_change = 32 * (scoreA - expectedA);
            players[i].elo += elo_change;
            players[i+1].elo -= elo_change;
        }
        
        std::cout << "Round " << round << " completed. Top bot: " << players[0].config.name << " with " << players[0].wins << " wins." << std::endl;
    }
    
    // Final Sort
    std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
        if (a.wins != b.wins) return a.wins > b.wins;
        return a.elo > b.elo;
    });
    
    std::cout << "\n=====================================" << std::endl;
    std::cout << "        TOURNAMENT RESULTS           " << std::endl;
    std::cout << "=====================================" << std::endl;
    
    for (int i = 0; i < 10; ++i) {
        const auto& p = players[i];
        std::cout << std::setw(2) << (i+1) << ". " << std::setw(10) << p.config.name 
                  << " | Wins: " << std::setw(2) << p.wins 
                  << " | Elo: " << p.elo 
                  << " | H=" << p.config.horizon << ", P=" << p.config.population
                  << ", D=" << p.config.dist_weight << ", A=" << p.config.align_weight 
                  << ", B=" << p.config.block_weight << ", S=" << p.config.shield_penalty << std::endl;
    }
    
    return 0;
}
