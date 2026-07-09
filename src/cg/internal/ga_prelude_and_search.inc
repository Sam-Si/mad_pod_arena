#pragma GCC optimize("O3,inline,omit-frame-pointer,unroll-loops")

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <cstddef>

using namespace std;

// Pure scoring/action math (Feathers extract). Amalgam pastes ga_pure.h before this file.
#if !defined(CG_BOT_AMALGAM)
#include "ga_pure.h"
#endif

// Physics numeric law SSOT: src/core/constants.h.
// kCgFriction is the only physics-law constant re-stated here (free-flight eval trunc).
// Collision constants for the bot live in amalgamated fast.h (csb::fast::*).
// Value MUST match csb_constants::kFriction (sim/check_ssot_policy.py).
static constexpr double kCgFriction = 0.85;  // csb_constants::kFriction

// === Default Time Limits (overridable via BotConfig for benchmarking) ===
constexpr double DEFAULT_TURN_TIME_LIMIT_MS = 75.0;
constexpr double DEFAULT_FIRST_TURN_TIME_LIMIT_MS = 1000.0;

// === Runner & Blocker Heuristic Weights ===
constexpr double RUNNER_BYPASS_WEIGHT = 20.0;
constexpr double RUNNER_FAST_ACT_PENALTY = 1000.0;
constexpr double OPPONENT_DELAY_WEIGHT = 10000.0;
constexpr double OPPONENT_DISTANCE_WEIGHT = 10.0;
constexpr double BLOCKER_REST_THRESHOLD = 300.0;
constexpr double BLOCKER_REST_SOFT_FACTOR = 0.1;
constexpr double BLOCKER_REST_PENALTY_FACTOR = 0.045;
constexpr double BLOCKER_FACING_WEIGHT = 30.0;
constexpr double BLOCKER_STAY_IN_FRONT_WEIGHT = 30.0;

// === Shield Costs & Thrust Bonuses ===
constexpr double RUNNER_SHIELD_COST = 330.0;
constexpr double RUNNER_THRUST_BONUS = 0.16;
constexpr double BLOCKER_SHIELD_COST = 495.0;
constexpr double BLOCKER_THRUST_BONUS = 0.06;

#ifdef CG_STANDALONE
// === Inline engine declarations for CodinGame submission ===
// Source of truth: src/engine/engine.h and src/engine/bot.h
extern const double PI;
extern double cos_lut[360];
extern double sin_lut[360];
extern thread_local bool g_friendly_collision;
extern thread_local int g_runner_id;

void InitLUT();
void SeedRand(uint32_t seed);
uint32_t FastRand();
int FastRandInt(int min, int max);

class Timer {
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
public:
    void Start();
    double ElapsedMs() const;
};

struct Vec2 {
    double x, y;
    Vec2();
    Vec2(double x, double y);
    Vec2 Add(const Vec2& o) const;
    Vec2 Sub(const Vec2& o) const;
    double DistanceSq(const Vec2& o) const;
    double Distance(const Vec2& o) const;
};


class GameEngine {
public:
    static double NormalizeAngle(double a);
    static double ShortestAngleDiff(double current, double target);
    static double RadToDeg(double radians);
};

struct PodAction {
    double tx, ty;
    int thrust; 
};

struct Pod {
    int id;
    int team;
    Vec2 pos, vel;
    double angle;
    int next_cp_id;
    bool boost_available;
    int shield_cd;
    int timeout; // To track 100 turns limit
    int laps_completed;

    Pod();
    double Mass() const;
    void ApplyGAAction(double angle_shift, int thrust);
    void ApplyServerAction(double tx, double ty, int thrust_val);
    void Move(double t);
    void EndTurn();
};

struct BotConfig {
    // Mirrored from src/cg/bot_config.h for CG_STANDALONE paste (must stay in sync).
    // GA Core
    int horizon = 6;           // Turns of lookahead (4-8)
    int population = 50;       // GA population size (20-100)
    
    // Runner Evaluation Weights
    double dist_weight = 1.5;       // Distance-to-CP penalty
    double align_weight = 3.0;      // Velocity alignment reward
    double speed_bonus = 0.5;       // Raw speed reward
    double lateral_penalty = 0.5;   // Sideways drift penalty
    double angle_penalty = 25.0;    // Angle-to-target penalty
    double corner_cut_dist = 300.0; // Corner-cutting offset (units)
    
    // Blocker Weights
    double block_weight = 1.0;      // Blocker aggressiveness (was 5.0 - way too dominant)
    double shield_penalty = 50.0;   // Shield usage penalty
    double shield_ram_dist = 850.0; // Distance to trigger shield-ram
    
    // Coordination
    double opp_penalty = 0.5;       // Penalize opponent's progress in eval
    
    // Time allocation
    double opp_model_ms = 0.0;      // Skip opponent GA model - use proxy instead

    double runner_bypass_weight = 20.0;
    double blocker_stay_in_front_weight = 30.0;
    double blocker_facing_weight = 30.0;
    double runner_evasion_weight = 1.0;

    // Time allocation (configurable for benchmarking)
    double turn_time_limit_ms = 75.0;
    double first_turn_time_limit_ms = 1000.0;

    int num_threads = std::max(1, (int)std::thread::hardware_concurrency());

    std::string name = "DefaultGA";

    void Randomize();
};

class IBot {
public:
    virtual ~IBot() = default;
    virtual std::string GetName() const = 0;
    
    // Called once per game before the first turn
    virtual void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) = 0;
    
    // Called every turn. Return exactly 2 PodActions (one for each of your pods)
    virtual std::vector<PodAction> GetActions(const std::vector<Pod>& pods) = 0;
    virtual void SetRoles(int runner_idx, int blocker_idx) {}
};

#else // !CG_STANDALONE — use shared engine
#include "src/engine/engine.h"
#include "src/engine/bot.h"
#include "src/cg/bot_config.h"
#endif // CG_STANDALONE

const int MAX_HORIZON = 8;
const int MAX_POP = 64;

struct Action {
    double angle = 0;  // Angle shift [-18, 18] in degrees
    int thrust = 0;    // [0, 200]
    void Randomize();
    void MutateAggressive(double amplitude);
    void SmallMutate();
};

// Solution encodes moves for BOTH runner and blocker (combined GA).
struct Solution {
    double score;
    Action runner_moves[MAX_HORIZON];
    Action blocker_moves[MAX_HORIZON];
    int runner_shield_step;   // Turn to use shield (0-2 active, >=3 no shield)
    int blocker_shield_step;

    Solution();
    void Randomize(int horizon);
    void MutateFromOne(const Solution& parent, int horizon, double amplitude);
    void CrossoverFromTwo(const Solution& a, const Solution& b, int horizon);
};

Action MakeGoToTarget(const Pod& pod, double tx, double ty, int thrust_val = 200);

// === 8-Core Persistent Thread Pool ===
class GABotThreadPool {
    struct Worker {
        std::thread thread;
        std::mutex mutex;
        std::condition_variable cv;
        std::function<void()> task = nullptr;
        bool has_task = false;
        bool stop = false;
    };
    std::vector<std::unique_ptr<Worker>> workers;
    std::mutex master_mutex;
    std::condition_variable master_cv;
    std::atomic<int> active_workers{0};

public:
    GABotThreadPool(int num_threads) {
        for (int i = 0; i < num_threads - 1; ++i) {
            auto w = std::make_unique<Worker>();
            Worker* ptr = w.get();
            ptr->thread = std::thread([this, ptr]() {
                while (true) {
                    std::function<void()> local_task = nullptr;
                    {
                        std::unique_lock<std::mutex> lock(ptr->mutex);
                        ptr->cv.wait(lock, [ptr]() { return ptr->has_task || ptr->stop; });
                        if (ptr->stop) return;
                        local_task = std::move(ptr->task);
                        ptr->has_task = false;
                    }
                    if (local_task) {
                        local_task();
                    }
                    int prev = active_workers.fetch_sub(1);
                    if (prev == 1) {
                        std::unique_lock<std::mutex> lock(master_mutex);
                        master_cv.notify_one();
                    }
                }
            });
            workers.push_back(std::move(w));
        }
    }

    ~GABotThreadPool() {
        for (auto& w : workers) {
            {
                std::unique_lock<std::mutex> lock(w->mutex);
                w->stop = true;
            }
            w->cv.notify_one();
            if (w->thread.joinable()) {
                w->thread.join();
            }
        }
    }

    void RunParallel(const std::vector<std::function<void()>>& tasks) {
        if (tasks.empty()) return;
        int n_tasks = tasks.size();
        int n_workers = workers.size();
        
        int workers_to_run = std::min(n_tasks - 1, n_workers);
        if (workers_to_run < 0) workers_to_run = 0;

        active_workers.store(workers_to_run);

        // Dispatch tasks to workers
        for (int i = 0; i < workers_to_run; ++i) {
            {
                std::unique_lock<std::mutex> lock(workers[i]->mutex);
                workers[i]->task = tasks[i + 1];
                workers[i]->has_task = true;
            }
            workers[i]->cv.notify_one();
        }

        // Main thread runs task 0
        tasks[0]();

        // Wait for workers
        if (workers_to_run > 0) {
            std::unique_lock<std::mutex> lock(master_mutex);
            master_cv.wait(lock, [this]() { return active_workers.load() == 0; });
        }
    }
};

// === Asynchronous Migration Channel ===
struct MigrationChannel {
    std::mutex mutex;
    Solution solution;
    bool initialized = false;
};

class GABot : public IBot {
    int laps_ = 3;
    int cp_count_ = 0;
    std::vector<Vec2> cps_;
    int team_id_ = 0;
    int runner_idx_ = 0;
    int blocker_idx_ = 1;
    BotConfig config_;
    bool has_prev_best_ = false;
    Solution prev_best_;
    int turn_count_ = 0;
    double avg_dist_ = 0.0;
    std::shared_ptr<GABotThreadPool> thread_pool_;

    // Pre-computed race geometry
    std::vector<double> dist_to_end_;    // [linear_idx] remaining distance to finish
    std::vector<Vec2> entry_points_;     // [cp_id] smart checkpoint entry points
    std::vector<Vec2> ram_rest_points_;  // [cp_id] blocker camping positions
    std::vector<double> cp_distances_;
    int total_cps_in_race_ = 0;

public:
    static bool verbose;
    GABot(BotConfig config = BotConfig());
    std::string GetName() const override;
    void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) override;
    void SetRoles(int runner_idx, int blocker_idx) override { runner_idx_ = runner_idx; blocker_idx_ = blocker_idx; }
    std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override;
};

class Evolution {
public:
    static void ApplyBasicProxy(Pod& p, const std::vector<Vec2>& cps) {
        const Vec2& tgt = cps[p.next_cp_id];
        const double shift = ga_pure::AngleShiftTowardTarget(
            p.angle, p.pos.x, p.pos.y, tgt.x, tgt.y);
        p.ApplyGAAction(shift, 200);
    }
};

#ifdef CG_STANDALONE
// ======== ENGINE IMPLEMENTATIONS ========
#ifndef CG_BOT_AMALGAM
#include "fast.h"
#endif

const double PI = 3.14159265358979323846;
double cos_lut[360];
double sin_lut[360];
thread_local uint32_t xor_state = 2463534242;
thread_local bool g_friendly_collision = false;

void SeedRand(uint32_t seed) {
    xor_state = seed;
}

void InitLUT() {
    for (int i = 0; i < 360; ++i) {
        cos_lut[i] = std::cos(i * PI / 180.0);
        sin_lut[i] = std::sin(i * PI / 180.0);
    }
}

uint32_t FastRand() {
    xor_state ^= xor_state << 13;
    xor_state ^= xor_state >> 17;
    xor_state ^= xor_state << 5;
    return xor_state;
}

int FastRandInt(int min, int max) {
    uint32_t range = max - min + 1;
    uint64_t multi = (uint64_t)FastRand() * range;
    return min + (int)(multi >> 32);
}

void Timer::Start() { start_time = std::chrono::high_resolution_clock::now(); }
double Timer::ElapsedMs() const { return std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start_time).count(); }

Vec2::Vec2() : x(0), y(0) {}
Vec2::Vec2(double x, double y) : x(x), y(y) {}
Vec2 Vec2::Add(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
Vec2 Vec2::Sub(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
double Vec2::DistanceSq(const Vec2& o) const { return (x - o.x)*(x - o.x) + (y - o.y)*(y - o.y); }
double Vec2::Distance(const Vec2& o) const { return std::sqrt(DistanceSq(o)); }

double GameEngine::NormalizeAngle(double a) {
    while (a >= 360.0) a -= 360.0;
    while (a < 0.0) a += 360.0;
    return a;
}
double GameEngine::ShortestAngleDiff(double current, double target) {
    double diff = target - current;
    while (diff > 180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;
    return diff;
}
double GameEngine::RadToDeg(double radians) { return radians * 180.0 / PI; }

Pod::Pod() : id(0), team(0), pos(0,0), vel(0,0), angle(-1.0), next_cp_id(0), boost_available(true), shield_cd(0), timeout(0), laps_completed(0) {}
double Pod::Mass() const { return (shield_cd == 4) ? 10.0 : 1.0; }

struct TrigLUT {
    double cos_val[3600];
    double sin_val[3600];
    TrigLUT() {
        for (int i = 0; i < 3600; ++i) {
            double rad = (i * 0.1) * (3.14159265358979323846 / 180.0);
            cos_val[i] = std::cos(rad);
            sin_val[i] = std::sin(rad);
        }
    }
    inline void Lookup(double deg, double& out_cos, double& out_sin) const {
        double a = deg;
        while (a >= 360.0) a -= 360.0;
        while (a < 0.0) a += 360.0;
        int idx = static_cast<int>(a * 10.0 + 0.5);
        if (idx >= 3600) idx = 0;
        out_cos = cos_val[idx];
        out_sin = sin_val[idx];
    }
};

inline const TrigLUT& GetTrigLUT() {
    static TrigLUT lut;
    return lut;
}

void Pod::ApplyGAAction(double angle_shift, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 4; thrust_val = 0; }
    else if (shield_cd > 0) { thrust_val = 0; }
    if (thrust_val == 650) boost_available = false;

    if (angle < 0) angle = 0;
    else angle = GameEngine::NormalizeAngle(angle + angle_shift);

    double c, s;
    GetTrigLUT().Lookup(angle, c, s);
    vel.x += c * thrust_val;
    vel.y += s * thrust_val;
}

void Pod::ApplyServerAction(double tx, double ty, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 4; thrust_val = 0; }
    else if (shield_cd > 0) { thrust_val = 0; }
    if (thrust_val == 650) { 
        if (boost_available) { thrust_val = 650; boost_available = false; }
        else thrust_val = 200;
    }

    // Reference skips rotation+thrust when target equals current position
    if (tx == pos.x && ty == pos.y) return;

    double target_angle = GameEngine::RadToDeg(std::atan2(ty - pos.y, tx - pos.x));
    
    if (angle < 0) {
        angle = GameEngine::NormalizeAngle(target_angle);
    } else {
        double diff = GameEngine::ShortestAngleDiff(angle, target_angle);
        if (diff > 18.0) diff = 18.0;
        if (diff < -18.0) diff = -18.0;
        angle = GameEngine::NormalizeAngle(angle + diff);
    }

    double rad = angle * PI / 180.0;
    vel.x += std::cos(rad) * thrust_val;
    vel.y += std::sin(rad) * thrust_val;
}

void Pod::Move(double t) {
    pos.x += vel.x * t;
    pos.y += vel.y * t;
}

void Pod::EndTurn() {
    pos.x = std::floor(pos.x + 0.5);
    pos.y = std::floor(pos.y + 0.5);
    vel.x = ga_pure::FreeFlightFrictionVel(vel.x, kCgFriction);
    vel.y = ga_pure::FreeFlightFrictionVel(vel.y, kCgFriction);
    if (shield_cd > 0) shield_cd--;
}

inline void FastSimulateTurn(Pod* pods) {
    static_assert(sizeof(Pod) == sizeof(csb::fast::Pod), "standalone Pod layout must match csb::fast::Pod");
    static_assert(offsetof(Pod, id) == offsetof(csb::fast::Pod, id), "id offset");
    static_assert(offsetof(Pod, team) == offsetof(csb::fast::Pod, team), "team offset");
    static_assert(offsetof(Pod, pos) == offsetof(csb::fast::Pod, pos), "pos offset");
    static_assert(offsetof(Pod, vel) == offsetof(csb::fast::Pod, vel), "vel offset");
    static_assert(offsetof(Pod, angle) == offsetof(csb::fast::Pod, angle), "angle offset");
    static_assert(offsetof(Pod, next_cp_id) == offsetof(csb::fast::Pod, next_cp_id), "next_cp_id offset");
    static_assert(offsetof(Pod, boost_available) == offsetof(csb::fast::Pod, boost_available), "boost_available offset");
    static_assert(offsetof(Pod, shield_cd) == offsetof(csb::fast::Pod, shield_cd), "shield_cd offset");
    static_assert(offsetof(Pod, timeout) == offsetof(csb::fast::Pod, timeout), "timeout offset");
    static_assert(offsetof(Pod, laps_completed) == offsetof(csb::fast::Pod, laps_completed), "laps_completed offset");
    csb::fast::SimulateTurn(reinterpret_cast<csb::fast::Pod*>(pods));
}

#endif // CG_STANDALONE

// ======== BOT IMPLEMENTATION ========
// Thread-local reusable simulation buffer (eliminates heap alloc per Simulate call)
static thread_local Pod g_sim[4];

static inline double GetCPIntersectionTime(const Pod& p, const Vec2& cp) {
    double x = p.pos.x - cp.x;
    double y = p.pos.y - cp.y;
    double vx = p.vel.x;
    double vy = p.vel.y;

    double a = vx * vx + vy * vy;
    if (a < 0.00001) return -1.0;

    double b = 2.0 * (x * vx + y * vy);
    double c = x * x + y * y - 360000.0;

    if (c < 0.0) return 0.0;

    double delta = b * b - 4.0 * a * c;
    if (delta < 0.0) return -1.0;

    double t = (-b - std::sqrt(delta)) / (2.0 * a);
    if (t < 0.0) return -1.0;
    return t;
}

// ======== ACTION ========
void Action::Randomize() {
    angle = FastRandInt(-180, 180) / 10.0;
    // Bias toward high thrust (matches legacy bot's [-100, 500] clamped range)
    int raw = FastRandInt(-100, 500);
    thrust = (raw < 0) ? 0 : (raw > 200) ? 200 : raw;
}

void Action::MutateAggressive(double amplitude) {
    double threshold = 0.25 + amplitude;
    if (FastRandInt(0, 1000) / 1000.0 < threshold) {
        angle = FastRandInt(-400, 400) / 10.0;
        if (angle < -18.0) angle = -18.0;
        if (angle > 18.0) angle = 18.0;
    }
    if (FastRandInt(0, 1000) / 1000.0 < threshold) {
        int raw = FastRandInt(-100, 500);
        thrust = (raw < 0) ? 0 : (raw > 200) ? 200 : raw;
    }
}

void Action::SmallMutate() {
    angle += FastRandInt(-120, 120) / 10.0;  // ±12° (matches legacy)
    if (angle < -18.0) angle = -18.0;
    if (angle > 18.0) angle = 18.0;
    thrust += FastRandInt(-50, 50);
    if (thrust < 0) thrust = 0;
    if (thrust > 200) thrust = 200;
}

void BotConfig::Randomize() {
    horizon = FastRandInt(4, 8);
    population = FastRandInt(20, 48);
    dist_weight = FastRandInt(5, 25) / 10.0;
    align_weight = FastRandInt(5, 50) / 10.0;
    speed_bonus = FastRandInt(0, 10) / 10.0;
    lateral_penalty = FastRandInt(0, 20) / 10.0;
    angle_penalty = FastRandInt(10, 60);
    corner_cut_dist = FastRandInt(0, 600);
    block_weight = FastRandInt(0, 30) / 10.0;
    shield_penalty = FastRandInt(0, 100);
    shield_ram_dist = FastRandInt(600, 1200);
    opp_penalty = FastRandInt(0, 20) / 10.0;
    opp_model_ms = 0;

    runner_bypass_weight = FastRandInt(50, 500) / 10.0;
    blocker_stay_in_front_weight = FastRandInt(50, 400) / 10.0;
    blocker_facing_weight = FastRandInt(50, 400) / 10.0;
    runner_evasion_weight = FastRandInt(1, 10) / 10.0;
}

Action MakeGoToTarget(const Pod& pod, double tx, double ty, int thrust_val) {
    const double shift = ga_pure::AngleShiftTowardTarget(
        pod.angle, pod.pos.x, pod.pos.y, tx, ty);
    return {shift, thrust_val};
}

// ======== SOLUTION (combined: runner + blocker) ========
Solution::Solution() : score(-1e18), runner_shield_step(MAX_HORIZON), blocker_shield_step(MAX_HORIZON) {}

void Solution::Randomize(int horizon) {
    for (int i = 0; i < horizon; ++i) {
        runner_moves[i].Randomize();
        blocker_moves[i].Randomize();
    }
    runner_shield_step = FastRandInt(0, 7);   // 0-2 active, 3-7 = no shield
    blocker_shield_step = FastRandInt(0, 7);
}

void Solution::MutateFromOne(const Solution& parent, int horizon, double amplitude) {
    double threshold = 0.25 + amplitude;
    for (int t = 0; t < horizon; ++t) {
        runner_moves[t] = parent.runner_moves[t];
        runner_moves[t].MutateAggressive(amplitude);
        blocker_moves[t] = parent.blocker_moves[t];
        blocker_moves[t].MutateAggressive(amplitude);
    }
    runner_shield_step = parent.runner_shield_step;
    blocker_shield_step = parent.blocker_shield_step;
    if (FastRandInt(0, 1000) / 1000.0 < threshold) runner_shield_step = FastRandInt(0, 7);
    if (FastRandInt(0, 1000) / 1000.0 < threshold) blocker_shield_step = FastRandInt(0, 7);
}

void Solution::CrossoverFromTwo(const Solution& a, const Solution& b, int horizon) {
    for (int t = 0; t < horizon; ++t) {
        runner_moves[t] = (FastRandInt(0, 1) == 0) ? a.runner_moves[t] : b.runner_moves[t];
        blocker_moves[t] = (FastRandInt(0, 1) == 0) ? a.blocker_moves[t] : b.blocker_moves[t];
    }
    runner_shield_step = (FastRandInt(0, 1) == 0) ? a.runner_shield_step : b.runner_shield_step;
    blocker_shield_step = (FastRandInt(0, 1) == 0) ? a.blocker_shield_step : b.blocker_shield_step;
}

// ======== SIMULATION CONTEXT (internal) ========
struct SimCtx {
    const vector<Vec2>* cps;
    const vector<double>* dist_to_end;
    const vector<Vec2>* entry_points;
    const vector<Vec2>* ram_rest_points;
    int cp_count;
    int laps;
    int start_idx;
    int opp_start_idx;
    int runner_idx;
    int ram_beacon;       // Which CP the blocker should camp near
    bool risk_timeout;    // If true, blocker races instead of blocking
    const BotConfig* config; // Added
    bool force_boost;     // Force runner to boost on Turn 0
    Action opp_moves[MAX_HORIZON];
    bool has_opp_blocker_override = false;
    Action opp_blocker_override;
};

struct TeamAction {
    Action runner_move;
    Action blocker_move;
    int runner_shield_step = MAX_HORIZON;
    int blocker_shield_step = MAX_HORIZON;
    std::string name;
};

static double EvaluateTacticalCell(const TeamAction& my_act, const TeamAction& opp_act,
                                    const vector<Pod>& base_pods, const SimCtx& ctx) {
    const int n = ctx.cp_count;
    const int runner_pod = ctx.start_idx + ctx.runner_idx;
    const int blocker_pod = ctx.start_idx + (1 - ctx.runner_idx);
    const auto& cps = *ctx.cps;
    const auto& dte = *ctx.dist_to_end;
    const auto& ep = *ctx.entry_points;

    std::memcpy(g_sim, base_pods.data(), 4 * sizeof(Pod));
    g_friendly_collision = false;

    // Identify opponent runner/blocker by race progress
    int opp0 = ctx.opp_start_idx, opp1 = ctx.opp_start_idx + 1;
    int opp0_lin = g_sim[opp0].laps_completed * n + g_sim[opp0].next_cp_id;
    int opp1_lin = g_sim[opp1].laps_completed * n + g_sim[opp1].next_cp_id;
    double opp0_d = g_sim[opp0].pos.Distance(cps[g_sim[opp0].next_cp_id]);
    double opp1_d = g_sim[opp1].pos.Distance(cps[g_sim[opp1].next_cp_id]);
    int opp_runner, opp_blocker;
    if (opp0_lin > opp1_lin || (opp0_lin == opp1_lin && opp0_d < opp1_d)) {
        opp_runner = opp0; opp_blocker = opp1;
    } else {
        opp_runner = opp1; opp_blocker = opp0;
    }

    // --- TURN 0: Apply candidate actions ---
    // Our runner
    int r_thr = my_act.runner_move.thrust;
    if (my_act.runner_shield_step == 0 && g_sim[runner_pod].shield_cd == 0) r_thr = -1;
    g_sim[runner_pod].ApplyGAAction(my_act.runner_move.angle, r_thr);

    // Our blocker
    int b_thr = my_act.blocker_move.thrust;
    if (my_act.blocker_shield_step == 0 && g_sim[blocker_pod].shield_cd == 0) b_thr = -1;
    g_sim[blocker_pod].ApplyGAAction(my_act.blocker_move.angle, b_thr);

    // Opponent runner
    int opp_r_thr = opp_act.runner_move.thrust;
    if (opp_act.runner_shield_step == 0 && g_sim[opp_runner].shield_cd == 0) opp_r_thr = -1;
    g_sim[opp_runner].ApplyGAAction(opp_act.runner_move.angle, opp_r_thr);

    // Opponent blocker
    int opp_b_thr = opp_act.blocker_move.thrust;
    if (opp_act.blocker_shield_step == 0 && g_sim[opp_blocker].shield_cd == 0) opp_b_thr = -1;
    g_sim[opp_blocker].ApplyGAAction(opp_act.blocker_move.angle, opp_b_thr);

    FastSimulateTurn(g_sim);

    // Check CP crossing for Turn 0
    for (int i = 0; i < 4; ++i) {
        double dx = g_sim[i].pos.x - cps[g_sim[i].next_cp_id].x;
        double dy = g_sim[i].pos.y - cps[g_sim[i].next_cp_id].y;
        if (dx * dx + dy * dy <= 360000.0) {
            g_sim[i].next_cp_id++;
            if (g_sim[i].next_cp_id >= n) {
                g_sim[i].next_cp_id = 0;
                g_sim[i].laps_completed++;
            }
        }
    }

    // --- TURNS 1 to 3: Simple heuristic rollouts ---
    for (int t = 1; t <= 3; ++t) {
        // Our runner races
        {
            const Vec2& tgt = ep[g_sim[runner_pod].next_cp_id];
            Action a = MakeGoToTarget(g_sim[runner_pod], tgt.x, tgt.y, 200);
            g_sim[runner_pod].ApplyGAAction(a.angle, a.thrust);
        }
        // Our blocker blocks/rams
        {
            double tx = g_sim[opp_runner].pos.x + g_sim[opp_runner].vel.x;
            double ty = g_sim[opp_runner].pos.y + g_sim[opp_runner].vel.y;
            Action a = MakeGoToTarget(g_sim[blocker_pod], tx, ty, 200);
            g_sim[blocker_pod].ApplyGAAction(a.angle, a.thrust);
        }
        // Opponent runner races
        {
            const Vec2& tgt = ep[g_sim[opp_runner].next_cp_id];
            Action a = MakeGoToTarget(g_sim[opp_runner], tgt.x, tgt.y, 200);
            g_sim[opp_runner].ApplyGAAction(a.angle, a.thrust);
        }
        // Opponent blocker rams
        {
            double tx = g_sim[runner_pod].pos.x + g_sim[runner_pod].vel.x;
            double ty = g_sim[runner_pod].pos.y + g_sim[runner_pod].vel.y;
            Action a = MakeGoToTarget(g_sim[opp_blocker], tx, ty, 200);
            g_sim[opp_blocker].ApplyGAAction(a.angle, a.thrust);
        }

        FastSimulateTurn(g_sim);

        for (int i = 0; i < 4; ++i) {
            double dx = g_sim[i].pos.x - cps[g_sim[i].next_cp_id].x;
            double dy = g_sim[i].pos.y - cps[g_sim[i].next_cp_id].y;
            if (dx * dx + dy * dy <= 360000.0) {
                g_sim[i].next_cp_id++;
                if (g_sim[i].next_cp_id >= n) {
                    g_sim[i].next_cp_id = 0;
                    g_sim[i].laps_completed++;
                }
            }
        }
    }

    // Evaluate final state
    double score = 0.0;
    
    // Runner score
    int r_lin = g_sim[runner_pod].laps_completed * n + g_sim[runner_pod].next_cp_id;
    score -= dte[r_lin] + g_sim[runner_pod].pos.Distance(ep[g_sim[runner_pod].next_cp_id]);

    // Opponent runner score
    int opp_lin = g_sim[opp_runner].laps_completed * n + g_sim[opp_runner].next_cp_id;
    score += dte[opp_lin] + g_sim[opp_runner].pos.Distance(ep[g_sim[opp_runner].next_cp_id]);

    // Blocker intercept reward
    double blocker_opp_dist = g_sim[blocker_pod].pos.Distance(g_sim[opp_runner].pos);
    if (blocker_opp_dist < 1200.0) {
        score += 2000.0 * (1200.0 - blocker_opp_dist) / 1200.0;
    }

    if (g_friendly_collision) {
        score -= 5000.0;
    }

    return score;
}

// ======== SIMULATE + EVALUATE (combined GA: both pods GA-controlled) ========
static double SimulateAndEvaluate(const Solution& sol, const vector<Pod>& base_pods,
                                   const SimCtx& ctx, int horizon) {
    const int n = ctx.cp_count;
    const int runner_pod = ctx.start_idx + ctx.runner_idx;
    const int blocker_pod = ctx.start_idx + (1 - ctx.runner_idx);
    const auto& cps = *ctx.cps;
    const auto& dte = *ctx.dist_to_end;
    const auto& ep = *ctx.entry_points;
    const BotConfig& config = *ctx.config;

    // Reuse thread-local buffer via zero-overhead memcpy
    std::memcpy(g_sim, base_pods.data(), 4 * sizeof(Pod));
    g_friendly_collision = false;

    int init_cp = g_sim[runner_pod].next_cp_id;
    int init_lap = g_sim[runner_pod].laps_completed;
    int init_blocker_cp = g_sim[blocker_pod].next_cp_id;
    int init_blocker_lap = g_sim[blocker_pod].laps_completed;

    // Identify opponent runner/blocker by race progress
    int opp0 = ctx.opp_start_idx, opp1 = ctx.opp_start_idx + 1;
    int opp0_lin = g_sim[opp0].laps_completed * n + g_sim[opp0].next_cp_id;
    int opp1_lin = g_sim[opp1].laps_completed * n + g_sim[opp1].next_cp_id;
    double opp0_d = g_sim[opp0].pos.Distance(cps[g_sim[opp0].next_cp_id]);
    double opp1_d = g_sim[opp1].pos.Distance(cps[g_sim[opp1].next_cp_id]);
    int opp_runner, opp_blocker;
    if (opp0_lin > opp1_lin || (opp0_lin == opp1_lin && opp0_d < opp1_d)) {
        opp_runner = opp0; opp_blocker = opp1;
    } else {
        opp_runner = opp1; opp_blocker = opp0;
    }

    // Track CP activation times
    double runner_activation = (double)horizon + 0.3;
    double opp_activation = (double)horizon + 0.3;
    int init_opp_cp = g_sim[opp_runner].next_cp_id;
    int init_opp_lap = g_sim[opp_runner].laps_completed;

    // State buffers for Turn 1 evaluation
    Pod g_sim_turn_1[4];
    double runner_activation_turn_1 = (double)horizon + 0.3;
    double opp_activation_turn_1 = (double)horizon + 0.3;

    for (int t = 0; t < horizon; ++t) {
        Pod runner_start = g_sim[runner_pod];
        Pod opp_runner_start = g_sim[opp_runner];
        
        // Our runner: GA-controlled with shield
        int r_thr = sol.runner_moves[t].thrust;
        if (t == 0 && ctx.force_boost && g_sim[runner_pod].boost_available) {
            r_thr = 650;
        } else if (t == sol.runner_shield_step && sol.runner_shield_step < 3 && g_sim[runner_pod].shield_cd == 0) {
            r_thr = -1;
        }
        g_sim[runner_pod].ApplyGAAction(sol.runner_moves[t].angle, r_thr);

        // Our blocker: GA-controlled with shield
        int b_thr = sol.blocker_moves[t].thrust;
        if (t == sol.blocker_shield_step && sol.blocker_shield_step < 3 && g_sim[blocker_pod].shield_cd == 0)
            b_thr = -1;
        g_sim[blocker_pod].ApplyGAAction(sol.blocker_moves[t].angle, b_thr);

        // Opponent runner: apply pre-evolved optimal GA moves!
        g_sim[opp_runner].ApplyGAAction(ctx.opp_moves[t].angle, ctx.opp_moves[t].thrust);

        // Opponent blocker: check for override on t == 0, else chase our runner with velocity lead + shield
        if (t == 0 && ctx.has_opp_blocker_override) {
            g_sim[opp_blocker].ApplyGAAction(ctx.opp_blocker_override.angle, ctx.opp_blocker_override.thrust);
        } else {
            double tx = g_sim[runner_pod].pos.x + g_sim[runner_pod].vel.x;
            double ty = g_sim[runner_pod].pos.y + g_sim[runner_pod].vel.y;
            double desired = GameEngine::RadToDeg(atan2(ty - g_sim[opp_blocker].pos.y,
                                                         tx - g_sim[opp_blocker].pos.x));
            double shift = GameEngine::ShortestAngleDiff(g_sim[opp_blocker].angle, desired);
            shift = max(-18.0, min(18.0, shift));
            int opp_thr = 200;
            double opp_dist = g_sim[opp_blocker].pos.Distance(g_sim[runner_pod].pos);
            if (opp_dist < config.shield_ram_dist && g_sim[opp_blocker].shield_cd == 0) opp_thr = -1;
            g_sim[opp_blocker].ApplyGAAction(shift, opp_thr);
        }

        FastSimulateTurn(g_sim);

        // Fully unrolled CP check sweep (exact arena logic matching)
        {
            // Pod 0
            double dx0 = g_sim[0].pos.x - cps[g_sim[0].next_cp_id].x;
            double dy0 = g_sim[0].pos.y - cps[g_sim[0].next_cp_id].y;
            if (dx0 * dx0 + dy0 * dy0 <= 360000.0) {
                g_sim[0].next_cp_id++;
                if (g_sim[0].next_cp_id >= n) {
                    g_sim[0].next_cp_id = 0;
                    g_sim[0].laps_completed++;
                }
                if (0 == runner_pod && runner_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(runner_start, cps[runner_start.next_cp_id]);
                    runner_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
                if (0 == opp_runner && opp_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(opp_runner_start, cps[opp_runner_start.next_cp_id]);
                    opp_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
            }
            // Pod 1
            double dx1 = g_sim[1].pos.x - cps[g_sim[1].next_cp_id].x;
            double dy1 = g_sim[1].pos.y - cps[g_sim[1].next_cp_id].y;
            if (dx1 * dx1 + dy1 * dy1 <= 360000.0) {
                g_sim[1].next_cp_id++;
                if (g_sim[1].next_cp_id >= n) {
                    g_sim[1].next_cp_id = 0;
                    g_sim[1].laps_completed++;
                }
                if (1 == runner_pod && runner_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(runner_start, cps[runner_start.next_cp_id]);
                    runner_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
                if (1 == opp_runner && opp_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(opp_runner_start, cps[opp_runner_start.next_cp_id]);
                    opp_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
            }
            // Pod 2
            double dx2 = g_sim[2].pos.x - cps[g_sim[2].next_cp_id].x;
            double dy2 = g_sim[2].pos.y - cps[g_sim[2].next_cp_id].y;
            if (dx2 * dx2 + dy2 * dy2 <= 360000.0) {
                g_sim[2].next_cp_id++;
                if (g_sim[2].next_cp_id >= n) {
                    g_sim[2].next_cp_id = 0;
                    g_sim[2].laps_completed++;
                }
                if (2 == runner_pod && runner_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(runner_start, cps[runner_start.next_cp_id]);
                    runner_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
                if (2 == opp_runner && opp_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(opp_runner_start, cps[opp_runner_start.next_cp_id]);
                    opp_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
            }
            // Pod 3
            double dx3 = g_sim[3].pos.x - cps[g_sim[3].next_cp_id].x;
            double dy3 = g_sim[3].pos.y - cps[g_sim[3].next_cp_id].y;
            if (dx3 * dx3 + dy3 * dy3 <= 360000.0) {
                g_sim[3].next_cp_id++;
                if (g_sim[3].next_cp_id >= n) {
                    g_sim[3].next_cp_id = 0;
                    g_sim[3].laps_completed++;
                }
                if (3 == runner_pod && runner_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(runner_start, cps[runner_start.next_cp_id]);
                    runner_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
                if (3 == opp_runner && opp_activation > horizon) {
                    double t_hit = GetCPIntersectionTime(opp_runner_start, cps[opp_runner_start.next_cp_id]);
                    opp_activation = (double)t + (t_hit >= 0.0 ? t_hit : 0.0);
                }
            }
        }

        // Save state after Turn 1
        if (t == 0) {
            std::memcpy(g_sim_turn_1, g_sim, 4 * sizeof(Pod));
            runner_activation_turn_1 = runner_activation;
            opp_activation_turn_1 = opp_activation;
        }
    }

    // ===== LAMBDA STATE EVALUATION =====
    auto evaluate_state = [&](const Pod* state, double r_act, double o_act) -> double {
        const Pod& runner = state[runner_pod];
        const Pod& opp_run = state[opp_runner];

        double score = 0;

        // Boundary / CP-cross / runner kinematics — pure helpers (ga_pure.h)
        score += ga_pure::BoundsPenalty(runner.pos.x, runner.pos.y);
        score += ga_pure::BoundsPenalty(state[blocker_pod].pos.x, state[blocker_pod].pos.y);

        // Win/loss (huge bonus)
        if (runner.laps_completed >= ctx.laps) score += 1e9;
        if (opp_run.laps_completed >= ctx.laps) score -= 1e9;

        // CP crossing step bonus: reward crossing CPs during simulation! (fixes CP missing/orbiting)
        int initial_linear = init_lap * n + init_cp;
        int current_linear = runner.laps_completed * n + runner.next_cp_id;
        int cps_crossed = current_linear - initial_linear;
        score += ga_pure::CpCrossedBonus(cps_crossed);

        // Runner: minimize remaining race distance (dominant term)
        if (runner.laps_completed < ctx.laps) {
            int runner_lin = runner.laps_completed * n + runner.next_cp_id;
            double runner_remain = dte[runner_lin] + runner.pos.Distance(ep[runner.next_cp_id]);
            score -= ga_pure::DistWeightTerm(runner_remain, config.dist_weight);
        }

        // Runner: velocity toward entry point of next CP, speed/lateral/angle terms
        score += ga_pure::RunnerKinematicsScore(
            runner.pos.x, runner.pos.y, runner.vel.x, runner.vel.y, runner.angle,
            ep[runner.next_cp_id].x, ep[runner.next_cp_id].y,
            config.align_weight, config.lateral_penalty,
            config.angle_penalty, config.speed_bonus);

        // Fast activation bonus (reward crossing CP quickly)
        score -= ga_pure::FastActPenalty(r_act, RUNNER_FAST_ACT_PENALTY);

        // Bypass opponent rammer angle (reward runner being at an angle from opp blocker)
        {
            double ox = state[opp_blocker].pos.x - runner.pos.x;
            double oy = state[opp_blocker].pos.y - runner.pos.y;
            double cx = cps[runner.next_cp_id].x - runner.pos.x;
            double cy = cps[runner.next_cp_id].y - runner.pos.y;
            
            double cross_val = ox * cy - oy * cx;
            double dot_val = ox * cx + oy * cy;
            double angle_diff = atan2(fabs(cross_val), dot_val);
            score += config.runner_bypass_weight * angle_diff;
        }

        // Opponent delay bonus (penalize opponent progress)
        double opp_delay_score = OPPONENT_DELAY_WEIGHT * config.opp_penalty * o_act;

        // If opponent didn't cross a CP during simulation, bonus for them being far from it
        if (opp_run.next_cp_id == init_opp_cp && opp_run.laps_completed == init_opp_lap) {
            opp_delay_score += OPPONENT_DISTANCE_WEIGHT * config.opp_penalty * opp_run.pos.Distance(cps[opp_run.next_cp_id]);
        }
        score += opp_delay_score;

        // Blocker evaluation (depends on timeout risk)
        double blocker_score = 0;
        {
            const Pod& blocker = state[blocker_pod];

            if (ctx.risk_timeout) {
                // Timeout risk: blocker races to its own CPs (matches legacy riskTimeout behavior)
                if (blocker.laps_completed < ctx.laps) {
                    int blocker_lin = blocker.laps_completed * n + blocker.next_cp_id;
                    blocker_score -= (dte[blocker_lin] + blocker.pos.Distance(ep[blocker.next_cp_id])) * config.dist_weight; // Weighted by dist_weight
                }
            } else {
                // Normal blocking: camp near rest point!
                bool enforce_camp = !(init_blocker_lap == 0 && init_blocker_cp <= 1);
                
                if (enforce_camp) {
                    // Near ram rest point with distance threshold (legacy: acceptable dist = 300)
                    const Vec2& rrp = (*ctx.ram_rest_points)[ctx.ram_beacon];
                    double bd = blocker.pos.Distance(rrp);
                    double d_adj = bd - BLOCKER_REST_THRESHOLD;
                    if (d_adj < 0) d_adj *= BLOCKER_REST_SOFT_FACTOR;  // Soft penalty when already close
                    blocker_score -= BLOCKER_REST_PENALTY_FACTOR * d_adj;

                    // Facing opponent runner (optimized using dot product + LUT to avoid trig/atan2)
                    double dx = opp_run.pos.x - blocker.pos.x;
                    double dy = opp_run.pos.y - blocker.pos.y;
                    double d_sq = dx * dx + dy * dy;
                    if (d_sq > 0) {
                        int ang = (int)round(blocker.angle);
                        ang = (ang % 360 + 360) % 360;
                        double fx = cos_lut[ang];
                        double fy = sin_lut[ang];
                        double dot = (fx * dx + fy * dy) / sqrt(d_sq);
                        blocker_score -= config.blocker_facing_weight * (1.0 - dot) * 1.57;
                    }

                    // Stay in front of opponent (between opponent and their target CP)
                    // Optimized using dot product to avoid atan2
                    double bx = blocker.pos.x - opp_run.pos.x;
                    double by = blocker.pos.y - opp_run.pos.y;
                    double cx = (*ctx.cps)[opp_run.next_cp_id].x - opp_run.pos.x;
                    double cy = (*ctx.cps)[opp_run.next_cp_id].y - opp_run.pos.y;
                    double d1_sq = bx * bx + by * by;
                    double d2_sq = cx * cx + cy * cy;
                    if (d1_sq > 0 && d2_sq > 0) {
                        double dot = (bx * cx + by * cy) / sqrt(d1_sq * d2_sq);
                        blocker_score -= config.blocker_stay_in_front_weight * (1.0 - dot) * 1.57;
                    }
                } else {
                    // Start of the race: blocker is free to seek runner and ram-boost it!
                    double dist_to_runner = blocker.pos.Distance(runner.pos);
                    blocker_score -= dist_to_runner * config.dist_weight * 0.1; // seek runner!
                }
            }
        }
        score += blocker_score * config.block_weight; // Weighted by block_weight

        return score;
    };

    // ===== MULTI-STAGE COMBINATION =====
    double score_turn_1 = evaluate_state(g_sim_turn_1, runner_activation_turn_1, opp_activation_turn_1);
    double score_turn_N = evaluate_state(g_sim, runner_activation, opp_activation);
    
    double final_score = 0.10 * score_turn_1 + 0.90 * score_turn_N;

    // Shield cost/thrust bonus (match legacy coefficients)
    // Applied once to final score since they only depend on Turn 0 action
    if (sol.runner_shield_step == 0) final_score -= RUNNER_SHIELD_COST;
    else final_score += RUNNER_THRUST_BONUS * max(0, sol.runner_moves[0].thrust);
    if (sol.blocker_shield_step == 0) final_score -= BLOCKER_SHIELD_COST;
    else final_score += BLOCKER_THRUST_BONUS * max(0, sol.blocker_moves[0].thrust);

    if (g_friendly_collision) {
        final_score -= 10000.0; // Huge friendly fire penalty!
    }
    return final_score;
}

// ======== STEADY-STATE GA (combined, both pods) ========
static Solution RunGA(const vector<Pod>& base_pods, Timer& timer, double time_limit_ms,
                       const SimCtx& ctx, const BotConfig& config,
                       int thread_id, int num_threads,
                       MigrationChannel* shared_migrants,
                       std::atomic<bool>* stop_flag,
                       int* out_iters,
                       const Solution* warm_start,
                       const Solution* intra_warm_start = nullptr) {
    int pop_size = min(config.population, (int)MAX_POP);
    int horizon = min(config.horizon, (int)MAX_HORIZON);
    int runner_pod = ctx.start_idx + ctx.runner_idx;
    int blocker_pod = ctx.start_idx + (1 - ctx.runner_idx);
    const auto& cps = *ctx.cps;
    int n = ctx.cp_count;

    Solution pop[MAX_POP];
    double scores[MAX_POP];
    int idx = 0;

    // === Seed 0: warm start (shift by 1 turn) ===
    if (warm_start) {
        for (int t = 0; t < horizon - 1; ++t) {
            pop[0].runner_moves[t] = warm_start->runner_moves[t + 1];
            pop[0].blocker_moves[t] = warm_start->blocker_moves[t + 1];
        }
        pop[0].runner_moves[horizon - 1].Randomize();
        pop[0].blocker_moves[horizon - 1].Randomize();
        pop[0].runner_shield_step = (warm_start->runner_shield_step > 0) ? warm_start->runner_shield_step - 1 : MAX_HORIZON;
        pop[0].blocker_shield_step = (warm_start->blocker_shield_step > 0) ? warm_start->blocker_shield_step - 1 : MAX_HORIZON;
        idx = 1;
    } else if (intra_warm_start) {
        pop[0] = *intra_warm_start;
        idx = 1;
    }

    // Extra warm-start diversity: mutated copies of seed 0 for faster convergence
    if (idx > 0 && idx + 14 < pop_size) {
        pop[idx] = pop[0];
        for (int t = 0; t < horizon; ++t) {
            pop[idx].runner_moves[t].SmallMutate();
            pop[idx].blocker_moves[t].SmallMutate();
        }
        idx++;
        pop[idx] = pop[0];
        pop[idx].runner_shield_step = (pop[0].runner_shield_step < 3) ? MAX_HORIZON : 0;
        pop[idx].blocker_shield_step = (pop[0].blocker_shield_step < 3) ? MAX_HORIZON : 1;
        idx++;
    }

    // === Seed heuristic moves for runner + blocker ===
    {
        // Runner: forward-simulate go-to-entry-point
        Pod rsim = base_pods[runner_pod];
        Action runner_h[MAX_HORIZON];
        Vec2 rsim_pos[MAX_HORIZON]; // Added
        for (int t = 0; t < horizon; ++t) {
            const Vec2& tgt = (*ctx.entry_points)[rsim.next_cp_id];
            runner_h[t] = MakeGoToTarget(rsim, tgt.x, tgt.y, 200);
            rsim.ApplyGAAction(runner_h[t].angle, runner_h[t].thrust);
            rsim.pos.x += rsim.vel.x; rsim.pos.y += rsim.vel.y;
            rsim.vel.x = ga_pure::FreeFlightFrictionVel(rsim.vel.x, kCgFriction); rsim.vel.y = ga_pure::FreeFlightFrictionVel(rsim.vel.y, kCgFriction);
            rsim.pos.x = round(rsim.pos.x); rsim.pos.y = round(rsim.pos.y);
            rsim_pos[t] = rsim.pos; // Save predicted position
            if (rsim.pos.DistanceSq(cps[rsim.next_cp_id]) <= 360000) {
                rsim.next_cp_id++;
                if (rsim.next_cp_id >= n) rsim.next_cp_id = 0;
            }
        }

        // Blocker: identify opponent runner, aim at their predicted path
        int opp0 = ctx.opp_start_idx, opp1 = ctx.opp_start_idx + 1;
        int o0lin = base_pods[opp0].laps_completed * n + base_pods[opp0].next_cp_id;
        int o1lin = base_pods[opp1].laps_completed * n + base_pods[opp1].next_cp_id;
        int opp_r = (o0lin >= o1lin) ? opp0 : opp1;
        Pod bsim = base_pods[blocker_pod];
        Action blocker_h[MAX_HORIZON];
        for (int t = 0; t < horizon; ++t) {
            // Aim toward opponent runner's next CP with velocity lead
            double tx = base_pods[opp_r].pos.x + base_pods[opp_r].vel.x * (t + 2);
            double ty = base_pods[opp_r].pos.y + base_pods[opp_r].vel.y * (t + 2);
            blocker_h[t] = MakeGoToTarget(bsim, tx, ty, 200);
            bsim.ApplyGAAction(blocker_h[t].angle, blocker_h[t].thrust);
            bsim.pos.x += bsim.vel.x; bsim.pos.y += bsim.vel.y;
            bsim.vel.x = ga_pure::FreeFlightFrictionVel(bsim.vel.x, kCgFriction); bsim.vel.y = ga_pure::FreeFlightFrictionVel(bsim.vel.y, kCgFriction);
            bsim.pos.x = round(bsim.pos.x); bsim.pos.y = round(bsim.pos.y);
        }

        // Helper: fills a seed with variations on runner and blocker heuristics
        auto fill_seed = [&](int i, int r_thr_mult, double r_angle_mult, int r_shld, int b_shld) {
            if (i >= pop_size) return;
            for (int t = 0; t < horizon; ++t) {
                pop[i].runner_moves[t] = runner_h[t];
                if (r_thr_mult != 200) pop[i].runner_moves[t].thrust = r_thr_mult;
                if (r_angle_mult != 1.0) {
                    pop[i].runner_moves[t].angle *= r_angle_mult;
                    pop[i].runner_moves[t].angle = max(-18.0, min(18.0, pop[i].runner_moves[t].angle));
                }
                pop[i].blocker_moves[t] = blocker_h[t];
            }
            pop[i].runner_shield_step = r_shld;
            pop[i].blocker_shield_step = b_shld;
        };

        // Static seed injector
        auto inject_static = [&](int i, double r_ang, int r_thr, double b_ang, int b_thr) {
            if (i >= pop_size) return;
            for (int t = 0; t < horizon; ++t) {
                pop[i].runner_moves[t].angle = r_ang;
                pop[i].runner_moves[t].thrust = r_thr;
                pop[i].blocker_moves[t].angle = b_ang;
                pop[i].blocker_moves[t].thrust = b_thr;
            }
            pop[i].runner_shield_step = MAX_HORIZON;
            pop[i].blocker_shield_step = MAX_HORIZON;
        };

        // Thread-Specific Specialized Seed Injection
        int active_spec = (num_threads > 1) ? thread_id : 0;

        if (active_spec == 0) {
            // Generalist: balanced seed list
            fill_seed(idx,     200, 1.0,  MAX_HORIZON, MAX_HORIZON); // Base
            fill_seed(idx + 1, 150, 1.0,  MAX_HORIZON, MAX_HORIZON); // Lower thrust
            fill_seed(idx + 2, 200, 0.5,  MAX_HORIZON, MAX_HORIZON); // Halved angles
            fill_seed(idx + 3, 200, 1.3,  MAX_HORIZON, MAX_HORIZON); // Wider angles
            fill_seed(idx + 4, 200, 1.0,  MAX_HORIZON, 0);           // Blocker shield t=0
            fill_seed(idx + 5, 200, 1.0,  0,           MAX_HORIZON); // Runner shield t=0
            idx += 6;
        } 
        else if (active_spec == 1) {
            // Racing Clone Specialist (Legacy Clone Heuristic)
            fill_seed(idx,     200, 1.0,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 1, 200, 0.9,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 2, 200, 1.1,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 3, 200, 1.0,  0,           MAX_HORIZON); // Early runner shield
            idx += 4;
        } 
        else if (active_spec == 2) {
            // Ramming Interceptor Specialist (Lead velocity intercepts)
            for (int k = 0; k < 4 && idx < pop_size; ++k) {
                Pod bsim_lead = base_pods[blocker_pod];
                for (int t = 0; t < horizon; ++t) {
                    pop[idx].runner_moves[t] = runner_h[t];
                    // Lead offset varies from t+1 to t+4
                    double tx = base_pods[opp_r].pos.x + base_pods[opp_r].vel.x * (t + 1 + k);
                    double ty = base_pods[opp_r].pos.y + base_pods[opp_r].vel.y * (t + 1 + k);
                    pop[idx].blocker_moves[t] = MakeGoToTarget(bsim_lead, tx, ty, 200);
                    bsim_lead.ApplyGAAction(pop[idx].blocker_moves[t].angle, 200);
                    bsim_lead.pos.x += bsim_lead.vel.x; bsim_lead.pos.y += bsim_lead.vel.y;
                    bsim_lead.vel.x = ga_pure::FreeFlightFrictionVel(bsim_lead.vel.x, kCgFriction); bsim_lead.vel.y = ga_pure::FreeFlightFrictionVel(bsim_lead.vel.y, kCgFriction);
                    bsim_lead.pos.x = round(bsim_lead.pos.x); bsim_lead.pos.y = round(bsim_lead.pos.y);
                }
                pop[idx].runner_shield_step = MAX_HORIZON;
                pop[idx].blocker_shield_step = MAX_HORIZON;
                idx++;
            }
        } 
        else if (active_spec == 3) {
            // Rest-Point Camper (Goalie Camping)
            const Vec2& rrp = (*ctx.ram_rest_points)[ctx.ram_beacon];
            Pod bsim2 = base_pods[blocker_pod];
            for (int t = 0; t < horizon; ++t) {
                pop[idx].runner_moves[t] = runner_h[t];
                pop[idx].blocker_moves[t] = MakeGoToTarget(bsim2, rrp.x, rrp.y, 200);
                bsim2.ApplyGAAction(pop[idx].blocker_moves[t].angle, 200);
                bsim2.pos.x += bsim2.vel.x; bsim2.pos.y += bsim2.vel.y;
                bsim2.vel.x = ga_pure::FreeFlightFrictionVel(bsim2.vel.x, kCgFriction); bsim2.vel.y = ga_pure::FreeFlightFrictionVel(bsim2.vel.y, kCgFriction);
                bsim2.pos.x = round(bsim2.pos.x); bsim2.pos.y = round(bsim2.pos.y);
            }
            pop[idx].runner_shield_step = MAX_HORIZON;
            pop[idx].blocker_shield_step = MAX_HORIZON;
            idx++;
            
            // Wait at next checkpoint
            const Vec2& cp_wait = cps[base_pods[opp_r].next_cp_id];
            Pod bsim3 = base_pods[blocker_pod];
            for (int t = 0; t < horizon; ++t) {
                pop[idx].runner_moves[t] = runner_h[t];
                pop[idx].blocker_moves[t] = MakeGoToTarget(bsim3, cp_wait.x, cp_wait.y, 200);
                bsim3.ApplyGAAction(pop[idx].blocker_moves[t].angle, 200);
                bsim3.pos.x += bsim3.vel.x; bsim3.pos.y += bsim3.vel.y;
                bsim3.vel.x = ga_pure::FreeFlightFrictionVel(bsim3.vel.x, kCgFriction); bsim3.vel.y = ga_pure::FreeFlightFrictionVel(bsim3.vel.y, kCgFriction);
                bsim3.pos.x = round(bsim3.pos.x); bsim3.pos.y = round(bsim3.pos.y);
            }
            pop[idx].runner_shield_step = MAX_HORIZON;
            pop[idx].blocker_shield_step = MAX_HORIZON;
            idx++;
        } 
        else if (active_spec == 4) {
            // Tight Cornering Specialist (Low thrusts runner)
            fill_seed(idx,     150, 1.0,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 1, 100, 1.0,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 2,  50, 1.0,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 3,   0, 1.0,  MAX_HORIZON, MAX_HORIZON); // Brake / Slide turn
            idx += 4;
        } 
        else if (active_spec == 5) {
            // Slingshot Specialist (Wide approach angles runner)
            fill_seed(idx,     200, 1.2,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 1, 200, 1.4,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 2, 200, 0.6,  MAX_HORIZON, MAX_HORIZON);
            fill_seed(idx + 3, 200, 1.5,  MAX_HORIZON, MAX_HORIZON);
            idx += 4;
        } 
        else if (active_spec == 6) {
            // Thread 6: Monte Carlo Search Specialist
            // Run K = 100 random rollout simulations around our baseline heuristics
            Solution best_mc_sol;
            double best_mc_score = -1e18;
            for (int k = 0; k < 100; ++k) {
                Solution sol;
                for (int t = 0; t < horizon; ++t) {
                    sol.runner_moves[t] = runner_h[t];
                    sol.blocker_moves[t] = blocker_h[t];
                }
                sol.runner_shield_step = MAX_HORIZON;
                sol.blocker_shield_step = MAX_HORIZON;

                // Random walks: mutate Turn 0-2 heavily, and subsequent turns moderately
                for (int t = 0; t < horizon; ++t) {
                    double amp = (t < 3) ? 1.0 : 0.4;
                    sol.runner_moves[t].MutateAggressive(amp);
                    sol.blocker_moves[t].MutateAggressive(amp);
                }

                // Random shields
                if (FastRandInt(0, 4) == 0) sol.runner_shield_step = FastRandInt(0, 2);
                if (FastRandInt(0, 4) == 0) sol.blocker_shield_step = FastRandInt(0, 2);

                double score = SimulateAndEvaluate(sol, base_pods, ctx, horizon);
                if (score > best_mc_score) {
                    best_mc_score = score;
                    best_mc_sol = sol;
                }
            }

            // Inject the best discovered Monte Carlo solution and slightly mutated copies
            if (idx < pop_size) {
                pop[idx] = best_mc_sol;
                idx++;
            }
            for (int j = 0; j < 3 && idx < pop_size; ++j) {
                pop[idx] = best_mc_sol;
                for (int t = 0; t < horizon; ++t) {
                    pop[idx].runner_moves[t].SmallMutate();
                    pop[idx].blocker_moves[t].SmallMutate();
                }
                idx++;
            }
        } 
        else if (active_spec == 7) {
            // Thread 7: Minimax Specialist (Game-Theoretic Adversary)
            // Discretize and compute Turn 0 simultaneous-move minimax optimal strategy
            struct JointAction {
                Action runner;
                Action blocker;
                int r_shld;
                int b_shld;
            };

            JointAction A[8];
            Pod rsim_mm = base_pods[runner_pod];
            Pod bsim_mm = base_pods[blocker_pod];
            
            const Vec2& my_cp_target = (*ctx.entry_points)[rsim_mm.next_cp_id];
            const Vec2& goalie_pt = (*ctx.ram_rest_points)[ctx.ram_beacon];
            
            Action r_racing = MakeGoToTarget(rsim_mm, my_cp_target.x, my_cp_target.y, 200);
            Action b_intercept = MakeGoToTarget(bsim_mm, base_pods[opp_r].pos.x + base_pods[opp_r].vel.x * 2.0, base_pods[opp_r].pos.y + base_pods[opp_r].vel.y * 2.0, 200);
            Action b_camp = MakeGoToTarget(bsim_mm, goalie_pt.x, goalie_pt.y, 200);
            Action b_ram_direct = MakeGoToTarget(bsim_mm, base_pods[opp_r].pos.x, base_pods[opp_r].pos.y, 200);
            Action b_boost = MakeGoToTarget(bsim_mm, rsim_pos[0].x, rsim_pos[0].y, 200);
            
            int opp_blocker_pod = ctx.opp_start_idx + (opp_r == ctx.opp_start_idx ? 1 : 0);
            double opp_b_dir = atan2(base_pods[opp_blocker_pod].pos.y - rsim_mm.pos.y, base_pods[opp_blocker_pod].pos.x - rsim_mm.pos.x);
            double evade_angle = opp_b_dir + 3.14159265358979323846 / 2.0;
            Action r_evade = MakeGoToTarget(rsim_mm, rsim_mm.pos.x + cos(evade_angle)*1000.0, rsim_mm.pos.y + sin(evade_angle)*1000.0, 200);
            
            Action r_slingshot = r_racing;
            r_slingshot.angle *= 1.4;
            r_slingshot.angle = max(-18.0, min(18.0, r_slingshot.angle));

            A[0] = {r_racing, b_intercept, MAX_HORIZON, MAX_HORIZON};
            Action r_brake = r_racing; r_brake.thrust = 0;
            A[1] = {r_brake, b_intercept, MAX_HORIZON, MAX_HORIZON};
            A[2] = {r_racing, b_camp, MAX_HORIZON, MAX_HORIZON};
            A[3] = {r_racing, b_boost, MAX_HORIZON, MAX_HORIZON};
            A[4] = {r_evade, b_intercept, MAX_HORIZON, MAX_HORIZON};
            Action r_shld = r_racing; r_shld.thrust = -1;
            Action b_shld = b_intercept; b_shld.thrust = -1;
            A[5] = {r_shld, b_shld, 0, 0};
            A[6] = {r_slingshot, b_intercept, MAX_HORIZON, MAX_HORIZON};
            A[7] = {r_racing, b_ram_direct, MAX_HORIZON, MAX_HORIZON};

            JointAction B[8];
            Pod opp_rsim = base_pods[opp_r];
            Pod opp_bsim = base_pods[opp_blocker_pod];
            
            const Vec2& opp_cp_target = cps[opp_rsim.next_cp_id];
            Action opp_r_racing = MakeGoToTarget(opp_rsim, opp_cp_target.x, opp_cp_target.y, 200);
            Action opp_r_racing_base = (ctx.opp_moves[0].thrust >= 0) ? ctx.opp_moves[0] : opp_r_racing;
            
            Action opp_b_intercept = MakeGoToTarget(opp_bsim, rsim_mm.pos.x + rsim_mm.vel.x * 2.0, rsim_mm.pos.y + rsim_mm.vel.y * 2.0, 200);
            Action opp_b_camp = MakeGoToTarget(opp_bsim, goalie_pt.x, goalie_pt.y, 200);
            Action opp_b_ram_direct = MakeGoToTarget(opp_bsim, rsim_mm.pos.x, rsim_mm.pos.y, 200);
            
            double my_b_dir = atan2(bsim_mm.pos.y - opp_rsim.pos.y, bsim_mm.pos.x - opp_rsim.pos.x);
            double opp_evade_angle = my_b_dir + 3.14159265358979323846 / 2.0;
            Action opp_r_evade = MakeGoToTarget(opp_rsim, opp_rsim.pos.x + cos(opp_evade_angle)*1000.0, opp_rsim.pos.y + sin(opp_evade_angle)*1000.0, 200);
            
            Action opp_r_slingshot = opp_r_racing_base;
            opp_r_slingshot.angle *= 1.4;
            opp_r_slingshot.angle = max(-18.0, min(18.0, opp_r_slingshot.angle));

            B[0] = {opp_r_racing_base, opp_b_intercept, MAX_HORIZON, MAX_HORIZON};
            Action opp_r_brake = opp_r_racing_base; opp_r_brake.thrust = 0;
            B[1] = {opp_r_brake, opp_b_intercept, MAX_HORIZON, MAX_HORIZON};
            B[2] = {opp_r_racing_base, opp_b_camp, MAX_HORIZON, MAX_HORIZON};
            Action opp_b_boost = MakeGoToTarget(opp_bsim, opp_rsim.pos.x + opp_rsim.vel.x, opp_rsim.pos.y + opp_rsim.vel.y, 200);
            B[3] = {opp_r_racing_base, opp_b_boost, MAX_HORIZON, MAX_HORIZON};
            B[4] = {opp_r_evade, opp_b_intercept, MAX_HORIZON, MAX_HORIZON};
            Action opp_r_shld = opp_r_racing_base; opp_r_shld.thrust = -1;
            Action opp_b_shld = opp_b_intercept; opp_b_shld.thrust = -1;
            B[5] = {opp_r_shld, opp_b_shld, 0, 0};
            B[6] = {opp_r_slingshot, opp_b_intercept, MAX_HORIZON, MAX_HORIZON};
            B[7] = {opp_r_racing_base, opp_b_ram_direct, MAX_HORIZON, MAX_HORIZON};

            double M_payoff[8][8];
            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 8; ++j) {
                    Solution sol;
                    for (int t = 0; t < horizon; ++t) {
                        sol.runner_moves[t] = runner_h[t];
                        sol.blocker_moves[t] = blocker_h[t];
                    }
                    sol.runner_moves[0] = A[i].runner;
                    sol.blocker_moves[0] = A[i].blocker;
                    sol.runner_shield_step = A[i].r_shld;
                    sol.blocker_shield_step = A[i].b_shld;
                    
                    SimCtx local_ctx = ctx;
                    local_ctx.opp_moves[0] = B[j].runner;
                    local_ctx.has_opp_blocker_override = true;
                    local_ctx.opp_blocker_override = B[j].blocker;
                    
                    M_payoff[i][j] = SimulateAndEvaluate(sol, base_pods, local_ctx, horizon);
                }
            }
            
            int best_i = 0;
            double best_minimax_value = -1e18;
            for (int i = 0; i < 8; ++i) {
                double min_val = 1e18;
                for (int j = 0; j < 8; ++j) {
                    if (M_payoff[i][j] < min_val) {
                        min_val = M_payoff[i][j];
                    }
                }
                if (min_val > best_minimax_value) {
                    best_minimax_value = min_val;
                    best_i = i;
                }
            }
            
            Solution best_mm_sol;
            for (int t = 0; t < horizon; ++t) {
                best_mm_sol.runner_moves[t] = runner_h[t];
                best_mm_sol.blocker_moves[t] = blocker_h[t];
            }
            best_mm_sol.runner_moves[0] = A[best_i].runner;
            best_mm_sol.blocker_moves[0] = A[best_i].blocker;
            best_mm_sol.runner_shield_step = A[best_i].r_shld;
            best_mm_sol.blocker_shield_step = A[best_i].b_shld;
            
            if (idx < pop_size) {
                pop[idx] = best_mm_sol;
                idx++;
            }
            for (int j = 0; j < 3 && idx < pop_size; ++j) {
                pop[idx] = best_mm_sol;
                for (int t = 0; t < horizon; ++t) {
                    pop[idx].runner_moves[t].SmallMutate();
                    pop[idx].blocker_moves[t].SmallMutate();
                }
                idx++;
            }
        }

        // Inject Extreme static seeds globally as standard baseline safety variants
        if (idx + 3 < pop_size) {
            inject_static(idx,      0.0, 200,   0.0, 200); // Extreme 1: All Max Thrust
            inject_static(idx + 1,  0.0,   0,   0.0,   0); // Extreme 2: All Zero Thrust (Full Brake)
            inject_static(idx + 2, -18.0, 200, -18.0, 200); // Extreme 3: All Max-Left
            inject_static(idx + 3,  18.0, 200,  18.0, 200); // Extreme 4: All Max-Right
            idx += 4;
        }
    }

    // Fill remaining with random
    for (; idx < pop_size; ++idx)
        pop[idx].Randomize(horizon);

    // === Initial evaluation ===
    int worst_idx = 0, best_idx = 0;
    for (int i = 0; i < pop_size; ++i) {
        scores[i] = SimulateAndEvaluate(pop[i], base_pods, ctx, horizon);
        pop[i].score = scores[i];
        if (scores[i] < scores[worst_idx]) worst_idx = i;
        if (scores[i] > scores[best_idx]) best_idx = i;
    }
    double worst_score = scores[worst_idx];

    // === Steady-state evolution loop ===
    int iterations = 0;
    double amplitude = 1.0;

    // Dynamic clock check frequency to balance timing precision and overhead
    int check_freq = 128;
    if (time_limit_ms < 15.0) check_freq = 64;
    else if (time_limit_ms >= 500.0) check_freq = 512;

    while (true) {
        iterations++;
        
        // Print Thread execution details on first clock check to debug core assignment
        if (GABot::verbose && iterations == check_freq) {
            cerr << "    [Thread-" << thread_id << "] OS_TID=" << std::this_thread::get_id() 
                 << " started concurrent GA search (time_budget=" << time_limit_ms << "ms)" << endl;
        }

        // Centralized Clock & Stop flag polling
        if (iterations % check_freq == 0) {
            if (stop_flag) {
                if (thread_id == 0) {
                    double elapsed = timer.ElapsedMs();
                    if (elapsed >= time_limit_ms) {
                        stop_flag->store(true, std::memory_order_relaxed);
                        break;
                    }
                    amplitude = 1.0 - elapsed / time_limit_ms;
                } else {
                    if (stop_flag->load(std::memory_order_relaxed)) {
                        break;
                    }
                }
            } else {
                double elapsed = timer.ElapsedMs();
                if (elapsed >= time_limit_ms) break;
                amplitude = 1.0 - elapsed / time_limit_ms;
            }
        }

        // Higher frequency poll on worker threads for immediate termination response
        if (stop_flag && thread_id > 0 && iterations % 64 == 0) {
            if (stop_flag->load(std::memory_order_relaxed)) {
                break;
            }
        }

        // Asynchronous Island Ring Migration
        if (shared_migrants && num_threads > 1 && iterations % 128 == 0) {
            // Export my best to my slot
            {
                std::lock_guard<std::mutex> lock(shared_migrants[thread_id].mutex);
                shared_migrants[thread_id].solution = pop[best_idx];
                shared_migrants[thread_id].initialized = true;
            }

            // Import neighbor best
            int neighbor_id = (thread_id + FastRandInt(1, num_threads - 1)) % num_threads;
            Solution migrant;
            bool got_migrant = false;
            {
                std::lock_guard<std::mutex> lock(shared_migrants[neighbor_id].mutex);
                if (shared_migrants[neighbor_id].initialized) {
                    migrant = shared_migrants[neighbor_id].solution;
                    got_migrant = true;
                }
            }

            if (got_migrant) {
                pop[worst_idx] = migrant;
                scores[worst_idx] = migrant.score;
                if (scores[worst_idx] > scores[best_idx]) best_idx = worst_idx;
                
                // Recompute worst index
                worst_idx = 0; worst_score = scores[0];
                for (int i = 1; i < pop_size; ++i) {
                    if (scores[i] < worst_score) { worst_idx = i; worst_score = scores[i]; }
                }
            }
        }

        // Stagnation detection
        if (scores[best_idx] < worst_score + 0.3) {
            for (int i = 0; i < pop_size; ++i) {
                if (i != best_idx) scores[i] -= 2000.0;
            }
            worst_score -= 2000.0;
        }

        // Tournament selection
        int p1 = FastRandInt(0, pop_size - 1);
        int p2 = FastRandInt(0, pop_size - 1);
        int parent1 = (scores[p1] >= scores[p2]) ? p1 : p2;

        if (FastRandInt(0, 4) == 0) {
            p1 = FastRandInt(0, pop_size - 1);
            p2 = FastRandInt(0, pop_size - 1);
            int parent2 = (scores[p1] >= scores[p2]) ? p1 : p2;
            pop[worst_idx].CrossoverFromTwo(pop[parent1], pop[parent2], horizon);
        } else {
            pop[worst_idx].MutateFromOne(pop[parent1], horizon, amplitude);
        }
        pop[worst_idx].runner_moves[FastRandInt(0, horizon - 1)].SmallMutate();
        pop[worst_idx].blocker_moves[FastRandInt(0, horizon - 1)].SmallMutate();

        // Evaluate child
        double child_score = SimulateAndEvaluate(pop[worst_idx], base_pods, ctx, horizon);

        if (child_score > scores[best_idx]) best_idx = worst_idx;
        scores[worst_idx] = child_score;
        pop[worst_idx].score = child_score;

        if (child_score > worst_score) {
            worst_idx = 0; worst_score = scores[0];
            for (int i = 1; i < pop_size; ++i) {
                if (scores[i] < worst_score) { worst_idx = i; worst_score = scores[i]; }
            }
        }
    }

    if (out_iters) *out_iters = iterations;

    if (GABot::verbose && config.num_threads <= 1 && thread_id == 0) {
        cerr << "  GA iters=" << iterations << " best=" << scores[best_idx] << endl;
    }
    return pop[best_idx];
}

static Solution RunGAParallel(const vector<Pod>& base_pods, Timer& timer, double time_limit_ms,
                              const SimCtx& ctx, const BotConfig& config,
                              GABotThreadPool* pool,
                              const Solution* warm_start,
                              const Solution* intra_warm_start = nullptr) {
    if (config.num_threads <= 1 || !pool) {
        return RunGA(base_pods, timer, time_limit_ms, ctx, config, 0, 1, nullptr, nullptr, nullptr, warm_start, intra_warm_start);
    }

    // Dynamic safety buffer:
    // - Large budgets (first turn >= 500ms): use a 100ms safety buffer to guarantee zero timeout under heavy core load.
    // - Standard budgets (75ms): use an 8.0ms safety buffer to safeguard against scheduling spikes.
    // - Tight budgets (< 15ms): use a 1.5ms safety buffer for maximal speed optimization.
    double safety_buffer = 1.5;
    if (time_limit_ms >= 500.0) {
        safety_buffer = 100.0;
    } else if (time_limit_ms >= 50.0) {
        safety_buffer = 8.0;
    }
    double adjusted_limit = time_limit_ms - safety_buffer;
    if (adjusted_limit < 2.0) adjusted_limit = 2.0;

    int num_threads = config.num_threads;
    std::vector<MigrationChannel> shared_migrants(num_threads);
    std::atomic<bool> stop_flag{false};

    std::vector<Solution> thread_results(num_threads);
    std::vector<int> thread_results_iters(num_threads, 0);
    uint32_t base_seed = FastRand();

    std::vector<std::function<void()>> tasks(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        tasks[i] = [i, num_threads, &base_pods, &timer, adjusted_limit, &ctx, &config,
                    &shared_migrants, &stop_flag, &thread_results, &thread_results_iters, base_seed, warm_start, intra_warm_start]() {
            SeedRand(base_seed + i * 1337 + 1);
            int iters = 0;
            thread_results[i] = RunGA(base_pods, timer, adjusted_limit, ctx, config,
                                      i, num_threads, shared_migrants.data(), &stop_flag,
                                      &iters, warm_start, intra_warm_start);
            thread_results_iters[i] = iters;
        };
    }

    pool->RunParallel(tasks);

    Solution best_sol;
    double best_score = -1e18;
    for (int i = 0; i < num_threads; ++i) {
        if (thread_results[i].score > best_score) {
            best_score = thread_results[i].score;
            best_sol = thread_results[i];
        }
    }

    if (GABot::verbose) {
        cerr << "  Parallel GA (" << num_threads << " threads) best=" << best_score << " total_iters=";
        int total = 0;
        for (int i = 0; i < num_threads; ++i) total += thread_results_iters[i];
        cerr << total << " [" << thread_results_iters[0];
        for (int i = 1; i < num_threads; ++i) cerr << "," << thread_results_iters[i];
        cerr << "]" << endl;
    }

    return best_sol;
}

// ======== BOT IMPLEMENTATION ========
bool GABot::verbose = false;
void SetGABotVerbose(bool verbose) { GABot::verbose = verbose; }
GABot::GABot(BotConfig config) : config_(config) {
    if (config_.num_threads > 1) {
        thread_pool_ = std::make_shared<GABotThreadPool>(config_.num_threads);
    }
}
string GABot::GetName() const { return config_.name; }

void GABot::Initialize(int laps, int cp_count, const vector<Vec2>& cps, int team_id) {
    laps_ = laps;
    cp_count_ = cp_count;
    cps_ = cps;
    team_id_ = team_id;
    has_prev_best_ = false;
    total_cps_in_race_ = laps * cp_count;

    // Check if map has any sharp turns (hairpins)
    bool has_sharp_turn = false;
    for (int i = 0; i < cp_count; ++i) {
        int prev = (i + cp_count - 1) % cp_count;
        int next = (i + 1) % cp_count;
        Vec2 v1 = cps[i].Sub(cps[prev]);
        Vec2 v2 = cps[next].Sub(cps[i]);
        double len1 = sqrt(v1.x * v1.x + v1.y * v1.y);
        double len2 = sqrt(v2.x * v2.x + v2.y * v2.y);
        if (len1 > 0 && len2 > 0) {
            double dot = v1.x * v2.x + v1.y * v2.y;
            double cos_theta = dot / (len1 * len2);
            if (cos_theta < 0.2) { // Sharp turn (>= 78 degrees)
                has_sharp_turn = true;
                break;
            }
        }
    }

    double total_dist = 0;
    for (int i = 0; i < cp_count; ++i) {
        total_dist += cps[i].Distance(cps[(i + 1) % cp_count]);
    }
    avg_dist_ = total_dist / cp_count;

    // Self-Adaptive Config Selection based on map complexity, turn sharpness, and average distance
    if (config_.name == "DefaultGA") {
        bool use_handling = (cp_count >= 5 || has_sharp_turn) && (avg_dist_ <= 6500.0);
        if (use_handling) {
            // Winding/Complex/Sharp Map: Use Bot_6 (High Handling, Deep Corner Cut)
            config_.name = "Bot_6_Handling";
            config_.horizon = 4;
            config_.population = 80;
            config_.dist_weight = 2.9;
            config_.align_weight = 3.7;
            config_.speed_bonus = 0.2;
            config_.lateral_penalty = 1.0; // Increased from 0.0 to prevent slipping on hairpins
            config_.angle_penalty = 60.0;
            config_.corner_cut_dist = 600.0;
            config_.block_weight = 4.3;
            config_.shield_penalty = 4.0;
            config_.shield_ram_dist = 600.0;
            config_.opp_penalty = 1.3;
            config_.opp_model_ms = 0.0;
            config_.runner_bypass_weight = 25.0;
            config_.blocker_stay_in_front_weight = 35.0;
            config_.blocker_facing_weight = 35.0;
            config_.runner_evasion_weight = 1.5;
        } else {
            // Straight/Simple Map: Use Bot_1 (Optimized Speed/Blocking)
            config_.name = "Bot_1_SpeedBlock";
            config_.horizon = 4;
            config_.population = 80;
            config_.dist_weight = 2.3;
            config_.align_weight = 1.7;
            config_.speed_bonus = 0.6;
            config_.lateral_penalty = 1.2;
            config_.angle_penalty = 43.0;
            config_.corner_cut_dist = 300.0;
            config_.block_weight = 6.8; // Reduced from 8.4 to prevent runner blocking
            config_.shield_penalty = 93.0;
            config_.shield_ram_dist = 1000.0;
            config_.opp_penalty = 1.2;    // Reduced from 2.7 to focus more on racing
            config_.opp_model_ms = 0.0;
            config_.runner_bypass_weight = 15.0;
            config_.blocker_stay_in_front_weight = 25.0;
            config_.blocker_facing_weight = 25.0;
            config_.runner_evasion_weight = 1.0;
        }
    }

    // CP distances
    cp_distances_.resize(cp_count);
    for (int i = 0; i < cp_count; i++) {
        int next = (i + 1) % cp_count;
        cp_distances_[i] = cps[i].Distance(cps[next]);
    }

    // Entry points using turn angle to scale corner cutting dynamically
    entry_points_.resize(cp_count);
    for (int i = 0; i < cp_count; ++i) {
        int prev = (i + cp_count - 1) % cp_count;
        int next = (i + 1) % cp_count;
        
        // Calculate vectors
        Vec2 v1 = cps[i].Sub(cps[prev]);
        Vec2 v2 = cps[next].Sub(cps[i]);
        double len1 = sqrt(v1.x * v1.x + v1.y * v1.y);
        double len2 = sqrt(v2.x * v2.x + v2.y * v2.y);
        
        double cos_theta = 1.0; // default if straight or error
        if (len1 > 0 && len2 > 0) {
            double dot = v1.x * v2.x + v1.y * v2.y;
            cos_theta = dot / (len1 * len2);
            cos_theta = std::max(-1.0, std::min(1.0, cos_theta)); // clamp
        }
        
        // Scale shift by turn sharpness: (1 - cos_theta) / 2
        double shift_dist = config_.corner_cut_dist * (1.0 - cos_theta) / 2.0;
        
        double dx = cps[prev].x - cps[next].x;
        double dy = cps[prev].y - cps[next].y;
        double d = sqrt(dx * dx + dy * dy);
        if (d > 0) {
            entry_points_[i] = {cps[i].x + shift_dist * dx / d, cps[i].y + shift_dist * dy / d};
        } else {
            entry_points_[i] = cps[i];
        }
    }

    // Ram rest points: 1000 units from CP in the "concavity" direction
    // (toward the midpoint of neighboring CPs - matches legacy ramRestPoint)
    ram_rest_points_.resize(cp_count);
    for (int i = 0; i < cp_count; ++i) {
        int prev = (i + cp_count - 1) % cp_count;
        int next = (i + 1) % cp_count;
        double dx = cps[prev].x + cps[next].x - 2.0 * cps[i].x;
        double dy = cps[prev].y + cps[next].y - 2.0 * cps[i].y;
        double d = sqrt(dx * dx + dy * dy);
        if (d > 0) {
            ram_rest_points_[i] = {cps[i].x + 1000.0 * dx / d, cps[i].y + 1000.0 * dy / d};
        } else {
            ram_rest_points_[i] = cps[i];
        }
    }

    // Distance-to-end lookup: dist_to_end[linear_idx] = total remaining CP-to-CP distance
    // linear_idx = laps_completed * cp_count + next_cp_id
    dist_to_end_.resize(total_cps_in_race_ + 1, 0.0);
    dist_to_end_[total_cps_in_race_ - 1] = 0.0; // Final stretch remaining CP-to-CP distance is 0
    for (int i = total_cps_in_race_ - 2; i >= 0; --i) {
        int cp = i % cp_count;
        int next_cp = (cp + 1) % cp_count;
        dist_to_end_[i] = dist_to_end_[i + 1] + cps[cp].Distance(cps[next_cp]);
    }
}

static Solution GenerateHeuristicOpponentModel(const vector<Pod>& base_pods, const SimCtx& ctx, int horizon) {
    Solution sol;
    Pod opp_sim[4];
    std::memcpy(opp_sim, base_pods.data(), 4 * sizeof(Pod));

    int opp_run_idx = ctx.opp_start_idx;
    int opp_block_idx = ctx.opp_start_idx + 1;
    const auto& cps = *ctx.cps;
    
    int opp0_lin = opp_sim[opp_run_idx].laps_completed * ctx.cp_count + opp_sim[opp_run_idx].next_cp_id;
    int opp1_lin = opp_sim[opp_block_idx].laps_completed * ctx.cp_count + opp_sim[opp_block_idx].next_cp_id;
    double opp0_d = opp_sim[opp_run_idx].pos.Distance(cps[opp_sim[opp_run_idx].next_cp_id]);
    double opp1_d = opp_sim[opp_block_idx].pos.Distance(cps[opp_sim[opp_block_idx].next_cp_id]);
    int opp_runner = opp_run_idx;
    if (opp1_lin > opp0_lin || (opp0_lin == opp1_lin && opp1_d < opp0_d)) {
        opp_runner = opp_block_idx;
    }

    sol.runner_shield_step = MAX_HORIZON;
    sol.blocker_shield_step = MAX_HORIZON;

    for (int t = 0; t < horizon; ++t) {
        Vec2 cp_target = cps[opp_sim[opp_runner].next_cp_id];
        double dx_r = cp_target.x - opp_sim[opp_runner].pos.x;
        double dy_r = cp_target.y - opp_sim[opp_runner].pos.y;
        double target_angle_r = GameEngine::RadToDeg(atan2(dy_r, dx_r));
        double shift_r = GameEngine::ShortestAngleDiff(opp_sim[opp_runner].angle, target_angle_r);
        shift_r = std::max(-18.0, std::min(18.0, shift_r));
        int thrust_r = 200;

        sol.runner_moves[t] = {shift_r, thrust_r};

        opp_sim[opp_runner].ApplyGAAction(shift_r, thrust_r);
        opp_sim[opp_runner].pos.x += opp_sim[opp_runner].vel.x;
        opp_sim[opp_runner].pos.y += opp_sim[opp_runner].vel.y;
        opp_sim[opp_runner].vel.x = ga_pure::FreeFlightFrictionVel(opp_sim[opp_runner].vel.x, kCgFriction);
        opp_sim[opp_runner].vel.y = ga_pure::FreeFlightFrictionVel(opp_sim[opp_runner].vel.y, kCgFriction);
        opp_sim[opp_runner].pos.x = round(opp_sim[opp_runner].pos.x);
        opp_sim[opp_runner].pos.y = round(opp_sim[opp_runner].pos.y);

        if (opp_sim[opp_runner].pos.DistanceSq(cps[opp_sim[opp_runner].next_cp_id]) <= 360000) {
            opp_sim[opp_runner].next_cp_id = (opp_sim[opp_runner].next_cp_id + 1) % ctx.cp_count;
        }
    }
    return sol;
}

vector<PodAction> GABot::GetActions(const vector<Pod>& pods) {
    Timer timer;
    timer.Start();

    int start_idx = team_id_ * 2;
    int opp_start_idx = (1 - team_id_) * 2;
    turn_count_++;

    // Dynamic role assignment using distToEnd for accurate comparison
    auto race_remaining = [&](int idx) {
        int lin = pods[idx].laps_completed * cp_count_ + pods[idx].next_cp_id;
        return dist_to_end_[lin] + pods[idx].pos.Distance(entry_points_[pods[idx].next_cp_id]);
    };
    double r0 = race_remaining(start_idx);
    double r1 = race_remaining(start_idx + 1);
    if (r1 < r0 - 200.0) {
        runner_idx_ = 1; blocker_idx_ = 0;
    } else {
        runner_idx_ = 0; blocker_idx_ = 1;
    }

    // Compute ram_beacon: which CP the blocker should camp near
    // (matches legacy bot's myRamBeacon logic)
    int opp0 = opp_start_idx, opp1 = opp_start_idx + 1;
    int n = cp_count_;
    int o0lin = pods[opp0].laps_completed * n + pods[opp0].next_cp_id;
    int o1lin = pods[opp1].laps_completed * n + pods[opp1].next_cp_id;
    double o0d = pods[opp0].pos.Distance(cps_[pods[opp0].next_cp_id]);
    double o1d = pods[opp1].pos.Distance(cps_[pods[opp1].next_cp_id]);
    int opp_runner_pod = (o0lin > o1lin || (o0lin == o1lin && o0d < o1d)) ? opp0 : opp1;
    int blocker_pod_idx = start_idx + blocker_idx_;

    int ram_beacon = pods[opp_runner_pod].next_cp_id;
    {
        double opp_dist = pods[opp_runner_pod].pos.Distance(cps_[pods[opp_runner_pod].next_cp_id]);
        double my_dist = pods[blocker_pod_idx].pos.Distance(cps_[pods[opp_runner_pod].next_cp_id]);
        if (my_dist > opp_dist + 2200) {
            int next_beacon = (ram_beacon + 1) % n;
            double opp_dist2 = opp_dist + cps_[ram_beacon].Distance(cps_[next_beacon]);
            double my_dist2 = pods[blocker_pod_idx].pos.Distance(cps_[next_beacon]);
            if (my_dist2 < opp_dist2 - 2200)
                ram_beacon = next_beacon;
            else
                ram_beacon = (next_beacon + 1) % n;
        }
    }

       // === STAGE 0: BASELINE OPPONENT PREDICTION (10ms) ===
    const Pod& opp_runner = pods[opp_runner_pod];
    bool opp_force_boost = false;
    if (opp_runner.boost_available) {
        double dist = opp_runner.pos.Distance(entry_points_[opp_runner.next_cp_id]);
        double target_angle = GameEngine::RadToDeg(std::atan2(entry_points_[opp_runner.next_cp_id].y - opp_runner.pos.y,
                                                             entry_points_[opp_runner.next_cp_id].x - opp_runner.pos.x));
        double diff = std::abs(GameEngine::ShortestAngleDiff(opp_runner.angle, target_angle));
        if (dist > 5000.0 && diff < 5.0) {
            opp_force_boost = true;
        }
    }

    SimCtx opp_ctx;
    opp_ctx.cps = &cps_;
    opp_ctx.dist_to_end = &dist_to_end_;
    opp_ctx.entry_points = &entry_points_;
    opp_ctx.ram_rest_points = &ram_rest_points_;
    opp_ctx.cp_count = cp_count_;
    opp_ctx.laps = laps_;
    opp_ctx.start_idx = opp_start_idx;
    opp_ctx.opp_start_idx = start_idx;
    opp_ctx.runner_idx = opp_runner_pod - opp_start_idx;
    opp_ctx.ram_beacon = pods[start_idx + runner_idx_].next_cp_id;
    opp_ctx.risk_timeout = false;
    opp_ctx.force_boost = opp_force_boost;
    
    BotConfig opp_config = config_;
    opp_config.block_weight = 0.0;   // pure racing!
    opp_config.opp_penalty = 0.0;
    opp_config.population = 32;      // compact pool for 10ms speed
    opp_config.horizon = 4;
    opp_config.num_threads = 1;      // Force 1 thread for 10ms prediction search to avoid thread overhead
    opp_ctx.config = &opp_config;

    // Initially, assume our runner's moves are just basic go-to-target moves (baseline)
    // to give the opponent runner a basic obstacle to avoid
    {
        Pod rsim = pods[start_idx + runner_idx_];
        for (int t = 0; t < MAX_HORIZON; ++t) {
            const Vec2& tgt = entry_points_[rsim.next_cp_id];
            opp_ctx.opp_moves[t] = MakeGoToTarget(rsim, tgt.x, tgt.y, 200);
            rsim.ApplyGAAction(opp_ctx.opp_moves[t].angle, opp_ctx.opp_moves[t].thrust);
            rsim.pos.x += rsim.vel.x; rsim.pos.y += rsim.vel.y;
            rsim.vel.x = ga_pure::FreeFlightFrictionVel(rsim.vel.x, kCgFriction); rsim.vel.y = ga_pure::FreeFlightFrictionVel(rsim.vel.y, kCgFriction);
            rsim.pos.x = round(rsim.pos.x); rsim.pos.y = round(rsim.pos.y);
            if (rsim.pos.DistanceSq(cps_[rsim.next_cp_id]) <= 360000) {
                rsim.next_cp_id++;
                if (rsim.next_cp_id >= cp_count_) rsim.next_cp_id = 0;
            }
        }
    }

    // === TIME BUDGET ALLOCATION ===
    double total_budget = (turn_count_ == 1) ? config_.first_turn_time_limit_ms : config_.turn_time_limit_ms;
    bool tight_budget = (total_budget < 15.0);  // Sub-15ms: collapse to 2-stage IBR

    // Auto-scale GA parameters for tight time budgets
    if (tight_budget) {
        config_.population = 16;
        config_.horizon = 4;
        opp_config.population = 12;
        opp_config.horizon = 3;
    }

    double t0_limit, t1_limit, t2_limit, time_limit;
    if (turn_count_ == 1) {
        t0_limit = total_budget * 0.10;
        t1_limit = total_budget * 0.40;
        t2_limit = total_budget * 0.50;
        time_limit = total_budget;
    } else if (tight_budget) {
        // 2-stage fallback: baseline opp (25%) → final response (100%)
        t0_limit = total_budget * 0.25;
        t1_limit = total_budget * 0.50;  // unused
        t2_limit = total_budget * 0.50;  // unused
        time_limit = total_budget;
    } else {
        // Full 4-stage IBR
        t0_limit = total_budget * 0.13;
        t1_limit = total_budget * 0.46;
        t2_limit = total_budget * 0.60;
        time_limit = total_budget;
    }

    // === INITIALIZE OUR SIMCTX EARLY ===
    bool force_boost = false;
    int runner_pod_idx = start_idx + runner_idx_;
    const Pod& runner_pod = pods[runner_pod_idx];
    if (runner_pod.boost_available) {
        double dist = runner_pod.pos.Distance(entry_points_[runner_pod.next_cp_id]);
        double target_angle = GameEngine::RadToDeg(std::atan2(entry_points_[runner_pod.next_cp_id].y - runner_pod.pos.y,
                                                              entry_points_[runner_pod.next_cp_id].x - runner_pod.pos.x));
        double diff = std::abs(GameEngine::ShortestAngleDiff(runner_pod.angle, target_angle));
        if (dist > 5000.0 && diff < 5.0) {
            force_boost = true;
        }
    }

    SimCtx ctx;
    ctx.cps = &cps_;
    ctx.dist_to_end = &dist_to_end_;
    ctx.entry_points = &entry_points_;
    ctx.ram_rest_points = &ram_rest_points_;
    ctx.cp_count = cp_count_;
    ctx.laps = laps_;
    ctx.start_idx = start_idx;
    ctx.opp_start_idx = opp_start_idx;
    ctx.runner_idx = runner_idx_;
    ctx.ram_beacon = ram_beacon;
    ctx.risk_timeout = pods[start_idx + blocker_idx_].timeout >= 60;
    ctx.force_boost = force_boost;
    ctx.config = &config_;

    // Dynamic Horizon Scaling (skipped under tight budget — already set above)
    if (!tight_budget) {
        if (turn_count_ == 1) {
            config_.horizon = 8;
        } else {
            double opp_dist = pods[start_idx + blocker_idx_].pos.Distance(opp_runner.pos);
            if (opp_dist < 3000.0) {
                config_.horizon = 8;
            } else {
                config_.horizon = 6;
            }
        }
    }

    Solution opp_sol;
    if (tight_budget) {
        // INSTANT HEURISTIC OPPONENT PREDICTION
        opp_sol = GenerateHeuristicOpponentModel(pods, ctx, config_.horizon);
        for (int t = 0; t < MAX_HORIZON; ++t) {
            ctx.opp_moves[t] = opp_sol.runner_moves[t];
        }
    } else {
        opp_sol = RunGAParallel(pods, timer, t0_limit, opp_ctx, opp_config, thread_pool_.get(), nullptr);
        for (int t = 0; t < MAX_HORIZON; ++t) {
            ctx.opp_moves[t] = opp_sol.runner_moves[t];
        }
    }

    Solution baseline_sol;
    if (!tight_budget) {
        // === STAGE 1: BASELINE OUR RESPONSE ===
        baseline_sol = RunGAParallel(pods, timer, t1_limit, ctx, config_, thread_pool_.get(), has_prev_best_ ? &prev_best_ : nullptr);

        // === STAGE 2: ADAPTIVE OPPONENT PREDICTION ===
        Solution adaptive_opp_sol;
        for (int t = 0; t < MAX_HORIZON; ++t) {
            opp_ctx.opp_moves[t] = baseline_sol.runner_moves[t];
        }
        adaptive_opp_sol = RunGAParallel(pods, timer, t2_limit, opp_ctx, opp_config, thread_pool_.get(), nullptr, &opp_sol);
        
        for (int t = 0; t < MAX_HORIZON; ++t) {
            ctx.opp_moves[t] = adaptive_opp_sol.runner_moves[t];
        }
    }

    // === STAGE 3: FINAL OUR RESPONSE ===
    Solution best = RunGAParallel(pods, timer, time_limit, ctx, config_, thread_pool_.get(),
                                  has_prev_best_ ? &prev_best_ : nullptr,
                                  tight_budget ? nullptr : &baseline_sol);

    prev_best_ = best;
    has_prev_best_ = true;

    // === HYBRID REGRET MATCHING SEARCH (Tactical correction) ===
    std::vector<TeamAction> my_actions;
    
    // 1. GA (the strategic solution we found)
    my_actions.push_back({best.runner_moves[0], best.blocker_moves[0], best.runner_shield_step, best.blocker_shield_step, "GA"});
    
    // 2. Race & Block
    {
        Pod rsim = pods[start_idx + runner_idx_];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 200);
        
        Pod bsim = pods[start_idx + blocker_idx_];
        const Vec2& bp_tgt = ram_rest_points_[ram_beacon];
        Action b_act = MakeGoToTarget(bsim, bp_tgt.x, bp_tgt.y, 200);
        
        my_actions.push_back({r_act, b_act, MAX_HORIZON, MAX_HORIZON, "Race_Block"});
    }
    
    // 3. Race & Ram
    {
        Pod rsim = pods[start_idx + runner_idx_];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 200);
        
        Pod bsim = pods[start_idx + blocker_idx_];
        double tx = opp_runner.pos.x + opp_runner.vel.x * 2.0;
        double ty = opp_runner.pos.y + opp_runner.vel.y * 2.0;
        Action b_act = MakeGoToTarget(bsim, tx, ty, 200);
        
        my_actions.push_back({r_act, b_act, MAX_HORIZON, MAX_HORIZON, "Race_Ram"});
    }
    
    // 4. Race & Shield
    {
        Pod rsim = pods[start_idx + runner_idx_];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 200);
        
        Pod bsim = pods[start_idx + blocker_idx_];
        double tx = opp_runner.pos.x + opp_runner.vel.x;
        double ty = opp_runner.pos.y + opp_runner.vel.y;
        Action b_act = MakeGoToTarget(bsim, tx, ty, 0);
        
        my_actions.push_back({r_act, b_act, MAX_HORIZON, 0, "Race_Shield"});
    }
    
    // 5. Shield & Block
    {
        Pod rsim = pods[start_idx + runner_idx_];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 0);
        
        Pod bsim = pods[start_idx + blocker_idx_];
        const Vec2& bp_tgt = ram_rest_points_[ram_beacon];
        Action b_act = MakeGoToTarget(bsim, bp_tgt.x, bp_tgt.y, 200);
        
        my_actions.push_back({r_act, b_act, 0, MAX_HORIZON, "Shield_Block"});
    }
    
    // 6. Brake & Block
    {
        Pod rsim = pods[start_idx + runner_idx_];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 0);
        
        Pod bsim = pods[start_idx + blocker_idx_];
        const Vec2& bp_tgt = ram_rest_points_[ram_beacon];
        Action b_act = MakeGoToTarget(bsim, bp_tgt.x, bp_tgt.y, 200);
        
        my_actions.push_back({r_act, b_act, MAX_HORIZON, MAX_HORIZON, "Brake_Block"});
    }

    std::vector<TeamAction> opp_actions;
    
    // 1. Opponent GA (Stage 0 prediction)
    opp_actions.push_back({opp_sol.runner_moves[0], opp_sol.blocker_moves[0], opp_sol.runner_shield_step, opp_sol.blocker_shield_step, "OppGA"});
    
    // 2. Race & Block
    {
        Pod rsim = pods[opp_runner_pod];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 200);
        
        Pod bsim = pods[opp_start_idx + (1 - (opp_runner_pod - opp_start_idx))];
        const Vec2& bp_tgt = ram_rest_points_[pods[start_idx + runner_idx_].next_cp_id]; // camp our runner's next CP
        Action b_act = MakeGoToTarget(bsim, bp_tgt.x, bp_tgt.y, 200);
        
        opp_actions.push_back({r_act, b_act, MAX_HORIZON, MAX_HORIZON, "Race_Block"});
    }
    
    // 3. Race & Ram
    {
        Pod rsim = pods[opp_runner_pod];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 200);
        
        Pod bsim = pods[opp_start_idx + (1 - (opp_runner_pod - opp_start_idx))];
        double tx = pods[start_idx + runner_idx_].pos.x + pods[start_idx + runner_idx_].vel.x * 2.0;
        double ty = pods[start_idx + runner_idx_].pos.y + pods[start_idx + runner_idx_].vel.y * 2.0;
        Action b_act = MakeGoToTarget(bsim, tx, ty, 200);
        
        opp_actions.push_back({r_act, b_act, MAX_HORIZON, MAX_HORIZON, "Race_Ram"});
    }
    
    // 4. Race & Shield
    {
        Pod rsim = pods[opp_runner_pod];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 200);
        
        Pod bsim = pods[opp_start_idx + (1 - (opp_runner_pod - opp_start_idx))];
        double tx = pods[start_idx + runner_idx_].pos.x + pods[start_idx + runner_idx_].vel.x;
        double ty = pods[start_idx + runner_idx_].pos.y + pods[start_idx + runner_idx_].vel.y;
        Action b_act = MakeGoToTarget(bsim, tx, ty, 0);
        
        opp_actions.push_back({r_act, b_act, MAX_HORIZON, 0, "Race_Shield"});
    }
    
    // 5. Shield & Block
    {
        Pod rsim = pods[opp_runner_pod];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 0);
        
        Pod bsim = pods[opp_start_idx + (1 - (opp_runner_pod - opp_start_idx))];
        const Vec2& bp_tgt = ram_rest_points_[pods[start_idx + runner_idx_].next_cp_id];
        Action b_act = MakeGoToTarget(bsim, bp_tgt.x, bp_tgt.y, 200);
        
        opp_actions.push_back({r_act, b_act, 0, MAX_HORIZON, "Shield_Block"});
    }
    
    // 6. Brake & Block
    {
        Pod rsim = pods[opp_runner_pod];
        const Vec2& ep_tgt = entry_points_[rsim.next_cp_id];
        Action r_act = MakeGoToTarget(rsim, ep_tgt.x, ep_tgt.y, 0);
        
        Pod bsim = pods[opp_start_idx + (1 - (opp_runner_pod - opp_start_idx))];
        const Vec2& bp_tgt = ram_rest_points_[pods[start_idx + runner_idx_].next_cp_id];
        Action b_act = MakeGoToTarget(bsim, bp_tgt.x, bp_tgt.y, 200);
        
        opp_actions.push_back({r_act, b_act, MAX_HORIZON, MAX_HORIZON, "Brake_Block"});
    }

    int K = 6;
    std::vector<std::vector<double>> M(K, std::vector<double>(K, 0.0));
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) {
            M[i][j] = EvaluateTacticalCell(my_actions[i], opp_actions[j], pods, ctx);
        }
    }

    std::vector<double> R1(K, 0.0), R2(K, 0.0);
    std::vector<double> avg_sigma1(K, 0.0), avg_sigma2(K, 0.0);
    
    int T = 1000;
    for (int step = 0; step < T; ++step) {
        std::vector<double> P1(K, 0.0), P2(K, 0.0);
        double sum1 = 0.0, sum2 = 0.0;
        for (int i = 0; i < K; ++i) {
            if (R1[i] > 0.0) { P1[i] = R1[i]; sum1 += R1[i]; }
            if (R2[i] > 0.0) { P2[i] = R2[i]; sum2 += R2[i]; }
        }
        
        if (sum1 > 0.0) {
            for (int i = 0; i < K; ++i) P1[i] /= sum1;
        } else {
            for (int i = 0; i < K; ++i) P1[i] = 1.0 / K;
        }
        
        if (sum2 > 0.0) {
            for (int i = 0; i < K; ++i) P2[i] /= sum2;
        } else {
            for (int i = 0; i < K; ++i) P2[i] = 1.0 / K;
        }
        
        for (int i = 0; i < K; ++i) {
            avg_sigma1[i] += P1[i];
            avg_sigma2[i] += P2[i];
        }
        
        std::vector<double> v1(K, 0.0), v2(K, 0.0);
        double ev1 = 0.0, ev2 = 0.0;
        for (int i = 0; i < K; ++i) {
            for (int j = 0; j < K; ++j) {
                v1[i] += M[i][j] * P2[j];
                v2[j] -= M[i][j] * P1[i];
            }
        }
        
        for (int i = 0; i < K; ++i) {
            ev1 += v1[i] * P1[i];
            ev2 += v2[i] * P2[i];
        }
        
        for (int i = 0; i < K; ++i) {
            R1[i] += v1[i] - ev1;
            R2[i] += v2[i] - ev2;
        }
    }
    
    int best_act_idx = 0;
    double max_weight = -1.0;
    for (int i = 0; i < K; ++i) {
        if (avg_sigma1[i] > max_weight) {
            max_weight = avg_sigma1[i];
            best_act_idx = i;
        }
    }
    
    TeamAction chosen = my_actions[best_act_idx];

    // Convert chosen solution to PodActions
    vector<PodAction> actions(2);

    auto make_output = [&](const Action& a, int pod_idx, int shield_step) -> PodAction {
        const Pod& pod = pods[pod_idx];
        int out_thrust = max(0, min(200, a.thrust));
        if (pod_idx == start_idx + runner_idx_ && force_boost && pod.boost_available) {
            out_thrust = 650;
        } else if (shield_step == 0 && pod.shield_cd == 0) {
            out_thrust = -1;
        }
        double shift = max(-18.0, min(18.0, a.angle));
        double final_angle = GameEngine::NormalizeAngle(pod.angle + shift);
        double rad = final_angle * PI / 180.0;
        double tx = pod.pos.x + cos(rad) * 10000.0;
        double ty = pod.pos.y + sin(rad) * 10000.0;
        return {tx, ty, out_thrust};
    };

    actions[runner_idx_] = make_output(chosen.runner_move, start_idx + runner_idx_, chosen.runner_shield_step);
    actions[blocker_idx_] = make_output(chosen.blocker_move, start_idx + blocker_idx_, chosen.blocker_shield_step);

    if (verbose) {
        cerr << "[T" << turn_count_ << "] " << fixed << setprecision(1)
             << timer.ElapsedMs() << "ms R" << runner_idx_
             << " Chosen: " << chosen.name << " w=" << max_weight / T
             << " S:" << setprecision(0) << best.score << endl;
    }
    return actions;
}

// ======== HEURISTIC BLOCKER (For backup) ========
struct HeuristicBlocker {
    static PodAction GetAction(const Pod& blocker, const vector<Pod>& pods, const vector<Vec2>& cps, int opp_start_idx) {
        const Pod& opp0 = pods[opp_start_idx];
        const Pod& opp1 = pods[opp_start_idx + 1];
        const Pod& opp_runner = (opp0.next_cp_id >= opp1.next_cp_id) ? opp0 : opp1;
        const Pod& target_opp = (opp0.next_cp_id == opp1.next_cp_id) ?
            (opp0.pos.DistanceSq(cps[opp0.next_cp_id]) < opp1.pos.DistanceSq(cps[opp1.next_cp_id]) ? opp0 : opp1)
            : opp_runner;
        
        double dist_to_opp = blocker.pos.Distance(target_opp.pos);
        
        Vec2 intercept;
        if (dist_to_opp < 1500) {
            intercept = target_opp.pos;
            intercept.x += target_opp.vel.x * 0.5;
            intercept.y += target_opp.vel.y * 0.5;
        } else if (dist_to_opp < 4000) {
            intercept.x = target_opp.pos.x + target_opp.vel.x * 2.0;
            intercept.y = target_opp.pos.y + target_opp.vel.y * 2.0;
        } else {
            Vec2 cp_target = cps[target_opp.next_cp_id];
            double our_dist_to_cp = blocker.pos.Distance(cp_target);
            double opp_dist_to_cp = target_opp.pos.Distance(cp_target);
            if (our_dist_to_cp > opp_dist_to_cp + 1000) {
                int next_cp = (target_opp.next_cp_id + 1) % cps.size();
                cp_target = cps[next_cp];
            }
            intercept = cp_target;
        }
        
        int thrust = 200;
        
        if (dist_to_opp < 900 && blocker.shield_cd == 0) {
            double rel_vx = blocker.vel.x - target_opp.vel.x;
            double rel_vy = blocker.vel.y - target_opp.vel.y;
            double dx = target_opp.pos.x - blocker.pos.x;
            double dy = target_opp.pos.y - blocker.pos.y;
            double closing = (rel_vx * dx + rel_vy * dy);
            if (closing > 0 && dist_to_opp < 850) {
                return {intercept.x, intercept.y, -1};
            }
        }
        
        if (dist_to_opp > 5000) {
            double our_dist = blocker.pos.Distance(intercept);
            if (our_dist < 1500) {
                double speed = std::sqrt(blocker.vel.x*blocker.vel.x + blocker.vel.y*blocker.vel.y);
                if (speed > 200) thrust = 0;
                else thrust = 100;
            }
        }
        
        return {intercept.x, intercept.y, thrust};
    }
};

struct OutputAction {
    int tx, ty;
    int thrust;
    bool was_boost;
    bool was_shield;
};

string GetTrollMessage(int pod_id, int runner_idx, int blocker_idx, const vector<Pod>& env, const OutputAction& act, int cp_count) {
    int opp_runner = (env[2].laps_completed * cp_count + env[2].next_cp_id > env[3].laps_completed * cp_count + env[3].next_cp_id) ? 2 : 3;
    int opp_blocker = 5 - opp_runner; // 2 or 3

    if (pod_id == runner_idx) {
        if (act.was_boost) return "ZOOM! See ya!";
        if (act.was_shield) return "SHIELD RAM ENGAGED!";
        
        double dist_to_blocker = env[pod_id].pos.Distance(env[opp_blocker].pos);
        if (dist_to_blocker < 1000) {
            return "Can't touch this! 🕺";
        }
        
        double speed = env[pod_id].vel.Distance({0,0});
        if (speed > 350) return "SPEEEED!";
        return "Catch me if you can!";
    } else {
        if (act.was_shield) return "BOOM! Roadblock! 🛑";
        
        double dist_to_opp_run = env[pod_id].pos.Distance(env[opp_runner].pos);
        if (dist_to_opp_run < 1200) {
            Vec2 blocker_to_opp = env[opp_runner].pos.Sub(env[pod_id].pos);
            double dot = blocker_to_opp.x * env[opp_runner].vel.x + blocker_to_opp.y * env[opp_runner].vel.y;
            if (dot > 0) return "Behind you! 🎯";
            return "YOU SHALL NOT PASS!";
        }
        
        return "Camping is a lifestyle 🏕️";
    }
}


// Factory for tournament / library consumers (no include-cpp).
#include <memory>
