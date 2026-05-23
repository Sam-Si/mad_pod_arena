#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <mutex>
#include <memory>

#include "src/engine/arena.h"
#include "src/bot/ga_bot.h"
#include "src/tournament/legacy_wrapper.h"

std::mutex log_mutex;

int main(int argc, char** argv) {
    InitLUT();
    GABot::verbose = (argc > 4 && std::string(argv[4]) == "-v");

    int total_maps = Arena::GetMapCount();
    int start_map = 0;
    int end_map = total_maps;
    int repeats = 1;
    
    if (argc > 1) start_map = std::atoi(argv[1]);
    if (argc > 2) end_map = std::atoi(argv[2]);
    if (argc > 3) repeats = std::atoi(argv[3]);
    if (end_map > total_maps) end_map = total_maps;
    if (start_map < 0) start_map = 0;

    BotConfig config;
    config.name = "GABot";

    std::cout << "=== GABot vs LegacyBot: Maps " << start_map << "-" << (end_map-1) 
              << ", " << repeats << " repeats/side ===" << std::endl;

    int total_ga_wins = 0, total_legacy_wins = 0, total_draws = 0;
    
    for (int m = start_map; m < end_map; ++m) {
        int ga_wins = 0, legacy_wins = 0, draws = 0;
        
        for (int r = 0; r < repeats; ++r) {
            {
                auto ga = std::make_shared<GABot>(config);
                auto legacy = std::make_shared<LegacyBotWrapper>();
                Arena arena(ga, legacy);
                ArenaResult res = arena.PlayGame(false, m);
                if (res.winner_team == 0) ga_wins++;
                else if (res.winner_team == 1) legacy_wins++;
                else draws++;
            }
            {
                auto legacy = std::make_shared<LegacyBotWrapper>();
                auto ga = std::make_shared<GABot>(config);
                Arena arena(legacy, ga);
                ArenaResult res = arena.PlayGame(false, m);
                if (res.winner_team == 1) ga_wins++;
                else if (res.winner_team == 0) legacy_wins++;
                else draws++;
            }
        }
        
        total_ga_wins += ga_wins;
        total_legacy_wins += legacy_wins;
        total_draws += draws;
        
        const char* status = (ga_wins > legacy_wins) ? "WIN" : (ga_wins < legacy_wins) ? "LOSS" : "DRAW";
        std::cout << "Map " << std::setw(2) << m << ": GA " << ga_wins 
                  << " - Legacy " << legacy_wins << " (draws: " << draws 
                  << ") [" << status << "]" << std::endl;
    }

    int total_games = total_ga_wins + total_legacy_wins + total_draws;
    double win_rate = (total_games > 0) ? 100.0 * total_ga_wins / total_games : 0.0;
    std::cout << "\n=== TOTALS ===" << std::endl;
    std::cout << "GA Wins: " << total_ga_wins << "  Legacy Wins: " << total_legacy_wins 
              << "  Draws: " << total_draws << std::endl;
    std::cout << "Win Rate: " << std::fixed << std::setprecision(1) << win_rate << "%" << std::endl;

    return 0;
}
