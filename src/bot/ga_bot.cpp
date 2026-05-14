#include "src/bot/ga_bot.h"
#include "src/engine/engine.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>

using namespace std;

void Action::Randomize() {
    gene1 = FastRandInt(0, 1000) / 1000.0;
    gene2 = FastRandInt(0, 1000) / 1000.0;
    gene3 = FastRandInt(0, 1000) / 1000.0;
}

void Action::Mutate() {
    double r = FastRandInt(0, 100);
    if (r < 33) {
        gene1 += FastRandInt(-100, 100) / 1000.0;
        gene1 = std::max(0.0, std::min(1.0, gene1));
    } else if (r < 66) {
        gene2 += FastRandInt(-100, 100) / 1000.0;
        gene2 = std::max(0.0, std::min(1.0, gene2));
    } else {
        gene3 += FastRandInt(-100, 100) / 1000.0;
        gene3 = std::max(0.0, std::min(1.0, gene3));
    }
}

Solution::Solution() : score(-1e9) {}

void Solution::Randomize(int horizon) {
    for (int i = 0; i < horizon; ++i) {
        moves[0][i].Randomize();
        moves[1][i].Randomize();
    }
}

void Solution::MutateFrom(const Solution& parent, int horizon) {
    for (int i = 0; i < horizon; ++i) {
        moves[0][i] = parent.moves[0][i];
        moves[1][i] = parent.moves[1][i];
    }
    int turn = FastRandInt(0, horizon - 1);
    if (FastRandInt(0, 1) == 0) moves[0][turn].Mutate();
    else moves[1][turn].Mutate();
}

// ======== EVALUATION ========
double Evolution::EvaluatePod(const Pod& pod, const vector<Vec2>& cps, int initial_cp, const BotConfig& config) {
    int cpspassed = pod.next_cp_id - initial_cp;
    if (cpspassed < 0) cpspassed += cps.size();

    double score = cpspassed * 50000.0;
    
    // Corner-cutting target
    Vec2 target = cps[pod.next_cp_id];
    int next_next = (pod.next_cp_id + 1) % cps.size();
    double to_next_x = cps[next_next].x - target.x;
    double to_next_y = cps[next_next].y - target.y;
    double to_next_len = std::sqrt(to_next_x*to_next_x + to_next_y*to_next_y);
    if (to_next_len > 0.0) {
        target.x += (to_next_x / to_next_len) * config.corner_cut_dist;
        target.y += (to_next_y / to_next_len) * config.corner_cut_dist;
    }
    
    // Distance penalty
    score -= pod.pos.Distance(target) * config.dist_weight;

    Vec2 dir(target.x - pod.pos.x, target.y - pod.pos.y);
    double dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
    if (dir_len > 0) {
        double nx = dir.x / dir_len;
        double ny = dir.y / dir_len;
        
        // Velocity toward target
        double speed_toward = pod.vel.x * nx + pod.vel.y * ny;
        score += speed_toward * config.align_weight;
        
        // Lateral drift penalty
        double lateral = pod.vel.x * ny - pod.vel.y * nx;
        score -= std::abs(lateral) * config.lateral_penalty;
    }
    
    // Speed bonus
    double total_speed = std::sqrt(pod.vel.x*pod.vel.x + pod.vel.y*pod.vel.y);
    score += total_speed * config.speed_bonus;
    
    // Angle penalty
    if (dir_len > 0) {
        double target_angle = GameEngine::RadToDeg(std::atan2(dir.y, dir.x));
        double angle_err = std::abs(GameEngine::ShortestAngleDiff(pod.angle, target_angle));
        score -= angle_err * config.angle_penalty;
    }

    // Shield penalty
    if (pod.shield_cd == 3) {
        score -= config.shield_penalty;
    }

    return score;
}

void Evolution::ApplyBasicProxy(Pod& p, const vector<Vec2>& cps) {
    Vec2 target = cps[p.next_cp_id];
    double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - p.pos.y, target.x - p.pos.x));
    int angle_shift = (int)GameEngine::ShortestAngleDiff(p.angle, desired_angle);
    angle_shift = std::max(-18, std::min(18, angle_shift));
    p.ApplyGAAction(angle_shift, 200);
}

// ======== GENE DECODING ========
static void DecodeGeneToAction(const Action& action_gene, Pod& pod, const vector<Vec2>& cps, const vector<Pod>& env, int opp_start_idx, bool is_runner, const BotConfig& config) {
    if (action_gene.gene1 > 0.95) {
        pod.ApplyGAAction(0, -1); // Shield
        return;
    }

    if (is_runner && action_gene.gene1 < 0.3) {
        // Direct bot to next CP with corner cutting
        Vec2 target = cps[pod.next_cp_id];
        int next_next = (pod.next_cp_id + 1) % cps.size();
        double to_next_x = cps[next_next].x - target.x;
        double to_next_y = cps[next_next].y - target.y;
        double to_next_len = std::sqrt(to_next_x*to_next_x + to_next_y*to_next_y);
        if (to_next_len > 0.0) {
            target.x += (to_next_x / to_next_len) * config.corner_cut_dist;
            target.y += (to_next_y / to_next_len) * config.corner_cut_dist;
        }
        double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - pod.pos.y, target.x - pod.pos.x));
        int angle_shift = (int)GameEngine::ShortestAngleDiff(pod.angle, desired_angle);
        angle_shift = std::max(-18, std::min(18, angle_shift));
        pod.ApplyGAAction(angle_shift, 200);
        return;
    }

    if (!is_runner) {
        if (action_gene.gene1 < 0.2) {
            const Pod& opp_pod = env[opp_start_idx];
            Vec2 target = cps[opp_pod.next_cp_id];
            double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - pod.pos.y, target.x - pod.pos.x));
            int angle_shift = (int)GameEngine::ShortestAngleDiff(pod.angle, desired_angle);
            angle_shift = std::max(-18, std::min(18, angle_shift));
            pod.ApplyGAAction(angle_shift, 200);
            return;
        } else if (action_gene.gene1 < 0.3) {
            const Pod& opp_pod = env[opp_start_idx];
            Vec2 target = opp_pod.pos;
            target.x += opp_pod.vel.x;
            target.y += opp_pod.vel.y;
            double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - pod.pos.y, target.x - pod.pos.x));
            int angle_shift = (int)GameEngine::ShortestAngleDiff(pod.angle, desired_angle);
            angle_shift = std::max(-18, std::min(18, angle_shift));
            pod.ApplyGAAction(angle_shift, 200);
            return;
        }
    }

    // Manual control
    int angle_shift = 0;
    if (action_gene.gene2 < 0.25) angle_shift = -18;
    else if (action_gene.gene2 > 0.75) angle_shift = 18;
    else angle_shift = -18 + 36 * ((action_gene.gene2 - 0.25) * 2.0);

    int thrust = 0;
    if (action_gene.gene3 < 0.25) thrust = 0;
    else if (action_gene.gene3 > 0.75) thrust = 200;
    else thrust = 200 * ((action_gene.gene3 - 0.25) * 2.0);

    pod.ApplyGAAction(angle_shift, thrust);
}

// ======== GA WITH JOINT SCORING ========
Solution Evolution::RunGA(const vector<Pod>& base_pods, const vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config, int runner_idx, const Solution* warm_start) {
    Solution pop[MAX_POPULATION];
    int start_idx = target_team * 2;
    int opp_start_idx = (1 - target_team) * 2;
    int pop_size = std::min(config.population, MAX_POPULATION);
    int horizon = std::min(config.horizon, MAX_HORIZON);

    for (int i = 0; i < pop_size; ++i) {
        pop[i].Randomize(horizon);
    }
    
    // Warm start from previous turn's shifted solution
    if (warm_start) {
        for (int t = 0; t < horizon - 1; ++t) {
            pop[0].moves[0][t] = warm_start->moves[0][t + 1];
            pop[0].moves[1][t] = warm_start->moves[1][t + 1];
        }
        pop[0].moves[0][horizon - 1].Randomize();
        pop[0].moves[1][horizon - 1].Randomize();
    } else {
        for (int t = 0; t < horizon; ++t) {
            pop[0].moves[0][t].gene1 = 0.15;
            pop[0].moves[1][t].gene1 = 0.15;
        }
    }
    // Seed pop[1] with Direct Bot baseline
    for (int t = 0; t < horizon; ++t) {
        pop[1].moves[0][t].gene1 = 0.15;
        pop[1].moves[1][t].gene1 = 0.15;
    }

    while (timer.ElapsedMs() < time_limit_ms) {
        for (int i = 0; i < pop_size; ++i) {
            if (pop[i].score != -1e9) continue;

            std::vector<Pod> sim_env = base_pods;

            for (int t = 0; t < horizon; ++t) {
                DecodeGeneToAction(pop[i].moves[0][t], sim_env[start_idx], cps, sim_env, opp_start_idx, true, config);
                DecodeGeneToAction(pop[i].moves[1][t], sim_env[start_idx + 1], cps, sim_env, opp_start_idx, false, config);

                if (enemy_plan) {
                    DecodeGeneToAction(enemy_plan->moves[0][t], sim_env[opp_start_idx], cps, sim_env, start_idx, true, config);
                    DecodeGeneToAction(enemy_plan->moves[1][t], sim_env[opp_start_idx + 1], cps, sim_env, start_idx, false, config);
                } else {
                    ApplyBasicProxy(sim_env[opp_start_idx], cps);
                    ApplyBasicProxy(sim_env[opp_start_idx + 1], cps);
                }

                PhysicsSimulator::SimulateTurn(sim_env);

                for(int p = 0; p < 4; ++p) {
                    if (sim_env[p].pos.DistanceSq(cps[sim_env[p].next_cp_id]) <= 360000) {
                        sim_env[p].next_cp_id = (sim_env[p].next_cp_id + 1) % cps.size();
                    }
                }
            }

            // ========== JOINT SCORING ==========
            const Pod& runner = sim_env[start_idx + runner_idx];
            const Pod& blocker = sim_env[start_idx + (1 - runner_idx)];
            const Pod& opp_runner = sim_env[opp_start_idx];
            
            // Runner score
            double runner_score = EvaluatePod(runner, cps, base_pods[start_idx + runner_idx].next_cp_id, config);
            
            // Blocker score: penalize opponent's progress
            double opp_progress = EvaluatePod(opp_runner, cps, base_pods[opp_start_idx].next_cp_id, config);
            double block_score = -opp_progress * config.opp_penalty;
            
            // Blocker proximity to opponent's next CP
            int target_cp = opp_runner.next_cp_id;
            double opp_dist_to_cp = opp_runner.pos.Distance(cps[target_cp]);
            double blocker_dist_to_cp = blocker.pos.Distance(cps[target_cp]);
            if (opp_dist_to_cp < blocker_dist_to_cp - 1500) {
                target_cp = (target_cp + 1) % cps.size();
            }
            double dist_to_intercept = blocker.pos.Distance(cps[target_cp]);
            block_score -= dist_to_intercept * 2.0;
            
            // Collision bonus: did our blocker hit the opponent?
            if (blocker.pos.Distance(opp_runner.pos) < 800) {
                block_score += 50000.0;
            }
            
            // Positional bonus: is blocker between opponent and their CP?
            Vec2 opp_to_cp(cps[target_cp].x - opp_runner.pos.x, cps[target_cp].y - opp_runner.pos.y);
            Vec2 opp_to_blocker(blocker.pos.x - opp_runner.pos.x, blocker.pos.y - opp_runner.pos.y);
            double opp_to_cp_len = std::sqrt(opp_to_cp.x*opp_to_cp.x + opp_to_cp.y*opp_to_cp.y);
            if (opp_to_cp_len > 0) {
                double proj = (opp_to_blocker.x * opp_to_cp.x + opp_to_blocker.y * opp_to_cp.y) / opp_to_cp_len;
                double perp_x = opp_to_blocker.x - (opp_to_cp.x / opp_to_cp_len) * proj;
                double perp_y = opp_to_blocker.y - (opp_to_cp.y / opp_to_cp_len) * proj;
                double perp_dist = std::sqrt(perp_x*perp_x + perp_y*perp_y);
                // Blocker is "between" if projection is positive (ahead of opp) and perpendicular distance is small
                if (proj > 0 && proj < opp_to_cp_len && perp_dist < 2000) {
                    block_score += 10000.0 * (1.0 - perp_dist / 2000.0);
                }
            }
            
            // Friendly fire penalty: don't ram your own teammate
            if (runner.pos.Distance(blocker.pos) < 900) {
                block_score -= 30000.0;
            }

            pop[i].score = runner_score + block_score * config.block_weight;
        }

        std::sort(pop, pop + pop_size, [](const Solution& a, const Solution& b) { return a.score > b.score; });

        for (int i = pop_size / 10; i < pop_size; ++i) {
            int parent_idx = FastRandInt(0, (pop_size / 10) - 1);
            pop[i].MutateFrom(pop[parent_idx], horizon);
            pop[i].score = -1e9;
        }
    }

    return pop[0];
}

// ======== HEURISTIC BLOCKER ========
PodAction HeuristicBlocker::GetAction(const Pod& blocker, const vector<Pod>& pods, const vector<Vec2>& cps, int opp_start_idx, const BotConfig& config) {
    const Pod& opp0 = pods[opp_start_idx];
    const Pod& opp1 = pods[opp_start_idx + 1];
    // Identify opponent runner (furthest ahead)
    const Pod& target_opp = (opp0.next_cp_id == opp1.next_cp_id) ?
        (opp0.pos.DistanceSq(cps[opp0.next_cp_id]) < opp1.pos.DistanceSq(cps[opp1.next_cp_id]) ? opp0 : opp1)
        : ((opp0.next_cp_id >= opp1.next_cp_id) ? opp0 : opp1);
    
    double dist_to_opp = blocker.pos.Distance(target_opp.pos);
    Vec2 intercept;
    
    if (dist_to_opp < 1500) {
        // RAM: aim at opponent with lead
        intercept = target_opp.pos;
        intercept.x += target_opp.vel.x * 0.5;
        intercept.y += target_opp.vel.y * 0.5;
    } else if (dist_to_opp < 4000) {
        // CHASE close: predict 2 turns ahead
        intercept.x = target_opp.pos.x + target_opp.vel.x * 2.0;
        intercept.y = target_opp.pos.y + target_opp.vel.y * 2.0;
    } else {
        // CHASE far: head to opponent's next CP
        Vec2 cp_target = cps[target_opp.next_cp_id];
        double our_dist = blocker.pos.Distance(cp_target);
        double opp_dist = target_opp.pos.Distance(cp_target);
        if (our_dist > opp_dist + 1000) {
            int next_cp = (target_opp.next_cp_id + 1) % cps.size();
            cp_target = cps[next_cp];
        }
        intercept = cp_target;
    }
    
    int thrust = 200;
    
    // SHIELD on close approach
    if (dist_to_opp < config.shield_ram_dist && blocker.shield_cd == 0) {
        double rel_vx = blocker.vel.x - target_opp.vel.x;
        double rel_vy = blocker.vel.y - target_opp.vel.y;
        double dx = target_opp.pos.x - blocker.pos.x;
        double dy = target_opp.pos.y - blocker.pos.y;
        double closing = rel_vx * dx + rel_vy * dy;
        if (closing > 0 && dist_to_opp < config.shield_ram_dist) {
            return {intercept.x, intercept.y, -1}; // SHIELD
        }
    }
    
    // Camping: slow down when at intercept point and opponent is far
    if (dist_to_opp > 5000) {
        double our_dist = blocker.pos.Distance(intercept);
        if (our_dist < 1500) {
            double speed = std::sqrt(blocker.vel.x*blocker.vel.x + blocker.vel.y*blocker.vel.y);
            thrust = (speed > 200) ? 0 : 100;
        }
    }
    
    return {intercept.x, intercept.y, thrust};
}

bool GABot::verbose = false;

// ======== BOT IMPLEMENTATION ========
GABot::GABot(BotConfig config) : config_(config) {}
std::string GABot::GetName() const { return config_.name; }

void GABot::Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) {
    laps_ = laps;
    cp_count_ = cp_count;
    cps_ = cps;
    team_id_ = team_id;
    has_prev_best_ = false;
    
    // Pre-compute optimal boost segment
    best_boost_cp_ = -1;
    double max_boost_value = 0;
    for (int i = 0; i < cp_count; i++) {
        int next = (i + 1) % cp_count;
        double d = cps[i].Distance(cps[next]);
        // Discount by turn angle sharpness
        int after = (next + 1) % cp_count;
        double dx1 = cps[next].x - cps[i].x, dy1 = cps[next].y - cps[i].y;
        double dx2 = cps[after].x - cps[next].x, dy2 = cps[after].y - cps[next].y;
        double len1 = std::sqrt(dx1*dx1 + dy1*dy1);
        double len2 = std::sqrt(dx2*dx2 + dy2*dy2);
        double dot = (dx1*dx2 + dy1*dy2) / (len1 * len2 + 0.001);
        double angle = std::acos(std::max(-1.0, std::min(1.0, dot)));
        double boost_value = d * (1.0 - angle / 3.14159);
        if (boost_value > max_boost_value) {
            max_boost_value = boost_value;
            best_boost_cp_ = i;
        }
    }
}

std::vector<PodAction> GABot::GetActions(const std::vector<Pod>& pods) {
    Timer timer;
    timer.Start();

    int start_idx = team_id_ * 2;
    int opp_start_idx = (1 - team_id_) * 2;
    turn_count_++;

    // Dynamic role assignment
    double score0 = pods[start_idx].next_cp_id * 1000 - pods[start_idx].pos.Distance(cps_[pods[start_idx].next_cp_id]);
    double score1 = pods[start_idx+1].next_cp_id * 1000 - pods[start_idx+1].pos.Distance(cps_[pods[start_idx+1].next_cp_id]);
    if (score1 > score0 + 1500) {
        runner_idx_ = 1;
        blocker_idx_ = 0;
    } else {
        runner_idx_ = 0;
        blocker_idx_ = 1;
    }

    // Phase 1: Model opponent
    Timer opp_timer;
    opp_timer.Start();
    Solution opp_plan = Evolution::RunGA(pods, cps_, timer, 15.0, 1 - team_id_, nullptr, BotConfig(), 0, nullptr);
    double opp_ms = opp_timer.ElapsedMs();
    
    // Phase 2: Plan our moves with solution persistence
    Timer our_timer;
    our_timer.Start();
    Solution our_plan = Evolution::RunGA(pods, cps_, timer, 55.0, team_id_, &opp_plan, config_, runner_idx_, has_prev_best_ ? &prev_best_ : nullptr);
    double our_ms = our_timer.ElapsedMs();
    
    prev_best_ = our_plan;
    has_prev_best_ = true;

    double total_ms = timer.ElapsedMs();
    
    if (verbose) {
        cerr << "[" << config_.name << " T" << turn_count_ << "] "
             << "Opp:" << std::fixed << std::setprecision(1) << opp_ms << "ms "
             << "Our:" << our_ms << "ms "
             << "Total:" << total_ms << "ms "
             << "Role:R" << runner_idx_ << "/B" << blocker_idx_
             << " Score:" << std::setprecision(0) << our_plan.score << endl;
    }

    std::vector<PodAction> actions(2);
    
    // Decode Runner action from GA
    {
        int i = runner_idx_;
        Action a = our_plan.moves[i][0];
        Pod p = pods[start_idx + i];
        DecodeGeneToAction(a, p, cps_, pods, opp_start_idx, true, config_);
        
        double target_angle = GameEngine::NormalizeAngle(p.angle);
        int a_idx = ((int)std::round(target_angle)) % 360;
        if (a_idx < 0) a_idx += 360;
        double tx = p.pos.x + cos_lut[a_idx] * 10000.0;
        double ty = p.pos.y + sin_lut[a_idx] * 10000.0;
        
        if (pods[start_idx + i].angle < 0) {
            tx = cps_[pods[start_idx + i].next_cp_id].x;
            ty = cps_[pods[start_idx + i].next_cp_id].y;
        }

        int out_thrust = 200;
        if (a.gene1 > 0.95 && p.shield_cd == 0) out_thrust = -1;
        else if (a.gene1 < 0.3) out_thrust = 200;
        else {
            if (a.gene3 < 0.25) out_thrust = 0;
            else if (a.gene3 > 0.75) out_thrust = 200;
            else out_thrust = 200 * ((a.gene3 - 0.25) * 2.0);
        }
        
        // Turn 1 forced acceleration
        if (pods[start_idx + i].angle < 0 && out_thrust != -1) out_thrust = 200;
        
        actions[i] = {tx, ty, out_thrust};
    }
    
    // Blocker: use heuristic
    actions[blocker_idx_] = HeuristicBlocker::GetAction(pods[start_idx + blocker_idx_], pods, cps_, opp_start_idx, config_);
    
    // Turn 1 forced acceleration for blocker
    if (pods[start_idx + blocker_idx_].angle < 0 && actions[blocker_idx_].thrust != -1) {
        actions[blocker_idx_].thrust = 200;
    }

    return actions;
}
