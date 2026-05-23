#include "src/bot/ga_bot.h"
#include "src/engine/engine.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstring>

using namespace std;

// === GA-specific constants ===
constexpr double RUNNER_FAST_ACT_PENALTY = 1000.0;
constexpr double OPPONENT_DELAY_WEIGHT = 10000.0;
constexpr double OPPONENT_DISTANCE_WEIGHT = 10.0;
constexpr double BLOCKER_REST_THRESHOLD = 300.0;
constexpr double BLOCKER_REST_SOFT_FACTOR = 0.1;
constexpr double BLOCKER_REST_PENALTY_FACTOR = 0.045;
constexpr double RUNNER_SHIELD_COST = 330.0;
constexpr double RUNNER_THRUST_BONUS = 0.16;
constexpr double BLOCKER_SHIELD_COST = 495.0;
constexpr double BLOCKER_THRUST_BONUS = 0.06;
constexpr double TURN_TIME_LIMIT_MS = 75.0;
constexpr double FIRST_TURN_TIME_LIMIT_MS = 1000.0;

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
    angle += FastRandInt(-120, 120) / 10.0;  // +/-12 degrees (matches legacy)
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
    const BotConfig* config;
    bool force_boost;     // Force runner to boost on Turn 0
    Action opp_moves[MAX_HORIZON];
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

        // Opponent runner: apply pre-evolved optimal GA moves
        g_sim[opp_runner].ApplyGAAction(ctx.opp_moves[t].angle, ctx.opp_moves[t].thrust);

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
            if (opp_dist < config.shield_ram_dist && g_sim[opp_blocker].shield_cd == 0) opp_thr = -1;
            g_sim[opp_blocker].ApplyGAAction(shift, opp_thr);
        }

        PhysicsSimulator::SimulateTurn(g_sim);

        // Fully unrolled CP check sweep (exact arena logic matching)
        {
            // Pod 0
            double dx0 = g_sim[0].pos.x - cps[g_sim[0].next_cp_id].x;
            double dy0 = g_sim[0].pos.y - cps[g_sim[0].next_cp_id].y;
            if (dx0 * dx0 + dy0 * dy0 <= 360000.0) {
                g_sim[0].next_cp_id++;
                if (g_sim[0].next_cp_id >= n) { g_sim[0].next_cp_id = 0; g_sim[0].laps_completed++; }
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
                if (g_sim[1].next_cp_id >= n) { g_sim[1].next_cp_id = 0; g_sim[1].laps_completed++; }
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
                if (g_sim[2].next_cp_id >= n) { g_sim[2].next_cp_id = 0; g_sim[2].laps_completed++; }
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
                if (g_sim[3].next_cp_id >= n) { g_sim[3].next_cp_id = 0; g_sim[3].laps_completed++; }
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

        // Boundary penalties
        auto check_bounds = [](const Vec2& pos) {
            if (pos.x < -1000.0 || pos.x > 17000.0 || pos.y < -1000.0 || pos.y > 10000.0)
                return -100000.0;
            return 0.0;
        };
        score += check_bounds(runner.pos);
        score += check_bounds(state[blocker_pod].pos);

        // Win/loss
        if (runner.laps_completed >= ctx.laps) score += 1e9;
        if (opp_run.laps_completed >= ctx.laps) score -= 1e9;

        // CP crossing step bonus
        int initial_linear = init_lap * n + init_cp;
        int current_linear = runner.laps_completed * n + runner.next_cp_id;
        int cps_crossed = current_linear - initial_linear;
        if (cps_crossed > 0) score += cps_crossed * 15000.0;

        // Runner: minimize remaining race distance
        if (runner.laps_completed < ctx.laps) {
            int runner_lin = runner.laps_completed * n + runner.next_cp_id;
            double runner_remain = dte[runner_lin] + runner.pos.Distance(ep[runner.next_cp_id]);
            score -= runner_remain * config.dist_weight;
        }

        // Runner: velocity alignment, speed bonus, lateral penalty, angle penalty
        {
            double dx = ep[runner.next_cp_id].x - runner.pos.x;
            double dy = ep[runner.next_cp_id].y - runner.pos.y;
            double d = sqrt(dx * dx + dy * dy);
            if (d > 0) {
                double nx = dx / d;
                double ny = dy / d;
                score += (runner.vel.x * nx + runner.vel.y * ny) * config.align_weight;
                double lateral = runner.vel.x * ny - runner.vel.y * nx;
                score -= fabs(lateral) * config.lateral_penalty;
                double target_angle = GameEngine::RadToDeg(atan2(dy, dx));
                double angle_err = fabs(GameEngine::ShortestAngleDiff(runner.angle, target_angle));
                score -= angle_err * config.angle_penalty;
            }
            double speed = sqrt(runner.vel.x * runner.vel.x + runner.vel.y * runner.vel.y);
            score += speed * config.speed_bonus;
        }

        // Fast activation bonus
        score -= RUNNER_FAST_ACT_PENALTY * r_act;

        // Bypass opponent rammer angle
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

        // Opponent delay bonus
        double opp_delay_score = OPPONENT_DELAY_WEIGHT * config.opp_penalty * o_act;
        if (opp_run.next_cp_id == init_opp_cp && opp_run.laps_completed == init_opp_lap) {
            opp_delay_score += OPPONENT_DISTANCE_WEIGHT * config.opp_penalty * opp_run.pos.Distance(cps[opp_run.next_cp_id]);
        }
        score += opp_delay_score;

        // Blocker evaluation
        double blocker_score = 0;
        {
            const Pod& blocker = state[blocker_pod];

            if (ctx.risk_timeout) {
                if (blocker.laps_completed < ctx.laps) {
                    int blocker_lin = blocker.laps_completed * n + blocker.next_cp_id;
                    blocker_score -= (dte[blocker_lin] + blocker.pos.Distance(ep[blocker.next_cp_id])) * config.dist_weight;
                }
            } else {
                bool enforce_camp = !(init_blocker_lap == 0 && init_blocker_cp <= 1);

                if (enforce_camp) {
                    const Vec2& rrp = (*ctx.ram_rest_points)[ctx.ram_beacon];
                    double bd = blocker.pos.Distance(rrp);
                    double d_adj = bd - BLOCKER_REST_THRESHOLD;
                    if (d_adj < 0) d_adj *= BLOCKER_REST_SOFT_FACTOR;
                    blocker_score -= BLOCKER_REST_PENALTY_FACTOR * d_adj;

                    // Facing opponent runner (optimized using dot product + LUT)
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

                    // Stay in front of opponent
                    double bx = blocker.pos.x - opp_run.pos.x;
                    double by = blocker.pos.y - opp_run.pos.y;
                    double ccx = (*ctx.cps)[opp_run.next_cp_id].x - opp_run.pos.x;
                    double ccy = (*ctx.cps)[opp_run.next_cp_id].y - opp_run.pos.y;
                    double d1_sq = bx * bx + by * by;
                    double d2_sq = ccx * ccx + ccy * ccy;
                    if (d1_sq > 0 && d2_sq > 0) {
                        double dot = (bx * ccx + by * ccy) / sqrt(d1_sq * d2_sq);
                        blocker_score -= config.blocker_stay_in_front_weight * (1.0 - dot) * 1.57;
                    }
                } else {
                    // Start of the race: blocker seeks runner for ram-boost
                    double dist_to_runner = blocker.pos.Distance(runner.pos);
                    blocker_score -= dist_to_runner * config.dist_weight * 0.1;
                }
            }
        }
        score += blocker_score * config.block_weight;

        return score;
    };

    // ===== MULTI-STAGE COMBINATION =====
    double score_turn_1 = evaluate_state(g_sim_turn_1, runner_activation_turn_1, opp_activation_turn_1);
    double score_turn_N = evaluate_state(g_sim, runner_activation, opp_activation);

    double final_score = 0.10 * score_turn_1 + 0.90 * score_turn_N;

    // Shield cost/thrust bonus
    if (sol.runner_shield_step == 0) final_score -= RUNNER_SHIELD_COST;
    else final_score += RUNNER_THRUST_BONUS * max(0, sol.runner_moves[0].thrust);
    if (sol.blocker_shield_step == 0) final_score -= BLOCKER_SHIELD_COST;
    else final_score += BLOCKER_THRUST_BONUS * max(0, sol.blocker_moves[0].thrust);

    if (g_friendly_collision) {
        final_score -= 10000.0;
    }
    return final_score;
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
        Vec2 rsim_pos[MAX_HORIZON];
        for (int t = 0; t < horizon; ++t) {
            const Vec2& tgt = (*ctx.entry_points)[rsim.next_cp_id];
            runner_h[t] = MakeGoToTarget(rsim, tgt.x, tgt.y, 200);
            rsim.ApplyGAAction(runner_h[t].angle, runner_h[t].thrust);
            rsim.pos.x += rsim.vel.x; rsim.pos.y += rsim.vel.y;
            rsim.vel.x = trunc(rsim.vel.x * 0.85); rsim.vel.y = trunc(rsim.vel.y * 0.85);
            rsim.pos.x = round(rsim.pos.x); rsim.pos.y = round(rsim.pos.y);
            rsim_pos[t] = rsim.pos;
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

        fill_seed(idx,     200, 1.0,  MAX_HORIZON, MAX_HORIZON);
        fill_seed(idx + 1, 150, 1.0,  MAX_HORIZON, MAX_HORIZON);
        fill_seed(idx + 2, 200, 0.5,  MAX_HORIZON, MAX_HORIZON);
        fill_seed(idx + 3, 200, 1.3,  MAX_HORIZON, MAX_HORIZON);
        fill_seed(idx + 4, 200, 1.0,  MAX_HORIZON, 0);
        fill_seed(idx + 5, 200, 1.0,  0,           MAX_HORIZON);
        fill_seed(idx + 6, 200, 1.0,  MAX_HORIZON, 1);
        fill_seed(idx + 7, 200, 1.0,  1,           MAX_HORIZON);

        // Seed with blocker aiming at ram rest point instead of opponent
        if (idx + 13 < pop_size) {
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

            // Coordinated Boost Seed (blocker aiming at our runner's predicted path)
            Pod bsim_boost = base_pods[blocker_pod];
            for (int t = 0; t < horizon; ++t) {
                pop[idx + 9].runner_moves[t] = runner_h[t];
                pop[idx + 9].blocker_moves[t] = MakeGoToTarget(bsim_boost, rsim_pos[t].x, rsim_pos[t].y, 200);
                bsim_boost.ApplyGAAction(pop[idx + 9].blocker_moves[t].angle, 200);
                bsim_boost.pos.x += bsim_boost.vel.x; bsim_boost.pos.y += bsim_boost.vel.y;
                bsim_boost.vel.x = trunc(bsim_boost.vel.x * 0.85); bsim_boost.vel.y = trunc(bsim_boost.vel.y * 0.85);
                bsim_boost.pos.x = round(bsim_boost.pos.x); bsim_boost.pos.y = round(bsim_boost.pos.y);
            }
            pop[idx + 9].runner_shield_step = MAX_HORIZON;
            pop[idx + 9].blocker_shield_step = MAX_HORIZON;

            // Extreme Seeds Injection
            auto inject_static = [&](int i, double r_ang, int r_thr, double b_ang, int b_thr) {
                for (int t = 0; t < horizon; ++t) {
                    pop[i].runner_moves[t].angle = r_ang;
                    pop[i].runner_moves[t].thrust = r_thr;
                    pop[i].blocker_moves[t].angle = b_ang;
                    pop[i].blocker_moves[t].thrust = b_thr;
                }
                pop[i].runner_shield_step = MAX_HORIZON;
                pop[i].blocker_shield_step = MAX_HORIZON;
            };

            inject_static(idx + 10,  0.0, 200,   0.0, 200);
            inject_static(idx + 11,  0.0,   0,   0.0,   0);
            inject_static(idx + 12, -18.0, 200, -18.0, 200);
            inject_static(idx + 13,  18.0, 200,  18.0, 200);

            idx += 14;
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
    while (true) {
        iterations++;
        if (iterations % 256 == 0) {
            double elapsed = timer.ElapsedMs();
            if (elapsed >= time_limit_ms) break;
            amplitude = 1.0 - elapsed / time_limit_ms;
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
            if (cos_theta < 0.2) {
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

    // Self-Adaptive Config Selection based on map complexity
    if (config_.name == "DefaultGA") {
        bool use_handling = (cp_count >= 5 || has_sharp_turn) && (avg_dist_ <= 6500.0);
        if (use_handling) {
            config_.name = "Bot_6_Handling";
            config_.horizon = 4;
            config_.population = 80;
            config_.dist_weight = 2.9;
            config_.align_weight = 3.7;
            config_.speed_bonus = 0.2;
            config_.lateral_penalty = 1.0;
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
        } else {
            config_.name = "Bot_1_SpeedBlock";
            config_.horizon = 4;
            config_.population = 80;
            config_.dist_weight = 2.3;
            config_.align_weight = 1.7;
            config_.speed_bonus = 0.6;
            config_.lateral_penalty = 1.2;
            config_.angle_penalty = 43.0;
            config_.corner_cut_dist = 300.0;
            config_.block_weight = 6.8;
            config_.shield_penalty = 93.0;
            config_.shield_ram_dist = 1000.0;
            config_.opp_penalty = 1.2;
            config_.opp_model_ms = 0.0;
            config_.runner_bypass_weight = 15.0;
            config_.blocker_stay_in_front_weight = 25.0;
            config_.blocker_facing_weight = 25.0;
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

        Vec2 v1 = cps[i].Sub(cps[prev]);
        Vec2 v2 = cps[next].Sub(cps[i]);
        double len1 = sqrt(v1.x * v1.x + v1.y * v1.y);
        double len2 = sqrt(v2.x * v2.x + v2.y * v2.y);

        double cos_theta = 1.0;
        if (len1 > 0 && len2 > 0) {
            double dot = v1.x * v2.x + v1.y * v2.y;
            cos_theta = dot / (len1 * len2);
            cos_theta = std::max(-1.0, std::min(1.0, cos_theta));
        }

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

    // Ram rest points
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

    // Distance-to-end lookup
    dist_to_end_.resize(total_cps_in_race_ + 1, 0.0);
    dist_to_end_[total_cps_in_race_ - 1] = 0.0;
    for (int i = total_cps_in_race_ - 2; i >= 0; --i) {
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

    // Compute ram_beacon
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

    // === 10MS OPPONENT GA PREDICTION SEARCH ===
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
    bool opp_force_boost = false;
    const Pod& opp_runner = pods[opp_runner_pod];
    if (opp_runner.boost_available) {
        double dist = opp_runner.pos.Distance(entry_points_[opp_runner.next_cp_id]);
        double target_angle = GameEngine::RadToDeg(std::atan2(entry_points_[opp_runner.next_cp_id].y - opp_runner.pos.y,
                                                             entry_points_[opp_runner.next_cp_id].x - opp_runner.pos.x));
        double diff = std::abs(GameEngine::ShortestAngleDiff(opp_runner.angle, target_angle));
        if (dist > 5000.0 && diff < 5.0) {
            opp_force_boost = true;
        }
    }
    opp_ctx.force_boost = opp_force_boost;

    BotConfig opp_config = config_;
    opp_config.block_weight = 0.0;
    opp_config.opp_penalty = 0.0;
    opp_config.population = 32;
    opp_config.horizon = 4;
    opp_ctx.config = &opp_config;

    // Initialize opp_moves with basic proxy as fallback
    for (int t = 0; t < MAX_HORIZON; ++t) {
        opp_ctx.opp_moves[t] = {0.0, 200};
    }

    Solution opp_sol = RunGA(pods, timer, 10.0, opp_ctx, opp_config, nullptr);

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
    ctx.force_boost = force_boost;

    // Populate predicted opponent moves
    for (int t = 0; t < MAX_HORIZON; ++t) {
        ctx.opp_moves[t] = opp_sol.runner_moves[t];
    }
    ctx.config = &config_;

    // Dynamic Horizon Scaling
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
    double time_limit = (turn_count_ == 1) ? FIRST_TURN_TIME_LIMIT_MS : TURN_TIME_LIMIT_MS;
    Solution best = RunGA(pods, timer, time_limit, ctx, config_, has_prev_best_ ? &prev_best_ : nullptr);
    prev_best_ = best;
    has_prev_best_ = true;

    // Convert GA solution to PodActions
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

    actions[runner_idx_] = make_output(best.runner_moves[0], start_idx + runner_idx_, best.runner_shield_step);
    actions[blocker_idx_] = make_output(best.blocker_moves[0], start_idx + blocker_idx_, best.blocker_shield_step);

    if (verbose) {
        cerr << "[T" << turn_count_ << "] " << fixed << setprecision(1)
             << timer.ElapsedMs() << "ms R" << runner_idx_
             << " S:" << setprecision(0) << best.score << endl;
    }
    return actions;
}
