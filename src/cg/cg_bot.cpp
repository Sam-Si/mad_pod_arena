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

using namespace std;

#ifdef LOCAL_VERIFY
#define PI LEGACY_PI
#define main legacy_main
#include "../legacy_bot/legacy_bot.cpp"
#undef main
#undef PI
#endif

constexpr double SPEED_FACTOR = 10.0;

extern const double PI;
extern double cos_lut[360];
extern double sin_lut[360];

void InitLUT();
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

class PhysicsSimulator {
public:
    static double GetCollisionTime(const Pod& p1, const Pod& p2);
    static void ResolveCollision(Pod& p1, Pod& p2);
    static void SimulateTurn(std::vector<Pod>& pods);
};

struct BotConfig {
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

const int MAX_HORIZON = 8;
const int MAX_POP = 48;

struct Action {
    double angle;  // Angle shift [-18, 18]
    int thrust;    // [0, 200] or -1 for shield
    void Randomize();
    void MutateAggressive(double amplitude);
    void SmallMutate();
};

struct Solution {
    double score;
    Action runner_moves[MAX_HORIZON];
    Action blocker_moves[MAX_HORIZON];
    int runner_shield_step;
    int blocker_shield_step;
    Solution();
    void Randomize(int horizon);
    void MutateFromOne(const Solution& parent, int horizon, double amplitude);
    void CrossoverFromTwo(const Solution& a, const Solution& b, int horizon);
};

Action MakeGoToTarget(const Pod& pod, double tx, double ty, int thrust_val = 200);

class GABot : public IBot {
    int laps_;
    int cp_count_;
    std::vector<Vec2> cps_;
    int team_id_;
    int runner_idx_ = 0;
    int blocker_idx_ = 1;
    BotConfig config_;
    bool has_prev_best_ = false;
    Solution prev_best_;
    int turn_count_ = 0;
    std::vector<double> cp_distances_;
    std::vector<Vec2> entry_points_;
    std::vector<Vec2> ram_rest_points_;
    std::vector<double> dist_to_end_;
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
        double desired = GameEngine::RadToDeg(atan2(tgt.y - p.pos.y, tgt.x - p.pos.x));
        double shift = GameEngine::ShortestAngleDiff(p.angle, desired);
        shift = max(-18.0, min(18.0, shift));
        p.ApplyGAAction(shift, 200);
    }
};

// ======== ENGINE IMPLEMENTATIONS ========
const double PI = 3.14159265358979323846;
double cos_lut[360];
double sin_lut[360];
thread_local uint32_t xor_state = 2463534242;

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
    return min + (FastRand() % (max - min + 1));
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
double Pod::Mass() const { return (shield_cd > 0) ? 10.0 : 1.0; }

void Pod::ApplyGAAction(double angle_shift, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 3; thrust_val = 0; }
    else if (shield_cd > 0) { shield_cd--; thrust_val = 0; }
    if (thrust_val == 650) boost_available = false;

    if (angle < 0) angle = 0;
    else angle = GameEngine::NormalizeAngle(angle + angle_shift);

    double rad = angle * PI / 180.0;
    vel.x += std::cos(rad) * thrust_val;
    vel.y += std::sin(rad) * thrust_val;
}

void Pod::ApplyServerAction(double tx, double ty, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 3; thrust_val = 0; }
    else if (shield_cd > 0) { shield_cd--; thrust_val = 0; }
    if (thrust_val == 650) { 
        if (boost_available) { thrust_val = 650; boost_available = false; }
        else thrust_val = 200;
    }

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
    pos.x = std::round(pos.x);
    pos.y = std::round(pos.y);
    vel.x = std::trunc(vel.x * 0.85);
    vel.y = std::trunc(vel.y * 0.85);
}

double PhysicsSimulator::GetCollisionTime(const Pod& p1, const Pod& p2) {
    double x = p1.pos.x - p2.pos.x;
    double y = p1.pos.y - p2.pos.y;
    double vx = p1.vel.x - p2.vel.x;
    double vy = p1.vel.y - p2.vel.y;

    double a = vx * vx + vy * vy;
    if (a < 0.00001) return -1.0;

    double b = 2.0 * (x * vx + y * vy);
    double c = x * x + y * y - 640000.0; 

    if (c < 0.0) return 0.0;

    double delta = b * b - 4.0 * a * c;
    if (delta < 0.0) return -1.0;

    double t = (-b - std::sqrt(delta)) / (2.0 * a);
    if (t < 0.0) return -1.0;
    return t;
}

void PhysicsSimulator::ResolveCollision(Pod& p1, Pod& p2) {
    double m1 = p1.Mass();
    double m2 = p2.Mass();
    double mcoeff = (m1 + m2) / (m1 * m2);
    
    double nx = p1.pos.x - p2.pos.x;
    double ny = p1.pos.y - p2.pos.y;
    double nxnysquare = nx * nx + ny * ny;
    
    double dvx = p1.vel.x - p2.vel.x;
    double dvy = p1.vel.y - p2.vel.y;
    
    double product = nx * dvx + ny * dvy;
    double fx = (nx * product) / (nxnysquare * mcoeff);
    double fy = (ny * product) / (nxnysquare * mcoeff);
    
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
    
    double impulse = std::sqrt(fx * fx + fy * fy);
    if (impulse < 120.0) {
        fx = fx * 120.0 / impulse;
        fy = fy * 120.0 / impulse;
    }
    
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
}

void PhysicsSimulator::SimulateTurn(std::vector<Pod>& pods) {
    double t_current = 0.0;
    int col_count = 0;
    while (t_current < 1.0 && col_count < 10) {
        double first_col_t = 2.0;
        Pod* col_p1 = nullptr;
        Pod* col_p2 = nullptr;

        for (size_t i = 0; i < pods.size(); ++i) {
            for (size_t j = i + 1; j < pods.size(); ++j) {
                double t = GetCollisionTime(pods[i], pods[j]);
                if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) {
                    first_col_t = t;
                    col_p1 = &pods[i];
                    col_p2 = &pods[j];
                }
            }
        }

        if (first_col_t > 1.0 - t_current) {
            for (auto& pod : pods) pod.Move(1.0 - t_current);
            t_current = 1.0;
            break;
        }

        if (first_col_t < 0.0001) first_col_t = 0.0001;

        for (auto& pod : pods) pod.Move(first_col_t);
        if (col_p1 && col_p2) ResolveCollision(*col_p1, *col_p2);
        t_current += first_col_t;
        col_count++;
    }
    
    if (t_current < 1.0) {
        for (auto& pod : pods) pod.Move(1.0 - t_current);
    }
    
    for (auto& pod : pods) pod.EndTurn();
}

// ======== BOT IMPLEMENTATION ========
// Thread-local reusable simulation buffer (eliminates heap alloc per Simulate call)
static thread_local vector<Pod> g_sim(4);

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
}

Action MakeGoToTarget(const Pod& pod, double tx, double ty, int thrust_val) {
    double desired = GameEngine::RadToDeg(std::atan2(ty - pod.pos.y, tx - pod.pos.x));
    double shift = GameEngine::ShortestAngleDiff(pod.angle, desired);
    shift = std::max(-18.0, std::min(18.0, shift));
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
    int ram_beacon;  // Which CP the blocker should camp near
};

// ======== SIMULATE + EVALUATE (combined GA: both pods GA-controlled) ========
static double SimulateAndEvaluate(const Solution& sol, const vector<Pod>& base_pods,
                                   const SimCtx& ctx, int horizon) {
    const int n = ctx.cp_count;
    const int runner_pod = ctx.start_idx + ctx.runner_idx;
    const int blocker_pod = ctx.start_idx + (1 - ctx.runner_idx);
    const auto& cps = *ctx.cps;
    const auto& dte = *ctx.dist_to_end;
    const auto& ep = *ctx.entry_points;

    // Reuse thread-local buffer
    g_sim.assign(base_pods.begin(), base_pods.end());

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

    for (int t = 0; t < horizon; ++t) {
        // Our runner: GA-controlled with shield
        int r_thr = sol.runner_moves[t].thrust;
        if (t == sol.runner_shield_step && sol.runner_shield_step < 3 && g_sim[runner_pod].shield_cd == 0)
            r_thr = -1;
        g_sim[runner_pod].ApplyGAAction(sol.runner_moves[t].angle, r_thr);

        // Our blocker: GA-controlled with shield
        int b_thr = sol.blocker_moves[t].thrust;
        if (t == sol.blocker_shield_step && sol.blocker_shield_step < 3 && g_sim[blocker_pod].shield_cd == 0)
            b_thr = -1;
        g_sim[blocker_pod].ApplyGAAction(sol.blocker_moves[t].angle, b_thr);

        // Opponent runner: aim at their next CP (simple proxy)
        {
            const Vec2& tgt = cps[g_sim[opp_runner].next_cp_id];
            double desired = GameEngine::RadToDeg(atan2(tgt.y - g_sim[opp_runner].pos.y,
                                                         tgt.x - g_sim[opp_runner].pos.x));
            double shift = GameEngine::ShortestAngleDiff(g_sim[opp_runner].angle, desired);
            shift = max(-18.0, min(18.0, shift));
            g_sim[opp_runner].ApplyGAAction(shift, 200);
        }

        // Opponent blocker: chase our runner with velocity lead
        {
            double tx = g_sim[runner_pod].pos.x + g_sim[runner_pod].vel.x;
            double ty = g_sim[runner_pod].pos.y + g_sim[runner_pod].vel.y;
            double desired = GameEngine::RadToDeg(atan2(ty - g_sim[opp_blocker].pos.y,
                                                         tx - g_sim[opp_blocker].pos.x));
            double shift = GameEngine::ShortestAngleDiff(g_sim[opp_blocker].angle, desired);
            shift = max(-18.0, min(18.0, shift));
            g_sim[opp_blocker].ApplyGAAction(shift, 200);
        }

        PhysicsSimulator::SimulateTurn(g_sim);

        // Check CPs (match arena logic exactly: increment then wrap)
        for (int p = 0; p < 4; ++p) {
            if (g_sim[p].pos.DistanceSq(cps[g_sim[p].next_cp_id]) <= 360000) {
                g_sim[p].next_cp_id++;
                if (g_sim[p].next_cp_id >= n) {
                    g_sim[p].next_cp_id = 0;
                    g_sim[p].laps_completed++;
                }
                // Track first CP activation
                if (p == runner_pod && runner_activation > horizon)
                    runner_activation = (double)(t + 1);
                if (p == opp_runner && opp_activation > horizon)
                    opp_activation = (double)(t + 1);
            }
        }
    }

    // ===== EVALUATION =====
    const Pod& runner = g_sim[runner_pod];
    const Pod& opp_run = g_sim[opp_runner];

    double score = 0;

    // Win/loss (huge bonus)
    if (runner.laps_completed >= ctx.laps) score += 1e9;
    if (opp_run.laps_completed >= ctx.laps) score -= 1e9;

    // Runner: minimize remaining race distance (dominant term)
    if (runner.laps_completed < ctx.laps) {
        int runner_lin = runner.laps_completed * n + runner.next_cp_id;
        double runner_remain = dte[runner_lin] + runner.pos.Distance(ep[runner.next_cp_id]);
        score -= runner_remain;
    }

    // Runner: velocity toward entry point of next CP
    {
        double dx = ep[runner.next_cp_id].x - runner.pos.x;
        double dy = ep[runner.next_cp_id].y - runner.pos.y;
        double d = sqrt(dx * dx + dy * dy);
        if (d > 0)
            score += (runner.vel.x * dx / d + runner.vel.y * dy / d) * 3.0;
    }

    // Fast activation bonus (reward crossing CP quickly)
    score -= 30.0 * runner_activation;

    // Bypass opponent rammer angle (reward runner being at an angle from opp blocker)
    {
        double ox = g_sim[opp_blocker].pos.x - runner.pos.x;
        double oy = g_sim[opp_blocker].pos.y - runner.pos.y;
        double cx = cps[runner.next_cp_id].x - runner.pos.x;
        double cy = cps[runner.next_cp_id].y - runner.pos.y;
        double cross_val = fabs(ox * cy - oy * cx);
        double dot_val = ox * cx + oy * cy;
        score += 20.0 * atan2(cross_val, dot_val);
    }

    // Opponent delay bonus (reward delaying opponent's CP activation)
    score += 1500.0 * opp_activation;

    // If opponent didn't cross a CP during simulation, bonus for them being far from it
    if (opp_run.next_cp_id == init_opp_cp && opp_run.laps_completed == init_opp_lap) {
        score += 1.5 * opp_run.pos.Distance(cps[opp_run.next_cp_id]);
    }

    // Blocker positioning: reward being near the ram rest point of the targeted beacon
    {
        const Pod& blocker = g_sim[blocker_pod];
        const Vec2& rrp = (*ctx.ram_rest_points)[ctx.ram_beacon];
        double bd = blocker.pos.Distance(rrp);
        score -= 0.045 * bd;

        // Bonus for blocker facing toward opponent runner
        double dx = opp_run.pos.x - blocker.pos.x;
        double dy = opp_run.pos.y - blocker.pos.y;
        double desired = GameEngine::RadToDeg(atan2(dy, dx));
        double face_diff = GameEngine::ShortestAngleDiff(blocker.angle, desired);
        score -= 1.5 * 20.0 * fabs(face_diff) / 180.0;  // Normalized to ~0-30 range
    }

    // Shield cost/thrust bonus (match legacy coefficients)
    if (sol.runner_shield_step == 0) score -= 330.0;
    else score += 0.16 * max(0, sol.runner_moves[0].thrust);
    if (sol.blocker_shield_step == 0) score -= 495.0;
    else score += 0.06 * max(0, sol.blocker_moves[0].thrust);

    return score;
}

// ======== STEADY-STATE GA (combined, both pods) ========
static Solution RunGA(const vector<Pod>& base_pods, Timer& timer, double time_limit_ms,
                       const SimCtx& ctx, const BotConfig& config,
                       const Solution* warm_start) {
    int pop_size = min(config.population, (int)MAX_POP);
    int horizon = min(config.horizon, (int)MAX_HORIZON);

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
    while (timer.ElapsedMs() < time_limit_ms) {
        double amplitude = 1.0 - timer.ElapsedMs() / time_limit_ms;
        iterations++;

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
        // Small mutation on random genes of both pods
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

    if (GABot::verbose) {
        cerr << "  GA iters=" << iterations << " best=" << scores[best_idx] << endl;
    }
    return pop[best_idx];
}

// ======== BOT IMPLEMENTATION ========
bool GABot::verbose = false;
GABot::GABot(BotConfig config) : config_(config) {}
string GABot::GetName() const { return config_.name; }

void GABot::Initialize(int laps, int cp_count, const vector<Vec2>& cps, int team_id) {
    laps_ = laps;
    cp_count_ = cp_count;
    cps_ = cps;
    team_id_ = team_id;
    has_prev_best_ = false;
    total_cps_in_race_ = laps * cp_count;

    // CP distances
    cp_distances_.resize(cp_count);
    for (int i = 0; i < cp_count; i++) {
        int next = (i + 1) % cp_count;
        cp_distances_[i] = cps[i].Distance(cps[next]);
    }

    // Entry points: 300 units from CP center toward (prev - next) midpoint direction
    entry_points_.resize(cp_count);
    for (int i = 0; i < cp_count; ++i) {
        int prev = (i + cp_count - 1) % cp_count;
        int next = (i + 1) % cp_count;
        double dx = cps[prev].x - cps[next].x;
        double dy = cps[prev].y - cps[next].y;
        double d = sqrt(dx * dx + dy * dy);
        if (d > 0) {
            entry_points_[i] = {cps[i].x + 300.0 * dx / d, cps[i].y + 300.0 * dy / d};
        } else {
            entry_points_[i] = cps[i];
        }
    }

    // Ram rest points: 1000 units from CP in the "concavity" direction
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
    dist_to_end_.resize(total_cps_in_race_ + 1, 0.0);
    for (int i = total_cps_in_race_ - 1; i >= 0; --i) {
        int cp = i % cp_count;
        int next_cp = (cp + 1) % cp_count;
        dist_to_end_[i] = dist_to_end_[i + 1] + cps[cp].Distance(cps[next_cp]);
    }
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

    // Set up simulation context
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

    Solution best = RunGA(pods, timer, 75.0 / SPEED_FACTOR, ctx, config_, has_prev_best_ ? &prev_best_ : nullptr);
    prev_best_ = best;
    has_prev_best_ = true;

    // Convert GA solution to PodActions
    vector<PodAction> actions(2);

    auto make_output = [&](const Action& a, int pod_idx, int shield_step) -> PodAction {
        const Pod& pod = pods[pod_idx];
        int out_thrust = max(0, min(200, a.thrust));
        if (shield_step == 0 && pod.shield_cd == 0) out_thrust = -1;
        double shift = max(-18.0, min(18.0, a.angle));
        double final_angle = GameEngine::NormalizeAngle(pod.angle + shift);
        double rad = final_angle * PI / 180.0;
        double tx = pod.pos.x + cos(rad) * 10000.0;
        double ty = pod.pos.y + sin(rad) * 10000.0;
        return {tx, ty, out_thrust};
    };

    actions[runner_idx_] = make_output(best.runner_moves[0], start_idx + runner_idx_, best.runner_shield_step);
    actions[blocker_idx_] = make_output(best.blocker_moves[0], start_idx + blocker_idx_, best.blocker_shield_step);

    if (verbose) {
        cerr << "[T" << turn_count_ << "] " << fixed << setprecision(1)
             << timer.ElapsedMs() << "ms R" << runner_idx_
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

// ======== MAIN GAME LOOP ========
int main() {
    InitLUT();
    
    int laps;
    cin >> laps; cin.ignore();
    int cp_count;
    cin >> cp_count; cin.ignore();
    vector<Vec2> cps(cp_count);
    for (int i = 0; i < cp_count; i++) {
        cin >> cps[i].x >> cps[i].y; cin.ignore();
    }

    cerr << "Laps: " << laps << endl;
    cerr << "Checkpoint Count: " << cp_count << endl;
    for (int i = 0; i < cp_count; i++) {
        cerr << "CP[" << i << "] = ("
             << cps[i].x << ", "
             << cps[i].y << ")" << endl;
    }

    BotConfig config;
    config.name = "CoordBot";
    config.horizon = 6;
    config.population = 48;
    config.dist_weight = 1.5;
    config.align_weight = 3.0;
    config.speed_bonus = 0.5;
    config.lateral_penalty = 0.5;
    config.angle_penalty = 25;
    config.corner_cut_dist = 300;
    config.block_weight = 1.0;
    config.shield_penalty = 50;
    config.shield_ram_dist = 850;
    config.opp_penalty = 0.5;
    config.opp_model_ms = 0;

    GABot bot(config);
    GABot::verbose = true;
    bot.Initialize(laps, cp_count, cps, 0);
    vector<int> pod_laps(4, 0);
    vector<int> prev_cp(4, 1);

    bool has_prediction = false;
    vector<Pod> predicted(4);
    struct OutputAction {
        int tx, ty;
        int thrust;
        bool was_boost;
        bool was_shield;
    };
    vector<OutputAction> last_output(2);
    bool pred_had_opp_collision[2] = {false, false};
    double max_pos_err_clean = 0, max_vel_err_clean = 0;
    int verify_turn = 0;
    int shield_cd_track[4] = {0, 0, 0, 0};
    bool boost_available_0 = true;
    bool boost_available_1 = true;

    while (1) {
        vector<Pod> env(4);
        for (int i = 0; i < 4; i++) {
            int x, y, vx, vy, angle, next_cp_id;
            cin >> x >> y >> vx >> vy >> angle >> next_cp_id; cin.ignore();
            env[i].pos = Vec2(x, y);
            env[i].vel = Vec2(vx, vy);
            env[i].angle = (double)angle;
            env[i].next_cp_id = next_cp_id;
            env[i].shield_cd = shield_cd_track[i];
            if (shield_cd_track[i] > 0) shield_cd_track[i]--;
        }
        env[0].boost_available = boost_available_0;
        env[1].boost_available = boost_available_1;
        
        // Track laps manually so GA role assignment works
        for (int i = 0; i < 4; i++) {
            if (env[i].next_cp_id == 1 && prev_cp[i] == cp_count - 1) pod_laps[i]++;
            prev_cp[i] = env[i].next_cp_id;
            env[i].laps_completed = pod_laps[i];
        }

        if (has_prediction) {
            verify_turn++;
            cerr << "--- PHYSICS VERIFY T" << verify_turn << " ---" << endl;
            bool any_our_bug = false;
            for (int i = 0; i < 2; i++) {
                double pos_dx = env[i].pos.x - predicted[i].pos.x;
                double pos_dy = env[i].pos.y - predicted[i].pos.y;
                double pos_err = std::sqrt(pos_dx*pos_dx + pos_dy*pos_dy);
                double vel_dx = env[i].vel.x - predicted[i].vel.x;
                double vel_dy = env[i].vel.y - predicted[i].vel.y;
                double vel_err = std::sqrt(vel_dx*vel_dx + vel_dy*vel_dy);
                double angle_err = 0;
                if (env[i].angle >= 0 && predicted[i].angle >= 0) {
                    angle_err = std::abs(GameEngine::ShortestAngleDiff((int)env[i].angle, (int)std::round(predicted[i].angle)));
                }

                bool had_opp_col = pred_had_opp_collision[i];
                
                if (pos_err > 0.5 || vel_err > 0.5 || angle_err > 0.5) {
                    if (had_opp_col) {
                        cerr << "  Pod" << i << " COLLISION-EXPECTED: pos_err=" << fixed << setprecision(1) 
                             << pos_err << " vel_err=" << vel_err << endl;
                    } else {
                        any_our_bug = true;
                        cerr << "  *** Pod" << i << " PHYSICS BUG ***:" << endl;
                        cerr << "    Pos: predicted(" << (int)predicted[i].pos.x << "," << (int)predicted[i].pos.y 
                             << ") actual(" << (int)env[i].pos.x << "," << (int)env[i].pos.y 
                             << ") err=" << pos_err << endl;
                        cerr << "    Vel: predicted(" << (int)predicted[i].vel.x << "," << (int)predicted[i].vel.y 
                             << ") actual(" << (int)env[i].vel.x << "," << (int)env[i].vel.y 
                             << ") err=" << vel_err << endl;
                        if (angle_err > 0) {
                            cerr << "    Angle: predicted=" << (int)predicted[i].angle 
                                 << " actual=" << (int)env[i].angle 
                                 << " err=" << angle_err << endl;
                        }
                        max_pos_err_clean = std::max(max_pos_err_clean, pos_err);
                        max_vel_err_clean = std::max(max_vel_err_clean, vel_err);
                    }
                }
            }
            if (!any_our_bug) {
                cerr << "  OUR PODS OK (max_clean_pos=" << fixed << setprecision(1) << max_pos_err_clean 
                     << " max_clean_vel=" << max_vel_err_clean << ")" << endl;
            }
        }

        cerr << "--- STATE DUMP ---" << endl;
        for (int i = 0; i < 4; i++) {
            cerr << "Pod " << i << ": Pos(" << env[i].pos.x << ", " << env[i].pos.y 
                 << ") Vel(" << env[i].vel.x << ", " << env[i].vel.y 
                 << ") Angle: " << env[i].angle << " NextCP: " << env[i].next_cp_id << endl;
        }

        // Let GABot compute actions for both pods (runner GA, blocker heuristic)
        vector<PodAction> actions = bot.GetActions(env);
        
        int runner_idx = 0;
        int blocker_idx = 1;
        double score0 = pod_laps[0] * 50000 + env[0].next_cp_id * 1000 - env[0].pos.Distance(cps[env[0].next_cp_id]);
        double score1 = pod_laps[1] * 50000 + env[1].next_cp_id * 1000 - env[1].pos.Distance(cps[env[1].next_cp_id]);
        if (score1 > score0 + 1500) {
            runner_idx = 1;
            blocker_idx = 0;
        }

        for (int i = 0; i < 2; i++) {
            bool use_boost = false;
            if (i == runner_idx && ((runner_idx == 0 && boost_available_0) || (runner_idx == 1 && boost_available_1))) {
                double dist = env[i].pos.Distance(cps[env[i].next_cp_id]);
                double target_angle = GameEngine::RadToDeg(std::atan2(cps[env[i].next_cp_id].y - env[i].pos.y, cps[env[i].next_cp_id].x - env[i].pos.x));
                double diff = std::abs(GameEngine::ShortestAngleDiff(env[i].angle, target_angle));
                if (dist > 5000 && diff < 5.0) {
                    use_boost = true;
                    if (runner_idx == 0) boost_available_0 = false; else boost_available_1 = false;
                }
            }

            int out_thrust = actions[i].thrust;
            
            if (env[i].angle < 0 && out_thrust != -1) {
                out_thrust = 200;
            }

            last_output[i].tx = (int)actions[i].tx;
            last_output[i].ty = (int)actions[i].ty;
            last_output[i].was_boost = use_boost;
            last_output[i].was_shield = (out_thrust == -1);
            last_output[i].thrust = use_boost ? 650 : out_thrust;

            if (use_boost) {
                cout << last_output[i].tx << " " << last_output[i].ty << " BOOST" << endl;
                shield_cd_track[i] = 0;
            } else if (out_thrust == -1) {
                cout << last_output[i].tx << " " << last_output[i].ty << " SHIELD" << endl;
                shield_cd_track[i] = 3;
            } else {
                cout << last_output[i].tx << " " << last_output[i].ty << " " << out_thrust << endl;
            }
        }
        
        // Predict next state
        predicted = env;
        for (int i = 0; i < 2; i++) {
            if (last_output[i].was_shield) {
                predicted[i].shield_cd = 3;
            }
        }
        
        {
            int thrust0 = last_output[0].thrust;
            if (last_output[0].was_shield) thrust0 = -1;
            predicted[0].ApplyServerAction((double)last_output[0].tx, (double)last_output[0].ty, thrust0);
        }
        {
            int thrust1 = last_output[1].thrust;
            if (last_output[1].was_shield) thrust1 = -1;
            predicted[1].ApplyServerAction((double)last_output[1].tx, (double)last_output[1].ty, thrust1);
        }
        
        Evolution::ApplyBasicProxy(predicted[2], cps);
        Evolution::ApplyBasicProxy(predicted[3], cps);
        
        pred_had_opp_collision[0] = false;
        pred_had_opp_collision[1] = false;
        
        PhysicsSimulator::SimulateTurn(predicted);
        
#ifdef LOCAL_VERIFY
        {
            static bool initialized = false;
            static Game legacyGame;
            static MapData map;
            if (!initialized) {
                map.nBeacons = cp_count;
                map.nLaps = laps;
                for (int i = 0; i < cp_count; i++) {
                    map.beacons[i].pos = {cps[i].x, cps[i].y};
                }
                map.preCalculateBeaconStuff();
                legacyGame.mapData = &map;
                initialized = true;
            }

            for (int i = 0; i < 4; i++) {
                legacyGame.ships[i].pos = {env[i].pos.x, env[i].pos.y};
                legacyGame.ships[i].speed = {env[i].vel.x, env[i].vel.y};
                legacyGame.ships[i].angle = Angle(env[i].angle * PI / 180.0);
                legacyGame.ships[i].shieldCounter = env[i].shield_cd > 0 ? env[i].shield_cd + 1 : 0;
                legacyGame.ships[i].inverseShipMass = (env[i].shield_cd > 0) ? 0.1 : 1.0;
                legacyGame.ships[i].nextBeacon = env[i].next_cp_id;
                legacyGame.ships[i].lapNumber = env[i].laps_completed;
            }

            for (int i = 0; i < 2; i++) {
                double target_angle = std::atan2(last_output[i].ty - env[i].pos.y, last_output[i].tx - env[i].pos.x);
                double angle_diff = target_angle - (env[i].angle * PI / 180.0);
                while (angle_diff > PI) angle_diff -= 2 * PI;
                while (angle_diff < -PI) angle_diff += 2 * PI;
                
                GameAction ga(angle_diff, last_output[i].thrust, last_output[i].was_shield);
                legacyGame.ships[i].applyGameAction(ga);
            }
            for (int i = 2; i < 4; i++) {
                double target_angle = std::atan2(cps[env[i].next_cp_id].y - env[i].pos.y, cps[env[i].next_cp_id].x - env[i].pos.x);
                double angle_diff = target_angle - (env[i].angle * PI / 180.0);
                while (angle_diff > PI) angle_diff -= 2 * PI;
                while (angle_diff < -PI) angle_diff += 2 * PI;
                GameAction ga(angle_diff, 200, false);
                legacyGame.ships[i].applyGameAction(ga);
            }

            GameSimulator sim;
            sim.simulGame = legacyGame;
            sim.currentTime = 0.0;
            sim.simulGame.turnNumber = 0;
            
            double tmin = 0.0;
            double tmax = 1.0;
            bool oneTurnFinished = false;
            while(!oneTurnFinished) {
                oneTurnFinished = true;
                bool foundIntersect = false;
                double timeIntersect = 1e8;
                int ship1, ship2;
                for (int i = 0; i < 4 ; ++i) {
                    for (int j = i+1; j < 4; ++j) {
                        double timeResult = sim.simulGame.ships[i].intersectTime(sim.simulGame.ships[j], 640000.0);
                        if (timeResult < tmax && timeResult > tmin && timeResult < timeIntersect) {
                            ship1 = i; ship2 = j;
                            timeIntersect = timeResult;
                            foundIntersect = true;
                        }
                    }
                }
                if(foundIntersect) {
                    sim.bounce(sim.simulGame.ships[ship1], sim.simulGame.ships[ship2], timeIntersect);
                    tmin = timeIntersect;
                    oneTurnFinished = false;
                }
            }
            for(int i = 0; i < 4 ; ++i) {
                sim.simulGame.ships[i].updatePosition(1.0);
                sim.simulGame.ships[i].speed *= 0.85;
                sim.simulGame.ships[i].truncateAll();
            }

            for (int i = 0; i < 4; i++) {
                double dx = std::abs(predicted[i].pos.x - sim.simulGame.ships[i].pos.x);
                double dy = std::abs(predicted[i].pos.y - sim.simulGame.ships[i].pos.y);
                double dvx = std::abs(predicted[i].vel.x - sim.simulGame.ships[i].speed.x);
                double dvy = std::abs(predicted[i].vel.y - sim.simulGame.ships[i].speed.y);
                
                if (dx > 0.001 || dy > 0.001 || dvx > 0.001 || dvy > 0.001) {
                    cerr << "MISMATCH Pod " << i << " turn " << verify_turn << endl;
                    cerr << "  Our: Pos(" << predicted[i].pos.x << "," << predicted[i].pos.y << ") Vel(" << predicted[i].vel.x << "," << predicted[i].vel.y << ")" << endl;
                    cerr << "  Leg: Pos(" << sim.simulGame.ships[i].pos.x << "," << sim.simulGame.ships[i].pos.y << ") Vel(" << sim.simulGame.ships[i].speed.x << "," << sim.simulGame.ships[i].speed.y << ")" << endl;
                }
            }
        }
#endif

        for (int our = 0; our < 2; our++) {
            for (int opp = 2; opp < 4; opp++) {
                double dx = predicted[our].pos.x - predicted[opp].pos.x;
                double dy = predicted[our].pos.y - predicted[opp].pos.y;
                double dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 1200.0) {
                    pred_had_opp_collision[our] = true;
                }
                dx = env[our].pos.x - env[opp].pos.x;
                dy = env[our].pos.y - env[opp].pos.y;
                dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 1600.0) {
                    pred_had_opp_collision[our] = true;
                }
            }
        }
        
        for (int p = 0; p < 4; p++) {
            if (predicted[p].pos.DistanceSq(cps[predicted[p].next_cp_id]) <= 360000) {
                predicted[p].next_cp_id = (predicted[p].next_cp_id + 1) % cp_count;
            }
        }
        
        has_prediction = true;
    }
}
