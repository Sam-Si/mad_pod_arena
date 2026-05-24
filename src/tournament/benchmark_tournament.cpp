#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <memory>
#include <thread>

#include "src/engine/arena.h"
#include "src/tournament/cg_bot_wrapper.h"
#include "src/tournament/legacy_wrapper.h"

static void PrintUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
              << "  --start-map N   First map index (default: 0)\n"
              << "  --end-map N     One-past-last map index (default: all "
              << Arena::GetMapCount() << " maps)\n"
              << "  --repeats N     Games per map per side (default: 1)\n"
              << "  --verbose       Print per-turn GA debug info to stderr\n"
              << "  --help          Show this message\n"
              << "\n"
              << "Examples:\n"
              << "  benchmark_tournament                          # all maps, 1 repeat\n"
              << "  benchmark_tournament --repeats 5              # all maps, 5 repeats\n"
              << "  benchmark_tournament --start-map 0 --end-map 6 --repeats 3\n"
              << "  benchmark_tournament --repeats 10 --verbose\n";
}

int main(int argc, char** argv) {
    InitLUT();

    int total_maps = Arena::GetMapCount();
    int start_map = 0;
    int end_map = total_maps;
    int repeats = 1;
    bool verbose = false;

    // Named-flag argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--start-map" && i + 1 < argc) {
            start_map = std::atoi(argv[++i]);
        } else if (arg == "--end-map" && i + 1 < argc) {
            end_map = std::atoi(argv[++i]);
        } else if (arg == "--repeats" && i + 1 < argc) {
            repeats = std::atoi(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (end_map > total_maps) end_map = total_maps;
    if (start_map < 0) start_map = 0;

    CG_GABot::verbose = verbose;

    // Dynamic batch size: 2x available hardware threads
    unsigned int num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) num_cores = 4;  // safe fallback
    int batch_size = static_cast<int>(2 * num_cores);

    std::cout << "=== CGBot vs LegacyBot: Maps " << start_map << "-" << (end_map - 1)
              << ", " << repeats << " repeats/side, batch_size=" << batch_size
              << " (cores=" << num_cores << ") ===" << std::endl;

    // Build the full list of game tasks up front
    struct GameTask {
        int map_idx;
        bool cg_is_team0;  // true  → CGBot plays as team 0
    };

    std::vector<GameTask> tasks;
    tasks.reserve((end_map - start_map) * repeats * 2);
    for (int m = start_map; m < end_map; ++m) {
        for (int r = 0; r < repeats; ++r) {
            tasks.push_back({m, true});
            tasks.push_back({m, false});
        }
    }

    // Result per game: which map, and who won
    struct GameResult {
        int map_idx;
        int cg_won;      // 1 if CGBot won
        int legacy_won;  // 1 if LegacyBot won
    };

    std::vector<GameResult> results(tasks.size());

    auto bench_start = std::chrono::high_resolution_clock::now();

    // Process games in dynamic batches
    for (size_t batch_start = 0; batch_start < tasks.size();
         batch_start += batch_size) {
        size_t batch_end = std::min(batch_start + batch_size, tasks.size());
        std::vector<std::future<GameResult>> futures;
        futures.reserve(batch_end - batch_start);

        for (size_t i = batch_start; i < batch_end; ++i) {
            const auto& task = tasks[i];
            futures.push_back(std::async(std::launch::async,
                [task]() -> GameResult {
                    auto cg     = std::make_shared<CGBotWrapper>();
                    auto legacy = std::make_shared<LegacyBotWrapper>();

                    std::shared_ptr<IBot> team0 = task.cg_is_team0
                        ? std::static_pointer_cast<IBot>(cg)
                        : std::static_pointer_cast<IBot>(legacy);
                    std::shared_ptr<IBot> team1 = task.cg_is_team0
                        ? std::static_pointer_cast<IBot>(legacy)
                        : std::static_pointer_cast<IBot>(cg);

                    Arena arena(team0, team1);
                    ArenaResult res = arena.PlayGame(false, task.map_idx);

                    int cg_team = task.cg_is_team0 ? 0 : 1;
                    GameResult gr;
                    gr.map_idx    = task.map_idx;
                    gr.cg_won     = (res.winner_team == cg_team) ? 1 : 0;
                    gr.legacy_won = (res.winner_team != -1 &&
                                     res.winner_team != cg_team) ? 1 : 0;
                    return gr;
                }));
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            results[batch_start + i] = futures[i].get();
        }
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double elapsed_s = std::chrono::duration<double>(bench_end - bench_start).count();

    // Aggregate per-map statistics
    struct MapStats { int cg = 0, legacy = 0, draws = 0; };
    std::vector<MapStats> map_stats(total_maps);
    int total_cg = 0, total_legacy = 0, total_draws = 0;

    for (const auto& r : results) {
        if (r.cg_won)          { map_stats[r.map_idx].cg++;      total_cg++; }
        else if (r.legacy_won) { map_stats[r.map_idx].legacy++;  total_legacy++; }
        else                   { map_stats[r.map_idx].draws++;   total_draws++; }
    }

    // Per-map report
    for (int m = start_map; m < end_map; ++m) {
        const auto& s = map_stats[m];
        const char* status = (s.cg > s.legacy)  ? "WIN"
                           : (s.cg < s.legacy)  ? "LOSS"
                           :                      "DRAW";
        std::cout << "Map " << std::setw(2) << m << ": CG " << s.cg
                  << " - Legacy " << s.legacy << " (draws: " << s.draws
                  << ") [" << status << "]" << std::endl;
    }

    // Totals
    int total_games = total_cg + total_legacy + total_draws;
    double win_rate = (total_games > 0)
        ? 100.0 * total_cg / total_games : 0.0;

    std::cout << "\n=== TOTALS ===" << std::endl;
    std::cout << "CG Wins: " << total_cg << "  Legacy Wins: " << total_legacy
              << "  Draws: " << total_draws << std::endl;
    std::cout << "Win Rate: " << std::fixed << std::setprecision(1)
              << win_rate << "%" << std::endl;
    std::cout << "Elapsed: " << std::fixed << std::setprecision(1)
              << elapsed_s << "s" << std::endl;

    return 0;
}
