#include "src/engine/arena.h"
#include "src/bot/ga_bot.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <future>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <mutex>
#include <cmath>

struct Player {
    BotConfig config;
    int elo = 1200;
    int wins = 0;
    int losses = 0;
    int draws = 0;
    int matches_played = 0;
};

// Logger to handle both file and console output
class Logger {
    std::ofstream file;
    std::mutex mtx;
public:
    Logger() {}
    void Open(const std::string& filename) {
        file.open(filename);
    }
    template<typename T>
    Logger& operator<<(const T& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << msg;
        if (file.is_open()) file << msg;
        return *this;
    }
    Logger& operator<<(std::ostream& (*func)(std::ostream&)) {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << func;
        if (file.is_open()) file << func;
        return *this;
    }
    void flush() {
        std::cout.flush();
        file.flush();
    }
};

Logger logger;

BotConfig RandomConfig(int id) {
    BotConfig c;
    c.name = "Bot_" + std::to_string(id);
    
    // GA Core
    c.horizon = 4 + FastRandInt(0, 4);                     // 4..8
    c.population = 20 + FastRandInt(0, 6) * 10;            // 20..80
    
    // Runner Evaluation
    c.dist_weight = 0.5 + FastRandInt(0, 30) / 10.0;       // 0.5..3.5
    c.align_weight = 0.5 + FastRandInt(0, 40) / 10.0;      // 0.5..4.5
    c.speed_bonus = FastRandInt(0, 10) / 10.0;              // 0.0..1.0
    c.lateral_penalty = FastRandInt(0, 20) / 10.0;          // 0.0..2.0
    c.angle_penalty = FastRandInt(5, 60);                   // 5..60
    c.corner_cut_dist = 100 + FastRandInt(0, 8) * 100;     // 100..900
    
    // Blocker
    c.block_weight = FastRandInt(0, 100) / 10.0;            // 0.0..10.0
    c.shield_penalty = FastRandInt(0, 100);                 // 0..100
    c.shield_ram_dist = 600 + FastRandInt(0, 6) * 100;     // 600..1200
    
    // Coordination
    c.opp_penalty = FastRandInt(0, 30) / 10.0;             // 0.0..3.0
    
    // Time allocation (0 ms means heuristic only opponent model)
    c.opp_model_ms = FastRandInt(0, 10) * 5.0;            // 0..50ms
    
    return c;
}

void PrintConfig(const BotConfig& c) {
    logger << "  H=" << c.horizon << " P=" << c.population
           << " | dist=" << c.dist_weight << " align=" << c.align_weight
           << " speed=" << c.speed_bonus << " lat=" << c.lateral_penalty
           << " angle=" << c.angle_penalty << " corner=" << c.corner_cut_dist
           << " | block=" << c.block_weight << " shield=" << c.shield_penalty
           << " ram=" << c.shield_ram_dist
           << " | opp=" << c.opp_penalty
           << " | opp_ms=" << c.opp_model_ms << std::endl;
}

void PrintCopyableConfig(const BotConfig& w, const std::string& label = "") {
    if (!label.empty()) logger << "// === " << label << " ===" << std::endl;
    logger << "config.horizon = " << w.horizon << ";" << std::endl;
    logger << "config.population = " << w.population << ";" << std::endl;
    logger << "config.dist_weight = " << w.dist_weight << ";" << std::endl;
    logger << "config.align_weight = " << w.align_weight << ";" << std::endl;
    logger << "config.speed_bonus = " << w.speed_bonus << ";" << std::endl;
    logger << "config.lateral_penalty = " << w.lateral_penalty << ";" << std::endl;
    logger << "config.angle_penalty = " << w.angle_penalty << ";" << std::endl;
    logger << "config.corner_cut_dist = " << w.corner_cut_dist << ";" << std::endl;
    logger << "config.block_weight = " << w.block_weight << ";" << std::endl;
    logger << "config.shield_penalty = " << w.shield_penalty << ";" << std::endl;
    logger << "config.shield_ram_dist = " << w.shield_ram_dist << ";" << std::endl;
    logger << "config.opp_penalty = " << w.opp_penalty << ";" << std::endl;
    logger << "config.opp_model_ms = " << w.opp_model_ms << ";" << std::endl;
}

std::pair<int, int> PlayMatch(BotConfig confA, BotConfig confB, int maps_per_match) {
    int winsA = 0, winsB = 0;
    int total_maps = Arena::GetMapCount();
    
    for (int m = 0; m < maps_per_match; m++) {
        int map_idx = FastRandInt(0, total_maps - 1);
        
        // Side 1
        {
            auto botA = std::make_shared<GABot>(confA);
            auto botB = std::make_shared<GABot>(confB);
            Arena arena(botA, botB);
            ArenaResult res = arena.PlayGame(false, map_idx);
            if (res.winner_team == 0) winsA++;
            else if (res.winner_team == 1) winsB++;
        }
        
        // Side 2
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

std::string CurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

int main(int argc, char** argv) {
    InitLUT();
    
    std::string timestamp = CurrentTimestamp();
    logger.Open("tournament_log_" + timestamp + ".txt");
    
    logger << "Tournament started at " << timestamp << std::endl;
    
    int initial_bots = 10000;
    if (argc >= 2) initial_bots = std::atoi(argv[1]);
    if (initial_bots % 2 != 0) initial_bots++;
    
    // Stage 1: The Great Cull
    logger << "--- STAGE 1: The Great Cull (" << initial_bots << " bots) ---" << std::endl;
    std::vector<Player> players(initial_bots);
    for (int i = 0; i < initial_bots; ++i) {
        players[i].config = RandomConfig(i);
    }
    
    int culling_rounds = 6; // 10000 -> 5000 -> 2500 -> 1250 -> 625 -> 312 -> 156
    for (int r = 1; r <= culling_rounds; ++r) {
        logger << "Round " << r << "/" << culling_rounds << " (" << players.size() << " bots)..." << std::flush;
        auto start = std::chrono::high_resolution_clock::now();
        
        // Swiss pairing
        std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
            if (a.wins != b.wins) return a.wins > b.wins;
            return a.elo > b.elo;
        });
        
        int num_matches = players.size() / 2;
        int batch_size = 100; // Process matches in batches to avoid thread limits
        for (int b = 0; b < num_matches; b += batch_size) {
            std::vector<std::future<std::pair<int, int>>> futures;
            int current_batch = std::min(batch_size, num_matches - b);
            
            for (int i = 0; i < current_batch; ++i) {
                int idx = b + i;
                BotConfig cA = players[idx*2].config;
                BotConfig cB = players[idx*2+1].config;
                futures.push_back(std::async(std::launch::async, [cA, cB]() {
                    return PlayMatch(cA, cB, 1); // 2 games (1 map, both sides)
                }));
            }
            
            for (int i = 0; i < current_batch; ++i) {
                auto [wA, wB] = futures[i].get();
                int idx = b + i;
                int pA = idx*2;
                int pB = idx*2+1;
                players[pA].wins += wA;
                players[pB].wins += wB;
                players[pA].losses += wB;
                players[pB].losses += wA;
                int draws = 2 - wA - wB;
                players[pA].draws += draws;
                players[pB].draws += draws;
                
                double expectedA = 1.0 / (1.0 + std::pow(10.0, (players[pB].elo - players[pA].elo) / 400.0));
                double scoreA = (wA + draws * 0.5) / 2.0;
                int elo_change = 32 * (scoreA - expectedA);
                players[pA].elo += elo_change;
                players[pB].elo -= elo_change;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        logger << " done in " << std::fixed << std::setprecision(1) << diff.count() << "s" << std::endl;
    }
    
    // Sort and keep top 128
    std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
        if (a.wins != b.wins) return a.wins > b.wins;
        return a.elo > b.elo;
    });
    
    int keepers = std::min((int)players.size(), 128);
    players.erase(players.begin() + keepers, players.end());
    
    logger << "Culling complete. Top bot: " << players[0].config.name << " Elo: " << players[0].elo << std::endl;
    
    // Stage 2: Thorough Evaluation
    logger << "\n--- STAGE 2: Thorough Evaluation (Top " << keepers << ") ---" << std::endl;
    int eval_rounds = 8;
    for (int r = 1; r <= eval_rounds; ++r) {
        logger << "Round " << r << "/" << eval_rounds << "... " << std::flush;
        auto start = std::chrono::high_resolution_clock::now();
        
        std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
            if (a.wins != b.wins) return a.wins > b.wins;
            return a.elo > b.elo;
        });
        
        int num_matches = players.size() / 2;
        int batch_size = 50; // Smaller batch for heavier games
        for (int b = 0; b < num_matches; b += batch_size) {
            std::vector<std::future<std::pair<int, int>>> futures;
            int current_batch = std::min(batch_size, num_matches - b);
            
            for (int i = 0; i < current_batch; ++i) {
                int idx = b + i;
                BotConfig cA = players[idx*2].config;
                BotConfig cB = players[idx*2+1].config;
                futures.push_back(std::async(std::launch::async, [cA, cB]() {
                    return PlayMatch(cA, cB, 3); // 6 games (3 maps, both sides)
                }));
            }
            
            for (int i = 0; i < current_batch; ++i) {
                auto [wA, wB] = futures[i].get();
                int idx = b + i;
                int pA = idx*2;
                int pB = idx*2+1;
                players[pA].wins += wA;
                players[pB].wins += wB;
                players[pA].losses += wB;
                players[pB].losses += wA;
                int draws = 6 - wA - wB;
                players[pA].draws += draws;
                players[pB].draws += draws;
                
                double expectedA = 1.0 / (1.0 + std::pow(10.0, (players[pB].elo - players[pA].elo) / 400.0));
                double scoreA = (wA + draws * 0.5) / 6.0;
                int elo_change = 32 * (scoreA - expectedA);
                players[pA].elo += elo_change;
                players[pB].elo -= elo_change;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        logger << " done in " << std::fixed << std::setprecision(1) << diff.count() << "s. Top: " 
               << players[0].config.name << " (" << players[0].elo << ")" << std::endl;
    }
    
    // Stage 3: The Grand Finale
    int playoff_size = std::min((int)players.size(), 8);
    logger << "\n--- STAGE 3: Grand Finale (Top " << playoff_size << " Round-Robin) ---" << std::endl;
    int total_maps = Arena::GetMapCount();
    logger << "Playing every pair on all " << total_maps << " maps, both sides (" << total_maps*2 << " games per match)" << std::endl;
    
    struct PlayoffEntry {
        BotConfig config;
        int total_wins = 0;
        int total_losses = 0;
        int total_draws = 0;
        int elo = 1500;
    };
    
    std::vector<PlayoffEntry> playoff(playoff_size);
    for (int i = 0; i < playoff_size; ++i) {
        playoff[i].config = players[i].config;
    }
    
    for (int i = 0; i < playoff_size; ++i) {
        for (int j = i + 1; j < playoff_size; ++j) {
            logger << "Match: " << playoff[i].config.name << " vs " << playoff[j].config.name << "... " << std::flush;
            auto start = std::chrono::high_resolution_clock::now();
            
            BotConfig cA = playoff[i].config;
            BotConfig cB = playoff[j].config;
            
            int wA = 0, wB = 0;
            std::vector<std::future<std::pair<int, int>>> map_futures;
            for (int m = 0; m < total_maps; ++m) {
                map_futures.push_back(std::async(std::launch::async, [cA, cB, m]() {
                    int swA = 0, swB = 0;
                    // Side 1
                    {
                        auto botA = std::make_shared<GABot>(cA);
                        auto botB = std::make_shared<GABot>(cB);
                        Arena arena(botA, botB);
                        ArenaResult res = arena.PlayGame(false, m);
                        if (res.winner_team == 0) swA++;
                        else if (res.winner_team == 1) swB++;
                    }
                    // Side 2
                    {
                        auto botB = std::make_shared<GABot>(cB);
                        auto botA = std::make_shared<GABot>(cA);
                        Arena arena(botB, botA);
                        ArenaResult res = arena.PlayGame(false, m);
                        if (res.winner_team == 0) swB++;
                        else if (res.winner_team == 1) swA++;
                    }
                    return std::make_pair(swA, swB);
                }));
            }
            
            for (auto& fut : map_futures) {
                auto [resA, resB] = fut.get();
                wA += resA;
                wB += resB;
            }
            
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
            
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> diff = end - start;
            logger << wA << "-" << wB << " (" << draws << "D) in " << std::fixed << std::setprecision(1) << diff.count() << "s" << std::endl;
        }
    }
    
    std::sort(playoff.begin(), playoff.end(), [](const PlayoffEntry& a, const PlayoffEntry& b) {
        if (a.total_wins != b.total_wins) return a.total_wins > b.total_wins;
        return a.elo > b.elo;
    });
    
    logger << "\n========== FINAL STANDINGS ==========" << std::endl;
    for (int i = 0; i < playoff_size; ++i) {
        const auto& p = playoff[i];
        double win_rate = 100.0 * p.total_wins / (double)(p.total_wins + p.total_losses + p.total_draws);
        logger << std::setw(2) << (i+1) << ". " << std::setw(10) << p.config.name 
               << " W:" << std::setw(4) << p.total_wins 
               << " L:" << std::setw(4) << p.total_losses 
               << " D:" << std::setw(4) << p.total_draws
               << " WR:" << std::fixed << std::setprecision(1) << std::setw(5) << win_rate << "%"
               << " Elo:" << std::setw(5) << p.elo << std::endl;
        PrintConfig(p.config);
    }
    
    logger << "\n--- TOP TWO CONFIGS ---\n" << std::endl;
    PrintCopyableConfig(playoff[0].config, "CHAMPION (1st Place)");
    logger << std::endl;
    if (playoff_size >= 2) {
        PrintCopyableConfig(playoff[1].config, "RUNNER-UP (2nd Place)");
        logger << std::endl;
    }
    
    logger << "Tournament finished at " << CurrentTimestamp() << std::endl;
    logger << "Log saved to tournament_log_" << timestamp << ".txt" << std::endl;
    
    return 0;
}
