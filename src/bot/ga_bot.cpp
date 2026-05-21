#include "src/bot/ga_bot.h"
#include "src/engine/engine.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>

using namespace std;

// Thread-local reusable simulation buffer (eliminates heap alloc per Simulate call)
static thread_local vector<Pod> g_sim(4);

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
    int ram_beacon;       // Which CP the blocker should camp near
    bool risk_timeout;    // If true, blocker races instead of blocking
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

        // Opponent runner: aim at entry point for corner cutting (matches legacy behavior)
        {
            const Vec2& tgt = ep[g_sim[opp_runner].next_cp_id];
            double desired = GameEngine::RadToDeg(atan2(tgt.y - g_sim[opp_runner].pos.y,
                                                         tgt.x - g_sim[opp_runner].pos.x));
            double shift = GameEngine::ShortestAngleDiff(g_sim[opp_runner].angle, desired);
            shift = max(-18.0, min(18.0, shift));
            g_sim[opp_runner].ApplyGAAction(shift, 200);
        }

        // Opponent blocker: chase our runner with velocity lead + shield on close approach
        {
            double tx = g_sim[runner_pod].pos.x + g_sim[runner_pod].vel.x;
            double ty = g_sim[runner_pod].pos.y + g_sim[runner_pod].vel.y;
            double desired = GameEngine::RadToDeg(atan2(ty - g_sim[opp_blocker].pos.y,
                                                         tx - g_sim[opp_blocker].pos.x));
            double shift = GameEngine::ShortestAngleDiff(g_sim[opp_blocker].angle, desired);
            shift = max(-18.0, min(18.0, shift));
            int opp_thr = 200;
            double opp_dist = g_sim[opp_blocker].pos.Distance(g_sim[runner_pod].pos);
            if (opp_dist < 900 && g_sim[opp_blocker].shield_cd == 0) opp_thr = -1;
            g_sim[opp_blocker].ApplyGAAction(shift, opp_thr);
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

    // Blocker evaluation (depends on timeout risk)
    {
        const Pod& blocker = g_sim[blocker_pod];

        if (ctx.risk_timeout) {
            // Timeout risk: blocker races to its own CPs (matches legacy riskTimeout behavior)
            if (blocker.laps_completed < ctx.laps) {
                int blocker_lin = blocker.laps_completed * n + blocker.next_cp_id;
                score -= dte[blocker_lin] + blocker.pos.Distance(ep[blocker.next_cp_id]);
            }
        } else {
            // Normal blocking: match legacy COEFFEVAL_GLOBALRAM = 1.5

            // Near ram rest point with distance threshold (legacy: acceptable dist = 300)
            const Vec2& rrp = (*ctx.ram_rest_points)[ctx.ram_beacon];
            double bd = blocker.pos.Distance(rrp);
            double d_adj = bd - 300.0;
            if (d_adj < 0) d_adj *= 0.1;  // Soft penalty when already close
            score -= 0.045 * d_adj;

            // Facing opponent runner (legacy: -20.0 * 1.5 * |angle_diff|)
            double dx = opp_run.pos.x - blocker.pos.x;
            double dy = opp_run.pos.y - blocker.pos.y;
            double desired_rad = atan2(dy, dx);
            double blocker_rad = blocker.angle * PI / 180.0;
            double face_diff = atan2(sin(desired_rad - blocker_rad), cos(desired_rad - blocker_rad));
            score -= 30.0 * fabs(face_diff);

            // Stay in front of opponent (between opponent and their target CP)
            double bx = blocker.pos.x - opp_run.pos.x;
            double by = blocker.pos.y - opp_run.pos.y;
            double cx = (*ctx.cps)[opp_run.next_cp_id].x - opp_run.pos.x;
            double cy = (*ctx.cps)[opp_run.next_cp_id].y - opp_run.pos.y;
            double cross_val = fabs(bx * cy - by * cx);
            double dot_val = bx * cx + by * cy;
            score -= 30.0 * atan2(cross_val, dot_val);
        }
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
    }

    // === Seed heuristic moves for runner + blocker ===
    {
        // Runner: forward-simulate go-to-entry-point
        Pod rsim = base_pods[runner_pod];
        Action runner_h[MAX_HORIZON];
        for (int t = 0; t < horizon; ++t) {
            const Vec2& tgt = (*ctx.entry_points)[rsim.next_cp_id];
            runner_h[t] = MakeGoToTarget(rsim, tgt.x, tgt.y, 200);
            rsim.ApplyGAAction(runner_h[t].angle, runner_h[t].thrust);
            rsim.pos.x += rsim.vel.x; rsim.pos.y += rsim.vel.y;
            rsim.vel.x = trunc(rsim.vel.x * 0.85); rsim.vel.y = trunc(rsim.vel.y * 0.85);
            rsim.pos.x = round(rsim.pos.x); rsim.pos.y = round(rsim.pos.y);
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
            bsim.vel.x = trunc(bsim.vel.x * 0.85); bsim.vel.y = trunc(bsim.vel.y * 0.85);
            bsim.pos.x = round(bsim.pos.x); bsim.pos.y = round(bsim.pos.y);
        }

        // Seed variants
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

        fill_seed(idx,     200, 1.0,  MAX_HORIZON, MAX_HORIZON); // base
        fill_seed(idx + 1, 150, 1.0,  MAX_HORIZON, MAX_HORIZON); // lower thrust
        fill_seed(idx + 2, 200, 0.5,  MAX_HORIZON, MAX_HORIZON); // halved angles
        fill_seed(idx + 3, 200, 1.3,  MAX_HORIZON, MAX_HORIZON); // wider angles
        fill_seed(idx + 4, 200, 1.0,  MAX_HORIZON, 0);           // blocker shield t=0
        fill_seed(idx + 5, 200, 1.0,  0,           MAX_HORIZON); // runner shield t=0
        fill_seed(idx + 6, 200, 1.0,  MAX_HORIZON, 1);           // blocker shield t=1
        fill_seed(idx + 7, 200, 1.0,  1,           MAX_HORIZON); // runner shield t=1

        // Seed with blocker aiming at ram rest point instead of opponent
        if (idx + 8 < pop_size) {
            const Vec2& rrp = (*ctx.ram_rest_points)[ctx.ram_beacon];
            Pod bsim2 = base_pods[blocker_pod];
            for (int t = 0; t < horizon; ++t) {
                pop[idx + 8].runner_moves[t] = runner_h[t];
                pop[idx + 8].blocker_moves[t] = MakeGoToTarget(bsim2, rrp.x, rrp.y, 200);
                bsim2.ApplyGAAction(pop[idx + 8].blocker_moves[t].angle, 200);
                bsim2.pos.x += bsim2.vel.x; bsim2.pos.y += bsim2.vel.y;
                bsim2.vel.x = trunc(bsim2.vel.x * 0.85); bsim2.vel.y = trunc(bsim2.vel.y * 0.85);
                bsim2.pos.x = round(bsim2.pos.x); bsim2.pos.y = round(bsim2.pos.y);
            }
            pop[idx + 8].runner_shield_step = MAX_HORIZON;
            pop[idx + 8].blocker_shield_step = MAX_HORIZON;
        }
        idx += 9;
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

        // Stagnation detection (from legacy bot): if all scores converge, penalize non-best
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
    // This is where an optimally-cornering pod would aim (matches legacy entryPointv2)
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
    ctx.risk_timeout = pods[start_idx + blocker_idx_].timeout >= 60;

    Solution best = RunGA(pods, timer, 75.0, ctx, config_, has_prev_best_ ? &prev_best_ : nullptr);
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
