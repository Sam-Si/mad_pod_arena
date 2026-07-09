#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <memory>
#include <thread>
#include <cstdlib>

#include "src/engine/arena.h"
#include "src/tournament/cg_bot_wrapper.h"

static void PrintUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "Self-play benchmark: CGBot vs CGBot (both sides, all maps).\n"
              << "\n"
              << "Options:\n"
              << "  --start-map N      First map index (default: 0)\n"
              << "  --end-map N        One-past-last map index (default: all "
              << Arena::GetMapCount() << " maps)\n"
              << "  --repeats N        Games per map per side (default: 1)\n"
              << "  --time-budget MS   Per-turn time budget in ms (default: 7.5)\n"
              << "  --verbose          Print per-turn GA debug info to stderr\n"
              << "  --help             Show this message\n"
              << "\n"
              << "Examples:\n"
              << "  benchmark_tournament                          # all maps, 1 repeat, 7.5ms\n"
              << "  benchmark_tournament --repeats 5              # all maps, 5 repeats\n"
              << "  benchmark_tournament --time-budget 75         # full CG time budget\n"
              << "  benchmark_tournament --start-map 0 --end-map 6 --repeats 3\n"
              << "  benchmark_tournament --repeats 10 --verbose\n";
}

int main(int argc, char** argv) {
    InitLUT();

    int total_maps = Arena::GetMapCount();
    int start_map = 0;
    int end_map = total_maps;
    int repeats = 1;
    double time_budget_ms = 7.5;
    int batch_size = -1;
    bool verbose = false;

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
        } else if (arg == "--time-budget" && i + 1 < argc) {
            time_budget_ms = std::atof(argv[++i]);
        } else if (arg == "--batch-size" && i + 1 < argc) {
            batch_size = std::atoi(argv[++i]);
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

    SetGABotVerbose(verbose);

    unsigned int num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) num_cores = 4;

    if (batch_size == -1) {
        const char* env_threads = std::getenv("BOT_THREADS");
        if (env_threads && std::atoi(env_threads) > 1) {
            batch_size = 1;
        } else {
            batch_size = static_cast<int>(2 * num_cores);
        }
    }

    std::cout << "=== CGBot self-play: Maps " << start_map << "-" << (end_map - 1)
              << ", " << repeats << " repeats/side, time_budget=" << time_budget_ms << "ms"
              << ", batch_size=" << batch_size
              << " (cores=" << num_cores << ") ===" << std::endl;

    struct GameTask {
        int map_idx;
        bool bot_a_is_team0;
    };

    std::vector<GameTask> tasks;
    tasks.reserve((end_map - start_map) * repeats * 2);
    for (int m = start_map; m < end_map; ++m) {
        for (int r = 0; r < repeats; ++r) {
            tasks.push_back({m, true});
            tasks.push_back({m, false});
        }
    }

    struct GameResult {
        int map_idx;
        int team0_won;
        int team1_won;
    };

    std::vector<GameResult> results(tasks.size());
    auto bench_start = std::chrono::high_resolution_clock::now();

    for (size_t batch_start = 0; batch_start < tasks.size(); batch_start += batch_size) {
        size_t batch_end = std::min(batch_start + batch_size, tasks.size());
        std::vector<std::future<GameResult>> futures;
        futures.reserve(batch_end - batch_start);

        for (size_t i = batch_start; i < batch_end; ++i) {
            const auto& task = tasks[i];
            futures.push_back(std::async(std::launch::async,
                [task, time_budget_ms]() -> GameResult {
                    auto a = std::make_shared<CGBotWrapper>(time_budget_ms);
                    auto b = std::make_shared<CGBotWrapper>(time_budget_ms);

                    std::shared_ptr<IBot> team0 = task.bot_a_is_team0
                        ? std::static_pointer_cast<IBot>(a)
                        : std::static_pointer_cast<IBot>(b);
                    std::shared_ptr<IBot> team1 = task.bot_a_is_team0
                        ? std::static_pointer_cast<IBot>(b)
                        : std::static_pointer_cast<IBot>(a);

                    Arena arena(team0, team1);
                    ArenaResult res = arena.PlayGame(false, task.map_idx);

                    GameResult gr;
                    gr.map_idx = task.map_idx;
                    gr.team0_won = (res.winner_team == 0) ? 1 : 0;
                    gr.team1_won = (res.winner_team == 1) ? 1 : 0;
                    return gr;
                }));
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            results[batch_start + i] = futures[i].get();
        }
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double elapsed_s = std::chrono::duration<double>(bench_end - bench_start).count();

    struct MapStats { int t0 = 0, t1 = 0, draws = 0; };
    std::vector<MapStats> map_stats(total_maps);
    int total_t0 = 0, total_t1 = 0, total_draws = 0;

    for (const auto& r : results) {
        if (r.team0_won)       { map_stats[r.map_idx].t0++; total_t0++; }
        else if (r.team1_won)  { map_stats[r.map_idx].t1++; total_t1++; }
        else                   { map_stats[r.map_idx].draws++; total_draws++; }
    }

    for (int m = start_map; m < end_map; ++m) {
        const auto& s = map_stats[m];
        std::cout << "Map " << std::setw(2) << m << ": team0 " << s.t0
                  << " - team1 " << s.t1 << " (draws: " << s.draws << ")" << std::endl;
    }

    int total_games = total_t0 + total_t1 + total_draws;
    std::cout << "\n=== TOTALS ===" << std::endl;
    std::cout << "Team0 wins: " << total_t0 << "  Team1 wins: " << total_t1
              << "  Draws: " << total_draws
              << "  Games: " << total_games << std::endl;
    std::cout << "Elapsed: " << std::fixed << std::setprecision(1)
              << elapsed_s << "s" << std::endl;

    return 0;
}
