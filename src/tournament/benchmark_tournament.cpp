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

void PrintCopyableConfig(const BotConfig& w, const std::string& label = "") {
    std::lock_guard<std::mutex> lock(log_mutex);
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

// Play a match against LegacyBot
std::pair<int, int> PlayMatchAgainstLegacy(BotConfig confA) {
    int winsA = 0;
    int winsLegacy = 0;
    
    // Use 1 map for quick screening
    std::vector<int> map_indices = {0};
    for (int map_idx : map_indices) {
        // Round 1: A is Team 0
        {
            auto botA = std::make_shared<GABot>(confA);
            auto botLegacy = std::make_shared<LegacyBotWrapper>();
            Arena arena(botA, botLegacy);
            ArenaResult res = arena.PlayGame(false, map_idx);
            if (res.winner_team == 0) winsA++;
            else if (res.winner_team == 1) winsLegacy++;
        }
        // Round 2: A is Team 1
        {
            auto botA = std::make_shared<GABot>(confA);
            auto botLegacy = std::make_shared<LegacyBotWrapper>();
            Arena arena(botLegacy, botA);
            ArenaResult res = arena.PlayGame(false, map_idx);
            if (res.winner_team == 1) winsA++;
            else if (res.winner_team == 0) winsLegacy++;
        }
    }
    return {winsA, winsLegacy};
}

int main(int argc, char** argv) {
    int num_bots = 100;
    if (argc > 1) num_bots = std::atoi(argv[1]);

    std::cout << "--- BENCHMARK TOURNAMENT vs LEGACY BOT ---" << std::endl;
    std::cout << "Testing " << num_bots << " random configurations..." << std::endl;

    GABot::verbose = false; // Keep it quiet

    std::vector<std::future<std::pair<int, int>>> futures;
    std::vector<BotConfig> configs;

    for (int i = 0; i < num_bots; i++) {
        BotConfig c;
        c.name = "Bot_" + std::to_string(i);
        c.Randomize();
        configs.push_back(c);
        
        futures.push_back(std::async(std::launch::async, PlayMatchAgainstLegacy, c));
        
        // Batching to prevent thread explosion
        if (futures.size() >= 100) {
            for (size_t j = 0; j < futures.size(); j++) {
                auto res = futures[j].get();
                if (res.first > res.second) {
                    PrintCopyableConfig(configs[configs.size() - futures.size() + j], 
                        configs[configs.size() - futures.size() + j].name + " BEAT LEGACY (" + std::to_string(res.first) + "-" + std::to_string(res.second) + ")");
                }
            }
            futures.clear();
            std::cout << "Progress: " << i + 1 << "/" << num_bots << std::endl;
        }
    }

    // Final batch
    for (size_t j = 0; j < futures.size(); j++) {
        auto res = futures[j].get();
        if (res.first > res.second) {
            PrintCopyableConfig(configs[configs.size() - futures.size() + j], 
                configs[configs.size() - futures.size() + j].name + " BEAT LEGACY (" + std::to_string(res.first) + "-" + std::to_string(res.second) + ")");
        }
    }

    std::cout << "Benchmark complete." << std::endl;
    return 0;
}
