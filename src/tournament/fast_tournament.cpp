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

    // GA Core
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
    std::cout << "config.horizon = " << w.horizon << ";" << std::endl;
    std::cout << "config.population = " << w.population << ";" << std::endl;
    std::cout << "config.dist_weight = " << w.dist_weight << ";" << std::endl;
    std::cout << "config.align_weight = " << w.align_weight << ";" << std::endl;
    std::cout << "config.speed_bonus = " << w.speed_bonus << ";" << std::endl;
    std::cout << "config.lateral_penalty = " << w.lateral_penalty << ";" << std::endl;
    std::cout << "config.angle_penalty = " << w.angle_penalty << ";" << std::endl;
    std::cout << "config.corner_cut_dist = " << w.corner_cut_dist << ";" << std::endl;
    std::cout << "config.block_weight = " << w.block_weight << ";" << std::endl;
    std::cout << "config.shield_penalty = " << w.shield_penalty << ";" << std::endl;
    std::cout << "config.shield_ram_dist = " << w.shield_ram_dist << ";" << std::endl;
    std::cout << "config.opp_penalty = " << w.opp_penalty << ";" << std::endl;
    std::cout << "config.opp_model_ms = " << w.opp_model_ms << ";" << std::endl;
}

// Play a best-of-6 match: 3 random maps x 2 sides
std::pair<int, int> PlayMatch(BotConfig confA, BotConfig confB) {
    int winsA = 0, winsB = 0;
    int num_maps = std::min(3, Arena::GetMapCount());

    for (int m = 0; m < num_maps; m++) {
        int map_idx = FastRandInt(0, Arena::GetMapCount() - 1);

        {
            auto botA = std::make_shared<GABot>(confA);
            auto botB = std::make_shared<GABot>(confB);
            Arena arena(botA, botB);
            ArenaResult res = arena.PlayGame(false, map_idx);
            if (res.winner_team == 0) winsA++;
            else if (res.winner_team == 1) winsB++;
        }
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

    int num_bots = 32;
    int num_rounds = 5;

    if (argc >= 2) num_bots = std::atoi(argv[1]);
    if (argc >= 3) num_rounds = std::atoi(argv[2]);
    if (num_bots % 2 != 0) num_bots++;

    std::vector<Player> players(num_bots);
    for (int i = 0; i < num_bots; ++i) {
        players[i].config = RandomConfig(i);
    }

    std::cout << "Fast Swiss Tournament: " << num_bots << " bots, " << num_rounds << " rounds, "
              << Arena::GetMapCount() << " maps, 6 games/match" << std::endl;

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

    // Final standings
    std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
        if (a.wins != b.wins) return a.wins > b.wins;
        return a.elo > b.elo;
    });

    std::cout << "\n========== FINAL TOP 5 ==========" << std::endl;
    for (int i = 0; i < std::min(5, num_bots); ++i) {
        const auto& p = players[i];
        std::cout << std::setw(2) << (i+1) << ". " << std::setw(10) << p.config.name
                  << " W:" << std::setw(3) << p.wins
                  << " L:" << std::setw(3) << p.losses
                  << " Elo:" << std::setw(5) << p.elo << std::endl;
        PrintConfig(p.config);
    }

    std::cout << std::endl;
    PrintCopyableConfig(players[0].config, "1st PLACE");
    std::cout << std::endl;
    if (num_bots >= 2)
        PrintCopyableConfig(players[1].config, "2nd PLACE");

    return 0;
}
