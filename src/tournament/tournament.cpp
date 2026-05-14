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
    
    // GA Core (continuous sampling)
    c.horizon = 4 + FastRandInt(0, 4);                     // 4..8
    c.population = 20 + FastRandInt(0, 6) * 10;            // 20..80
    
    // Runner Evaluation
    c.dist_weight = 0.5 + FastRandInt(0, 30) / 10.0;       // 0.5..3.5
    c.align_weight = 0.5 + FastRandInt(0, 40) / 10.0;      // 0.5..4.5
    c.speed_bonus = FastRandInt(0, 10) / 10.0;              // 0.0..1.0
    c.lateral_penalty = FastRandInt(0, 20) / 10.0;          // 0.0..2.0
    c.angle_penalty = FastRandInt(5, 60);                   // 5..60
    c.corner_cut_dist = 200 + FastRandInt(0, 6) * 100;     // 200..800
    
    // Blocker
    c.block_weight = FastRandInt(0, 100) / 10.0;            // 0.0..10.0
    c.shield_penalty = FastRandInt(0, 100);                 // 0..100
    c.shield_ram_dist = 600 + FastRandInt(0, 5) * 100;     // 600..1100
    
    // Coordination
    c.opp_penalty = FastRandInt(0, 30) / 10.0;             // 0.0..3.0
    
    // Time allocation
    c.opp_model_ms = 5.0 + FastRandInt(0, 9) * 5.0;       // 5..50ms
    
    return c;
}

void PrintConfig(const BotConfig& c) {
    std::cout << "  H=" << c.horizon << " P=" << c.population
              << " | dist=" << c.dist_weight << " align=" << c.align_weight
              << " speed=" << c.speed_bonus << " lat=" << c.lateral_penalty
              << " angle=" << c.angle_penalty << " corner=" << c.corner_cut_dist
              << " | block=" << c.block_weight << " shield=" << c.shield_penalty
              << " ram=" << c.shield_ram_dist
              << " | opp=" << c.opp_penalty
              << " | opp_ms=" << c.opp_model_ms << std::endl;
}

void PrintCopyableConfig(const BotConfig& w, const std::string& label = "") {
    if (!label.empty()) std::cout << "// === " << label << " ===" << std::endl;
    std::cout << "    BotConfig config;" << std::endl;
    std::cout << "    config.name = \"" << w.name << "\";" << std::endl;
    std::cout << "    config.horizon = " << w.horizon << ";" << std::endl;
    std::cout << "    config.population = " << w.population << ";" << std::endl;
    std::cout << "    config.dist_weight = " << w.dist_weight << ";" << std::endl;
    std::cout << "    config.align_weight = " << w.align_weight << ";" << std::endl;
    std::cout << "    config.speed_bonus = " << w.speed_bonus << ";" << std::endl;
    std::cout << "    config.lateral_penalty = " << w.lateral_penalty << ";" << std::endl;
    std::cout << "    config.angle_penalty = " << w.angle_penalty << ";" << std::endl;
    std::cout << "    config.corner_cut_dist = " << w.corner_cut_dist << ";" << std::endl;
    std::cout << "    config.block_weight = " << w.block_weight << ";" << std::endl;
    std::cout << "    config.shield_penalty = " << w.shield_penalty << ";" << std::endl;
    std::cout << "    config.shield_ram_dist = " << w.shield_ram_dist << ";" << std::endl;
    std::cout << "    config.opp_penalty = " << w.opp_penalty << ";" << std::endl;
    std::cout << "    config.opp_model_ms = " << w.opp_model_ms << ";" << std::endl;
}

// Play a best-of-6 match: 3 maps × 2 sides
std::pair<int, int> PlayMatch(BotConfig confA, BotConfig confB) {
    int winsA = 0, winsB = 0;
    int num_maps = std::min(3, Arena::GetMapCount());
    
    for (int m = 0; m < num_maps; m++) {
        int map_idx = FastRandInt(0, Arena::GetMapCount() - 1);
        
        // Side 1: A=team0, B=team1
        {
            auto botA = std::make_shared<GABot>(confA);
            auto botB = std::make_shared<GABot>(confB);
            Arena arena(botA, botB);
            ArenaResult res = arena.PlayGame(false, map_idx);
            if (res.winner_team == 0) winsA++;
            else if (res.winner_team == 1) winsB++;
        }
        
        // Side 2: B=team0, A=team1
        {
            auto botB = std::make_shared<GABot>(confB);
            auto botA = std::make_shared<GABot>(confA);
            Arena arena(botB, botA);
            ArenaResult res = arena.PlayGame(false, map_idx);
            if (res.winner_team == 0) winsB++;
            else if (res.winner_team == 1) winsA++;
        }
    }
    
    return {winsA, winsB};
}

int main(int argc, char** argv) {
    InitLUT();
    GABot::verbose = false;
    
    int num_bots = 256;
    int num_rounds = 8;
    
    if (argc >= 2) num_bots = std::atoi(argv[1]);
    if (argc >= 3) num_rounds = std::atoi(argv[2]);
    // Ensure even number
    if (num_bots % 2 != 0) num_bots++;
    
    std::vector<Player> players(num_bots);
    for (int i = 0; i < num_bots; ++i) {
        players[i].config = RandomConfig(i);
    }
    
    std::cout << "Swiss Tournament: " << num_bots << " bots, " << num_rounds << " rounds, "
              << Arena::GetMapCount() << " maps" << std::endl;
    
    for (int round = 1; round <= num_rounds; ++round) {
        std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
            if (a.wins != b.wins) return a.wins > b.wins;
            return a.elo > b.elo;
        });
        
        std::vector<std::future<std::pair<int, int>>> match_results;
        for (int i = 0; i < num_bots; i += 2) {
            BotConfig cA = players[i].config;
            BotConfig cB = players[i + 1].config;
            match_results.push_back(std::async(std::launch::async, [cA, cB]() {
                return PlayMatch(cA, cB);
            }));
        }
        
        int match_idx = 0;
        for (int i = 0; i < num_bots; i += 2) {
            auto [winsA, winsB] = match_results[match_idx].get();
            match_idx++;
            players[i].wins += winsA;
            players[i + 1].wins += winsB;
            players[i].losses += winsB;
            players[i + 1].losses += winsA;
            int draws = 6 - winsA - winsB;
            players[i].draws += draws;
            players[i + 1].draws += draws;
            double expectedA = 1.0 / (1.0 + std::pow(10.0, (players[i+1].elo - players[i].elo) / 400.0));
            double scoreA = (winsA + draws * 0.5) / 6.0;
            int elo_change = 32 * (scoreA - expectedA);
            players[i].elo += elo_change;
            players[i+1].elo -= elo_change;
        }
        
        std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
            if (a.wins != b.wins) return a.wins > b.wins;
            return a.elo > b.elo;
        });
        
        // Compact top-2 per round
        std::cout << "R" << std::setw(2) << round << " | ";
        for (int i = 0; i < std::min(2, num_bots); ++i) {
            const auto& p = players[i];
            std::cout << "#" << (i+1) << " " << p.config.name 
                      << "(" << p.wins << "W/" << p.losses << "L E:" << p.elo 
                      << " H=" << p.config.horizon << " P=" << p.config.population
                      << " d=" << p.config.dist_weight << " a=" << p.config.align_weight
                      << " b=" << p.config.block_weight << ")";
            if (i == 0) std::cout << "  ";
        }
        std::cout << std::endl;
    }
    
    // Final Swiss standings
    std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
        if (a.wins != b.wins) return a.wins > b.wins;
        return a.elo > b.elo;
    });
    
    std::cout << "\n========== SWISS TOP 10 ==========" << std::endl;
    for (int i = 0; i < std::min(10, num_bots); ++i) {
        const auto& p = players[i];
        std::cout << std::setw(2) << (i+1) << ". " << std::setw(10) << p.config.name 
                  << " W:" << std::setw(3) << p.wins 
                  << " L:" << std::setw(3) << p.losses 
                  << " Elo:" << std::setw(5) << p.elo << std::endl;
        PrintConfig(p.config);
    }
    
    // ============================================================
    //  PLAYOFF: Top 8 round-robin, ALL maps x 2 sides per pair
    //  This eliminates map luck and reliably surfaces the best bot.
    // ============================================================
    int playoff_size = std::min(8, num_bots);
    int total_maps = Arena::GetMapCount();
    
    std::cout << "\n========== PLAYOFF: Top " << playoff_size 
              << " Round-Robin (" << total_maps << " maps x 2 sides = " 
              << total_maps * 2 << " games/pair) ==========" << std::endl;
    
    struct PlayoffEntry {
        int original_idx;
        BotConfig config;
        int total_wins = 0;
        int total_losses = 0;
        int total_draws = 0;
        int elo = 1200;
    };
    
    std::vector<PlayoffEntry> playoff(playoff_size);
    for (int i = 0; i < playoff_size; ++i) {
        playoff[i].original_idx = i;
        playoff[i].config = players[i].config;
    }
    
    // Play every pair across ALL maps, both sides
    for (int i = 0; i < playoff_size; ++i) {
        // Collect all match futures for player i against remaining opponents
        std::vector<std::pair<int, std::future<std::pair<int, int>>>> match_futures;
        for (int j = i + 1; j < playoff_size; ++j) {
            BotConfig cA = playoff[i].config;
            BotConfig cB = playoff[j].config;
            match_futures.push_back({j, std::async(std::launch::async, [cA, cB, total_maps]() {
                int wA = 0, wB = 0;
                for (int m = 0; m < total_maps; ++m) {
                    {
                        auto botA = std::make_shared<GABot>(cA);
                        auto botB = std::make_shared<GABot>(cB);
                        Arena arena(botA, botB);
                        ArenaResult res = arena.PlayGame(false, m);
                        if (res.winner_team == 0) wA++;
                        else if (res.winner_team == 1) wB++;
                    }
                    {
                        auto botB = std::make_shared<GABot>(cB);
                        auto botA = std::make_shared<GABot>(cA);
                        Arena arena(botB, botA);
                        ArenaResult res = arena.PlayGame(false, m);
                        if (res.winner_team == 0) wB++;
                        else if (res.winner_team == 1) wA++;
                    }
                }
                return std::make_pair(wA, wB);
            })});
        }
        
        // Collect results
        for (auto& [j, fut] : match_futures) {
            auto [wA, wB] = fut.get();
            int total_games = total_maps * 2;
            int draws = total_games - wA - wB;
            
            playoff[i].total_wins += wA;
            playoff[i].total_losses += wB;
            playoff[i].total_draws += draws;
            playoff[j].total_wins += wB;
            playoff[j].total_losses += wA;
            playoff[j].total_draws += draws;
            
            double expectedA = 1.0 / (1.0 + std::pow(10.0, (playoff[j].elo - playoff[i].elo) / 400.0));
            double scoreA = (wA + draws * 0.5) / (double)total_games;
            int elo_change = 32 * (scoreA - expectedA);
            playoff[i].elo += elo_change;
            playoff[j].elo -= elo_change;
            
            std::cout << "  " << playoff[i].config.name << " vs " << playoff[j].config.name 
                      << ": " << wA << "-" << wB << " (" << draws << " draws)" << std::endl;
        }
    }
    
    std::sort(playoff.begin(), playoff.end(), [](const PlayoffEntry& a, const PlayoffEntry& b) {
        if (a.total_wins != b.total_wins) return a.total_wins > b.total_wins;
        return a.elo > b.elo;
    });
    
    std::cout << "\n========== PLAYOFF FINAL STANDINGS ==========" << std::endl;
    for (int i = 0; i < playoff_size; ++i) {
        const auto& p = playoff[i];
        double win_rate = 100.0 * p.total_wins / (double)(p.total_wins + p.total_losses + p.total_draws);
        std::cout << std::setw(2) << (i+1) << ". " << std::setw(10) << p.config.name 
                  << " W:" << std::setw(4) << p.total_wins 
                  << " L:" << std::setw(4) << p.total_losses 
                  << " D:" << std::setw(3) << p.total_draws
                  << " WR:" << std::fixed << std::setprecision(1) << std::setw(5) << win_rate << "%"
                  << " Elo:" << std::setw(5) << p.elo << std::endl;
        PrintConfig(p.config);
    }
    
    std::cout << std::endl;
    PrintCopyableConfig(playoff[0].config, "CHAMPION");
    std::cout << std::endl;
    if (playoff_size >= 2)
        PrintCopyableConfig(playoff[1].config, "RUNNER-UP");
    
    return 0;
}

