// ============================================================================
// Standardized physics benchmark: Fidelity (physics.h) vs fast_physics.h
// ============================================================================
//
// Single source of truth for fast_physics benchmarking & EXACT parity.
//
// Build:
//   bazel build -c opt //src/physics:bench_fast_physics
//   g++ -std=c++17 -O3 -DNDEBUG -fno-math-errno -fomit-frame-pointer \
//       -I src/physics src/physics/bench_fast_physics.cpp -o /tmp/bench_fp
//
// Usage:
//   bench_fast_physics                                # default: 200×500, 11 iters
//   bench_fast_physics --scenarios 500 --turns 1000 --iters 21
//   bench_fast_physics --json                         # machine-readable output
//   bench_fast_physics --profile search               # only search-shaped workload
//   bench_fast_physics --no-correctness               # skip parity check
//   bench_fast_physics --no-interleave                # disable A-B-B-A ordering
//
// Methodology:
//   - N iterations per phase with median/min/max/stdev/p5/p95
//   - Thermal stabilization warmup (3 consecutive runs within 5% CoV)
//   - Interleaved A-B-B-A timing to prevent ordering / thermal bias
//   - DoNotOptimize barriers against dead-code elimination
//   - Correctness check (EXACT parity) runs separately from timing
//
// Exit codes:  0 = pass, 1 = EXACT parity failure
// ============================================================================

#define CSB_PHYSICS_NO_GLOBAL_USING
#include "physics.h"
#include "fast_physics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <string>
#include <vector>

// ── Compiler barrier ──────────────────────────────────────────────────────
// Prevents the optimizer from eliding the computation entirely.
#ifdef __GNUC__
template <typename T>
static inline void DoNotOptimize(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}
#else
template <typename T>
static void DoNotOptimize(T const& value) {
    volatile auto sink = value;
    (void)sink;
}
#endif

// ── Timing ────────────────────────────────────────────────────────────────
using Clock = std::chrono::steady_clock;

static inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
}

// ── RNG (deterministic, same as existing bench) ───────────────────────────
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    int i(int lo, int hi) {
        return lo + static_cast<int>(next() % (uint64_t)(hi - lo + 1));
    }
    double d(double lo, double hi) {
        return lo + (hi - lo) *
                        (static_cast<double>(next() & 0xffffff) /
                         static_cast<double>(0xffffff));
    }
};

// ── Scenario builder (same seed strategy as bench_fast_physics) ───────────
struct Scenario {
    std::vector<csb::Point> track;
    int laps = 3;
    int turns = 0;
    std::vector<csb::PlayerMove> fid_moves;
    std::vector<csb::fast_physics::Move> fp_moves;
};

static csb::PlayerMove makeFidMove(double tx, double ty, int thrust,
                                    bool shield, bool boost, bool inv) {
    csb::PlayerMove m;
    m.target = {tx, ty};
    m.thrust = thrust;
    m.shield = shield;
    m.boost = boost;
    m.valid = true;
    m.invalid_input = inv;
    return m;
}

static csb::fast_physics::Move makeFastMove(double tx, double ty, int thrust,
                                             bool shield, bool boost, bool inv) {
    csb::fast_physics::Move m;
    m.tx = tx;
    m.ty = ty;
    m.thrust = thrust;
    m.shield = shield;
    m.boost = boost;
    m.invalid_input = inv;
    return m;
}

static Scenario build_scenario(uint64_t seed, int turns) {
    Rng rng(seed);
    Scenario sc;
    sc.laps = 3;
    const int ncp = 3 + rng.i(0, 2);
    for (int i = 0; i < ncp; ++i)
        sc.track.push_back({rng.d(1000, 15000), rng.d(1000, 8000)});
    sc.turns = turns;
    sc.fid_moves.reserve(turns * 4);
    sc.fp_moves.reserve(turns * 4);
    for (int t = 0; t < turns; ++t) {
        for (int p = 0; p < 4; ++p) {
            int mode = rng.i(0, 99);
            double tx = rng.d(-20000, 20000);
            double ty = rng.d(-20000, 20000);
            bool shield = false, boost = false, inv = false;
            int thr = 200;
            if (mode < 5) { shield = true; thr = 0; }
            else if (mode < 8) { boost = true; thr = 0; }
            else if (mode < 10) { inv = true; thr = 999; }
            else if (mode < 20) { thr = 0; }
            else if (mode < 30) { thr = rng.i(0, 200); }
            if (mode == 50) { tx = 0; ty = 0; }
            sc.fid_moves.push_back(makeFidMove(tx, ty, thr, shield, boost, inv));
            sc.fp_moves.push_back(makeFastMove(tx, ty, thr, shield, boost, inv));
        }
    }
    return sc;
}

// ── State seeding ─────────────────────────────────────────────────────────
static void seed_fid(csb::Game& g, const Scenario& sc, Rng& rng) {
    g.setTrack(sc.track, sc.laps);
    for (int i = 0; i < 4; ++i) {
        double x = rng.d(2000, 14000), y = rng.d(1000, 8000);
        double vx = rng.d(-400, 400), vy = rng.d(-400, 400);
        double ang = rng.d(-M_PI, M_PI);
        int n = 1 + rng.i(0, (int)sc.track.size());
        g.setPodState(i, x, y, vx, vy, ang, n, 0, 0);
    }
    g.setPlayerTimeouts(100, 100);
    g.turn = 1;
}

static void seed_fp(csb::fast_physics::Game& fp, const Scenario& sc,
                     const csb::Game& fid) {
    double xy[16];
    for (size_t i = 0; i < sc.track.size() && i < 8; ++i) {
        xy[2 * i] = sc.track[i].x;
        xy[2 * i + 1] = sc.track[i].y;
    }
    fp.clear();
    fp.setTrack(xy, (int)sc.track.size(), sc.laps);
    for (int i = 0; i < 4; ++i) {
        const auto& p = fid.pods[i];
        fp.setPod(i, p.p.x, p.p.y, p.s.x, p.s.y, p.angle, p.next,
                  p.shieldtimer, p.boosted);
        fp.pods[i].hasRotated = p.hasRotated;
        fp.pods[i].won = p.won;
    }
    fp.setTimeouts(fid.playerTimeout[0], fid.playerTimeout[1]);
    fp.turn = fid.turn;
}

// ── EXACT match check ─────────────────────────────────────────────────────
static bool exact_match(const csb::Game& fid,
                         const csb::fast_physics::Game& fp,
                         std::string& why) {
    if (fid.playerTimeout[0] != fp.playerTimeout[0] ||
        fid.playerTimeout[1] != fp.playerTimeout[1]) {
        why = "timeouts";
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        const auto& a = fid.pods[i];
        const auto& b = fp.pods[i];
        if (a.p.x != b.px || a.p.y != b.py) {
            why = "pos pod" + std::to_string(i);
            return false;
        }
        if (a.s.x != b.vx || a.s.y != b.vy) {
            why = "vel pod" + std::to_string(i);
            return false;
        }
        if (a.next != b.next) {
            why = "next pod" + std::to_string(i);
            return false;
        }
        if (a.shieldtimer != b.shieldtimer || a.boosted != b.boosted ||
            a.won != b.won || a.hasRotated != b.hasRotated) {
            why = "flags pod" + std::to_string(i);
            return false;
        }
        double da = std::fabs(a.angle - b.angle);
        if (da > M_PI) da = 2 * M_PI - da;
        if (da > 1e-12) {
            why = "ang pod" + std::to_string(i);
            return false;
        }
    }
    return true;
}

// ── Statistics ─────────────────────────────────────────────────────────────
struct Stats {
    double median_ns;
    double min_ns;
    double max_ns;
    double mean_ns;
    double stdev_ns;
    double p5_ns;
    double p95_ns;
    int n;
};

static Stats compute_stats(std::vector<double>& samples) {
    Stats st{};
    st.n = (int)samples.size();
    if (st.n == 0) return st;
    std::sort(samples.begin(), samples.end());
    st.min_ns = samples.front();
    st.max_ns = samples.back();
    st.median_ns = (st.n % 2 == 1)
                       ? samples[st.n / 2]
                       : (samples[st.n / 2 - 1] + samples[st.n / 2]) / 2.0;
    st.p5_ns = samples[std::max(0, (int)(st.n * 0.05))];
    st.p95_ns = samples[std::min(st.n - 1, (int)(st.n * 0.95))];
    double sum = 0;
    for (auto v : samples) sum += v;
    st.mean_ns = sum / st.n;
    double var = 0;
    for (auto v : samples) var += (v - st.mean_ns) * (v - st.mean_ns);
    st.stdev_ns = std::sqrt(var / st.n);
    return st;
}

// ── Benchmark kernel: Fidelity step ───────────────────────────────────────
static double run_fid_step(const std::vector<Scenario>& all, int scenarios,
                            int turns_per) {
    double sink = 0;
    uint64_t t0 = now_ns();
    for (int s = 0; s < scenarios; ++s) {
        Rng rng(0xBADC0DEULL + (uint64_t)s);
        csb::Game fid;
        seed_fid(fid, all[s], rng);
        for (int t = 0; t < turns_per; ++t) {
            csb::PlayerMove fm[4];
            for (int i = 0; i < 4; ++i)
                fm[i] = all[s].fid_moves[t * 4 + i];
            fid.step(fm);
        }
        sink += fid.pods[0].p.x;
    }
    uint64_t t1 = now_ns();
    DoNotOptimize(sink);
    return (double)(t1 - t0);
}

// ── Benchmark kernel: fast_physics step ───────────────────────────────────
static double run_fp_step(const std::vector<Scenario>& all, int scenarios,
                           int turns_per) {
    double sink = 0;
    uint64_t t0 = now_ns();
    for (int s = 0; s < scenarios; ++s) {
        Rng rng(0xBADC0DEULL + (uint64_t)s);
        csb::Game fid;
        seed_fid(fid, all[s], rng);
        csb::fast_physics::Game fp;
        seed_fp(fp, all[s], fid);
        for (int t = 0; t < turns_per; ++t) {
            csb::fast_physics::Move pm[4];
            for (int i = 0; i < 4; ++i)
                pm[i] = all[s].fp_moves[t * 4 + i];
            fp.step(pm);
        }
        sink += fp.pods[0].px;
    }
    uint64_t t1 = now_ns();
    DoNotOptimize(sink);
    return (double)(t1 - t0);
}

// ── Benchmark kernel: Fidelity string driver ──────────────────────────────
static double run_fid_driver(const std::vector<Scenario>& all, int scenarios,
                              int turns_per) {
    double sink = 0;
    uint64_t t0 = now_ns();
    for (int s = 0; s < scenarios; ++s) {
        Rng rng(0xBADC0DEULL + (uint64_t)s);
        csb::Game fid;
        seed_fid(fid, all[s], rng);
        for (int t = 0; t < turns_per; ++t) {
            for (int i = 0; i < 4; ++i) {
                const auto& m = all[s].fid_moves[t * 4 + i];
                std::string thr;
                if (m.invalid_input) thr = "999";
                else if (m.shield) thr = "SHIELD";
                else if (m.boost) thr = "BOOST";
                else thr = std::to_string(m.thrust);
                fid.applyAction(i, (int)m.target.x, (int)m.target.y, thr);
            }
            fid.nextTurn();
        }
        sink += fid.pods[0].p.x;
    }
    uint64_t t1 = now_ns();
    DoNotOptimize(sink);
    return (double)(t1 - t0);
}

// ── Benchmark kernel: search-shaped batch ─────────────────────────────────
static double run_search_batch(const Scenario& base_sc,
                                const csb::Game& base_fid, int games,
                                int depth) {
    std::vector<csb::fast_physics::Game> batch(games);
    for (int g = 0; g < games; ++g) {
        seed_fp(batch[g], base_sc, base_fid);
        for (int i = 0; i < 4; ++i) batch[g].pods[i].px += g * 3;
    }
    double sink = 0;
    uint64_t t0 = now_ns();
    for (int rep = 0; rep < depth; ++rep) {
        for (int g = 0; g < games; ++g) {
            csb::fast_physics::Move pm[4];
            for (int i = 0; i < 4; ++i)
                pm[i] = base_sc.fp_moves[(rep % base_sc.turns) * 4 + i];
            batch[g].step(pm);
            sink += batch[g].pods[0].px;
        }
    }
    uint64_t t1 = now_ns();
    DoNotOptimize(sink);
    return (double)(t1 - t0);
}

// Search with memcpy clone from root each depth (expansion-shaped).
static double run_search_clone(const Scenario& base_sc,
                                const csb::Game& base_fid, int games,
                                int depth) {
    csb::fast_physics::Game root;
    seed_fp(root, base_sc, base_fid);
    std::vector<csb::fast_physics::Move> flat((size_t)games * 4);
    double sink = 0;
    uint64_t t0 = now_ns();
    for (int rep = 0; rep < depth; ++rep) {
        std::vector<csb::fast_physics::Game> batch(games);
        for (int g = 0; g < games; ++g) {
            batch[g].copyFrom(root);
            batch[g].pods[0].px += g * 3;
            for (int i = 0; i < 4; ++i)
                flat[(size_t)g * 4 + i] =
                    base_sc.fp_moves[(rep % base_sc.turns) * 4 + i];
        }
        csb::fast_physics::step_batch(batch.data(), flat.data(), games);
        for (int g = 0; g < games; ++g) sink += batch[g].pods[0].px;
        root.copyFrom(batch[0]);
    }
    uint64_t t1 = now_ns();
    DoNotOptimize(sink);
    return (double)(t1 - t0);
}

// Undo-stack shaped: save snapshot, step, restore.
static double run_search_undo(const Scenario& base_sc,
                               const csb::Game& base_fid, int games,
                               int depth) {
    csb::fast_physics::Game root;
    seed_fp(root, base_sc, base_fid);
    double sink = 0;
    uint64_t t0 = now_ns();
    for (int rep = 0; rep < depth; ++rep) {
        for (int g = 0; g < games; ++g) {
            csb::fast_physics::Snapshot snap;
            root.saveSnapshot(snap);
            csb::fast_physics::Move pm[4];
            for (int i = 0; i < 4; ++i)
                pm[i] = base_sc.fp_moves[(rep % base_sc.turns) * 4 + i];
            root.pods[0].px += g * 3;
            root.step(pm);
            sink += root.pods[0].px;
            root.restoreSnapshot(snap);
        }
    }
    uint64_t t1 = now_ns();
    DoNotOptimize(sink);
    return (double)(t1 - t0);
}

// ── Warmup: run until 3 consecutive measurements within 5% CoV ───────────
static void stabilize(const std::vector<Scenario>& all, int scenarios,
                       int turns_per) {
    const int max_warmup = 20;
    const double cov_threshold = 0.05;  // 5%
    std::vector<double> recent;
    for (int w = 0; w < max_warmup; ++w) {
        double ns = run_fid_step(all, scenarios, turns_per);
        run_fp_step(all, scenarios, turns_per);
        recent.push_back(ns);
        if (recent.size() >= 3) {
            // Check last 3
            double sum = 0, sum2 = 0;
            for (int j = (int)recent.size() - 3; j < (int)recent.size(); ++j) {
                sum += recent[j];
                sum2 += recent[j] * recent[j];
            }
            double mean = sum / 3.0;
            double var = sum2 / 3.0 - mean * mean;
            double cov = std::sqrt(std::max(0.0, var)) / mean;
            if (cov < cov_threshold) return;
        }
    }
}

// ── Config ─────────────────────────────────────────────────────────────────
struct Config {
    int scenarios = 200;
    int turns = 500;
    int iters = 11;
    int search_games = 64;
    int search_depth = 500;
    bool json = false;
    bool correctness = true;
    bool run_step = true;
    bool run_driver = true;
    bool run_search = true;
    bool run_clone = true;
    bool run_undo = true;
    bool interleave = true;  // interleave fid/fp measurements
};

static Config parse_args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--scenarios" && i + 1 < argc) c.scenarios = std::atoi(argv[++i]);
        else if (arg == "--turns" && i + 1 < argc) c.turns = std::atoi(argv[++i]);
        else if (arg == "--iters" && i + 1 < argc) c.iters = std::atoi(argv[++i]);
        else if (arg == "--search-games" && i + 1 < argc) c.search_games = std::atoi(argv[++i]);
        else if (arg == "--search-depth" && i + 1 < argc) c.search_depth = std::atoi(argv[++i]);
        else if (arg == "--json") c.json = true;
        else if (arg == "--no-correctness") c.correctness = false;
        else if (arg == "--no-interleave") c.interleave = false;
        else if (arg == "--profile") {
            if (i + 1 < argc) {
                std::string p = argv[++i];
                c.run_step = c.run_driver = c.run_search = c.run_clone = c.run_undo = false;
                if (p == "step") c.run_step = true;
                else if (p == "driver") c.run_driver = true;
                else if (p == "search") c.run_search = true;
                else if (p == "clone") c.run_clone = true;
                else if (p == "undo") c.run_undo = true;
                else if (p == "all") {
                    c.run_step = c.run_driver = c.run_search = c.run_clone = c.run_undo = true;
                }
            }
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "Usage: bench_fast_physics [OPTIONS]\n"
                "  --scenarios N     Number of random game scenarios (default: 200)\n"
                "  --turns N         Turns per scenario (default: 500)\n"
                "  --iters N         Timed iterations per benchmark (default: 11)\n"
                "  --search-games N  Parallel games in search workload (default: 64)\n"
                "  --search-depth N  Steps per game in search workload (default: 500)\n"
                "  --json            Machine-readable JSON output\n"
                "  --no-correctness  Skip EXACT parity check\n"
                "  --no-interleave   Run all fid iters then all fp iters (not interleaved)\n"
                "  --profile WHAT    step|driver|search|clone|undo|all\n");
            std::exit(0);
        }
    }
    // Force odd iteration count for clean median
    if (c.iters % 2 == 0) c.iters++;
    if (c.iters < 3) c.iters = 3;
    return c;
}

// ── JSON helpers ──────────────────────────────────────────────────────────
static void json_stats(const char* name, const Stats& st, bool last = false) {
    std::printf("    \"%s\": {\n", name);
    std::printf("      \"median_ns_per_turn\": %.1f,\n", st.median_ns);
    std::printf("      \"min_ns\": %.1f,\n", st.min_ns);
    std::printf("      \"max_ns\": %.1f,\n", st.max_ns);
    std::printf("      \"mean_ns\": %.1f,\n", st.mean_ns);
    std::printf("      \"stdev_ns\": %.1f,\n", st.stdev_ns);
    std::printf("      \"p5_ns\": %.1f,\n", st.p5_ns);
    std::printf("      \"p95_ns\": %.1f,\n", st.p95_ns);
    std::printf("      \"n\": %d\n", st.n);
    std::printf("    }%s\n", last ? "" : ",");
}

static void print_stats_table(const char* label, const Stats& st,
                               long long total_steps) {
    std::printf("  %-22s median %7.1f ns/turn   min %7.1f  max %7.1f  "
                "stdev %5.1f  (p5=%5.1f p95=%5.1f)  [%d iters, %lld steps]\n",
                label, st.median_ns, st.min_ns, st.max_ns,
                st.stdev_ns, st.p5_ns, st.p95_ns, st.n, total_steps);
}

// ── Main ──────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);
    const long long total_steps = (long long)cfg.scenarios * cfg.turns;

    if (!cfg.json) {
        std::printf("╔══════════════════════════════════════════════════════════════╗\n");
        std::printf("║  STANDARDIZED PHYSICS BENCHMARK — mad_pod_arena             ║\n");
        std::printf("╚══════════════════════════════════════════════════════════════╝\n");
        std::printf("Config: scenarios=%d turns=%d iters=%d total_steps=%lld\n",
                    cfg.scenarios, cfg.turns, cfg.iters, total_steps);
        std::printf("        search=%dx%d interleave=%s\n",
                    cfg.search_games, cfg.search_depth,
                    cfg.interleave ? "yes" : "no");
#if defined(__clang__)
        std::printf("Compiler: Clang %d.%d.%d\n", __clang_major__,
                    __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
        std::printf("Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__,
                    __GNUC_PATCHLEVEL__);
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
        std::printf("Arch: arm64\n");
#elif defined(__x86_64__) || defined(_M_X64)
        std::printf("Arch: x86_64\n");
#endif
        std::printf("─────────────────────────────────────────────────────────────\n");
    }

    // ── Build scenarios ───────────────────────────────────────────────────
    std::vector<Scenario> all;
    all.reserve(cfg.scenarios);
    for (int s = 0; s < cfg.scenarios; ++s)
        all.push_back(build_scenario(0xC0FFEEULL + (uint64_t)s * 17ULL, cfg.turns));

    // ── Phase 0: Correctness ──────────────────────────────────────────────
    if (cfg.correctness) {
        if (!cfg.json)
            std::printf("\n[Correctness] Checking EXACT parity (%lld steps)...\n",
                        total_steps);
        for (int s = 0; s < cfg.scenarios; ++s) {
            Rng rng(0xBADC0DEULL + (uint64_t)s);
            csb::Game fid;
            seed_fid(fid, all[s], rng);
            csb::fast_physics::Game fp;
            seed_fp(fp, all[s], fid);
            for (int t = 0; t < cfg.turns; ++t) {
                csb::PlayerMove fm[4];
                csb::fast_physics::Move pm[4];
                for (int i = 0; i < 4; ++i) {
                    fm[i] = all[s].fid_moves[t * 4 + i];
                    pm[i] = all[s].fp_moves[t * 4 + i];
                }
                fid.step(fm);
                fp.step(pm);
                std::string why;
                if (!exact_match(fid, fp, why)) {
                    std::fprintf(stderr,
                                 "EXACT FAIL at scenario=%d turn=%d (%s)\n",
                                 s, t, why.c_str());
                    return 1;
                }
            }
        }
        if (!cfg.json)
            std::printf("[Correctness] PASS — %lld steps EXACT\n", total_steps);
    }

    // ── Stabilization warmup ──────────────────────────────────────────────
    if (!cfg.json) std::printf("\n[Warmup] Stabilizing thermals...\n");
    stabilize(all, cfg.scenarios, cfg.turns);
    if (!cfg.json) std::printf("[Warmup] Done\n");

    // ── Phase 1: Core step() — interleaved ────────────────────────────────
    std::vector<double> fid_step_ns, fp_step_ns;
    std::vector<double> fid_drv_ns, fp_drv2_ns;
    std::vector<double> search_ns, clone_ns, undo_ns;

    if (cfg.run_step) {
        if (!cfg.json) std::printf("\n[Phase 1] Core step() — %d iterations\n", cfg.iters);
        fid_step_ns.reserve(cfg.iters);
        fp_step_ns.reserve(cfg.iters);

        if (cfg.interleave) {
            // Interleaved: ABABABAB... prevents systematic thermal drift
            for (int it = 0; it < cfg.iters; ++it) {
                // Alternate which goes first each iteration
                if (it % 2 == 0) {
                    double fns = run_fid_step(all, cfg.scenarios, cfg.turns);
                    double pns = run_fp_step(all, cfg.scenarios, cfg.turns);
                    fid_step_ns.push_back(fns / total_steps);
                    fp_step_ns.push_back(pns / total_steps);
                } else {
                    double pns = run_fp_step(all, cfg.scenarios, cfg.turns);
                    double fns = run_fid_step(all, cfg.scenarios, cfg.turns);
                    fp_step_ns.push_back(pns / total_steps);
                    fid_step_ns.push_back(fns / total_steps);
                }
                if (!cfg.json)
                    std::printf("  iter %2d: fid=%.1f ns  fp=%.1f ns\n",
                                it + 1, fid_step_ns.back(), fp_step_ns.back());
            }
        } else {
            // Non-interleaved (original style)
            for (int it = 0; it < cfg.iters; ++it) {
                double ns = run_fid_step(all, cfg.scenarios, cfg.turns);
                fid_step_ns.push_back(ns / total_steps);
            }
            for (int it = 0; it < cfg.iters; ++it) {
                double ns = run_fp_step(all, cfg.scenarios, cfg.turns);
                fp_step_ns.push_back(ns / total_steps);
            }
        }
    }

    // ── Phase 2: String driver ────────────────────────────────────────────
    if (cfg.run_driver) {
        if (!cfg.json) std::printf("\n[Phase 2] String driver — %d iterations\n", cfg.iters);
        fid_drv_ns.reserve(cfg.iters);
        fp_drv2_ns.reserve(cfg.iters);
        for (int it = 0; it < cfg.iters; ++it) {
            if (it % 2 == 0) {
                double dns = run_fid_driver(all, cfg.scenarios, cfg.turns);
                double pns = run_fp_step(all, cfg.scenarios, cfg.turns);
                fid_drv_ns.push_back(dns / total_steps);
                fp_drv2_ns.push_back(pns / total_steps);
            } else {
                double pns = run_fp_step(all, cfg.scenarios, cfg.turns);
                double dns = run_fid_driver(all, cfg.scenarios, cfg.turns);
                fp_drv2_ns.push_back(pns / total_steps);
                fid_drv_ns.push_back(dns / total_steps);
            }
        }
    }

    // ── Phase 3: Search-shaped batch ──────────────────────────────────────
    Rng brng(1);
    csb::Game base_fid;
    seed_fid(base_fid, all[0], brng);
    const long long search_steps = (long long)cfg.search_games * cfg.search_depth;
    if (cfg.run_search) {
        if (!cfg.json) std::printf("\n[Phase 3] Search-shaped batch — %d iterations\n", cfg.iters);
        search_ns.reserve(cfg.iters);
        for (int it = 0; it < cfg.iters; ++it) {
            double ns = run_search_batch(all[0], base_fid, cfg.search_games,
                                          cfg.search_depth);
            search_ns.push_back(ns / search_steps);
        }
    }
    if (cfg.run_clone) {
        if (!cfg.json) std::printf("\n[Phase 4] Clone+step_batch — %d iterations\n", cfg.iters);
        clone_ns.reserve(cfg.iters);
        for (int it = 0; it < cfg.iters; ++it) {
            double ns = run_search_clone(all[0], base_fid, cfg.search_games,
                                          cfg.search_depth);
            clone_ns.push_back(ns / search_steps);
        }
    }
    if (cfg.run_undo) {
        if (!cfg.json) std::printf("\n[Phase 5] Snapshot undo — %d iterations\n", cfg.iters);
        undo_ns.reserve(cfg.iters);
        for (int it = 0; it < cfg.iters; ++it) {
            double ns = run_search_undo(all[0], base_fid, cfg.search_games,
                                         cfg.search_depth);
            undo_ns.push_back(ns / search_steps);
        }
    }

    // ── Compute stats ─────────────────────────────────────────────────────
    Stats fid_st{}, fp_st{}, fid_drv_st{}, fp_drv2_st{}, search_st{}, clone_st{}, undo_st{};
    if (!fid_step_ns.empty()) fid_st = compute_stats(fid_step_ns);
    if (!fp_step_ns.empty()) fp_st = compute_stats(fp_step_ns);
    if (!fid_drv_ns.empty()) fid_drv_st = compute_stats(fid_drv_ns);
    if (!fp_drv2_ns.empty()) fp_drv2_st = compute_stats(fp_drv2_ns);
    if (!search_ns.empty()) search_st = compute_stats(search_ns);
    if (!clone_ns.empty()) clone_st = compute_stats(clone_ns);
    if (!undo_ns.empty()) undo_st = compute_stats(undo_ns);

    // ── Output ────────────────────────────────────────────────────────────
    if (cfg.json) {
        std::printf("{\n");
        std::printf("  \"benchmark\": \"bench_fast_physics\",\n");
        std::printf("  \"scenarios\": %d,\n", cfg.scenarios);
        std::printf("  \"turns\": %d,\n", cfg.turns);
        std::printf("  \"total_steps\": %lld,\n", total_steps);
        std::printf("  \"iters\": %d,\n", cfg.iters);
        std::printf("  \"interleaved\": %s,\n", cfg.interleave ? "true" : "false");
#if defined(__clang__)
        std::printf("  \"compiler\": \"clang-%d.%d.%d\",\n", __clang_major__,
                    __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
        std::printf("  \"compiler\": \"gcc-%d.%d.%d\",\n", __GNUC__,
                    __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
#if defined(__aarch64__)
        std::printf("  \"arch\": \"arm64\",\n");
#elif defined(__x86_64__)
        std::printf("  \"arch\": \"x86_64\",\n");
#endif
        std::printf("  \"results\": {\n");

        bool first_result = true;
        if (!fid_step_ns.empty()) {
            if (!first_result) std::printf(",\n");
            first_result = false;
            json_stats("fidelity_step", fid_st);
            json_stats("fast_physics_step", fp_st);
            double speedup = fid_st.median_ns / fp_st.median_ns;
            std::printf("    \"step_speedup\": %.3f,\n", speedup);
        }
        if (!fid_drv_ns.empty()) {
            json_stats("fidelity_driver", fid_drv_st);
            json_stats("fast_physics_vs_driver", fp_drv2_st);
            double speedup = fid_drv_st.median_ns / fp_drv2_st.median_ns;
            std::printf("    \"driver_speedup\": %.3f,\n", speedup);
        }
        if (!search_ns.empty()) {
            json_stats("search_batch", search_st, clone_ns.empty() && undo_ns.empty());
        }
        if (!clone_ns.empty()) {
            json_stats("search_clone_batch", clone_st, undo_ns.empty());
        }
        if (!undo_ns.empty()) {
            json_stats("search_undo", undo_st, true);
        }

        std::printf("  }\n");
        std::printf("}\n");
    } else {
        std::printf("\n═══════════════════════════════════════════════════════════════\n");
        std::printf("  RESULTS (all ns/turn, lower = faster)\n");
        std::printf("═══════════════════════════════════════════════════════════════\n\n");

        if (!fid_step_ns.empty()) {
            std::printf("Phase 1: Core step() — apples-to-apples physics kernel\n");
            print_stats_table("Fidelity step", fid_st, total_steps);
            print_stats_table("fast_physics step", fp_st, total_steps);
            double speedup = fid_st.median_ns / fp_st.median_ns;
            std::printf("  → Speedup (median): %.3fx\n\n", speedup);
        }

        if (!fid_drv_ns.empty()) {
            std::printf("Phase 2: String driver — Fidelity applyAction(string)+nextTurn vs fp.step\n");
            print_stats_table("Fidelity driver", fid_drv_st, total_steps);
            print_stats_table("fast_physics step", fp_drv2_st, total_steps);
            double speedup = fid_drv_st.median_ns / fp_drv2_st.median_ns;
            std::printf("  → Speedup (median): %.3fx\n\n", speedup);
        }

        if (!search_ns.empty()) {
            std::printf("Phase 3: Search-shaped batch (%d games × %d steps)\n",
                        cfg.search_games, cfg.search_depth);
            print_stats_table("fast_physics batch", search_st,
                              (long long)cfg.search_games * cfg.search_depth);
            std::printf("\n");
        }
        if (!clone_ns.empty()) {
            std::printf("Phase 4: memcpy clone + step_batch\n");
            print_stats_table("clone+batch", clone_st,
                              (long long)cfg.search_games * cfg.search_depth);
            std::printf("\n");
        }
        if (!undo_ns.empty()) {
            std::printf("Phase 5: snapshot save/step/restore\n");
            print_stats_table("undo stack", undo_st,
                              (long long)cfg.search_games * cfg.search_depth);
            std::printf("\n");
        }

        std::printf("═══════════════════════════════════════════════════════════════\n");
        std::printf("  SUMMARY\n");
        std::printf("═══════════════════════════════════════════════════════════════\n");
        if (!fid_step_ns.empty()) {
            double sp = fid_st.median_ns / fp_st.median_ns;
            double cv_fid = fid_st.stdev_ns / fid_st.median_ns * 100;
            double cv_fp = fp_st.stdev_ns / fp_st.median_ns * 100;
            std::printf("  Core step:    %.3fx  (CoV: fid=%.1f%% fp=%.1f%%)\n",
                        sp, cv_fid, cv_fp);
        }
        if (!fid_drv_ns.empty()) {
            double sp = fid_drv_st.median_ns / fp_drv2_st.median_ns;
            std::printf("  String driver: %.3fx\n", sp);
        }
        if (!search_ns.empty()) {
            std::printf("  Search batch:  %.1f ns/turn (median)\n",
                        search_st.median_ns);
        }
        if (!clone_ns.empty()) {
            std::printf("  Clone+batch:   %.1f ns/turn (median)\n",
                        clone_st.median_ns);
        }
        if (!undo_ns.empty()) {
            std::printf("  Undo stack:    %.1f ns/turn (median)\n",
                        undo_st.median_ns);
        }
        std::printf("═══════════════════════════════════════════════════════════════\n");
    }

    return 0;
}
