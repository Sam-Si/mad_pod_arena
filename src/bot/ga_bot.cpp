#include "src/bot/ga_bot.h"
#include "src/engine/engine.h"
#include <cmath>
#include <algorithm>
#include <iostream>

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

double Evolution::EvaluatePod(const Pod& pod, const vector<Vec2>& cps, int initial_cp, const BotConfig& config) {
    int cpspassed = pod.next_cp_id - initial_cp;
    if (cpspassed < 0) cpspassed += cps.size();

    double score = cpspassed * 50000.0;
    Vec2 target = cps[pod.next_cp_id];
    score -= pod.pos.Distance(target) * config.dist_weight;

    Vec2 dir(target.x - pod.pos.x, target.y - pod.pos.y);
    double dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
    if (dir_len > 0) {
        double dot = (pod.vel.x * (dir.x/dir_len)) + (pod.vel.y * (dir.y/dir_len));
        score += dot * config.align_weight;
        
        // Orbital penalty: penalize velocity that is perpendicular to the target
        double cross = (pod.vel.x * (dir.y/dir_len)) - (pod.vel.y * (dir.x/dir_len));
        score -= std::abs(cross) * 0.5; // Punish drifting sideways to prevent orbiting
    }

    // Add shield penalty
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

static void DecodeGeneToAction(const Action& action_gene, Pod& pod, const vector<Vec2>& cps, const vector<Pod>& env, int opp_start_idx, bool is_runner) {
    if (action_gene.gene1 > 0.95) {
        pod.ApplyGAAction(0, -1); // Shield
        return;
    }

    if (is_runner && action_gene.gene1 < 0.3) {
        // Direct bot to next CP
        Vec2 target = cps[pod.next_cp_id];
        double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - pod.pos.y, target.x - pod.pos.x));
        int angle_shift = (int)GameEngine::ShortestAngleDiff(pod.angle, desired_angle);
        angle_shift = std::max(-18, std::min(18, angle_shift));
        pod.ApplyGAAction(angle_shift, 200);
        return;
    }

    if (!is_runner) {
        if (action_gene.gene1 < 0.2) {
            // Direct bot to opp's next CP
            const Pod& opp_pod = env[opp_start_idx]; // Assume opp 0 is best
            Vec2 target = cps[opp_pod.next_cp_id];
            double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - pod.pos.y, target.x - pod.pos.x));
            int angle_shift = (int)GameEngine::ShortestAngleDiff(pod.angle, desired_angle);
            angle_shift = std::max(-18, std::min(18, angle_shift));
            pod.ApplyGAAction(angle_shift, 200);
            return;
        } else if (action_gene.gene1 < 0.3) {
            // Direct bot intercept opp
            const Pod& opp_pod = env[opp_start_idx];
            Vec2 target = opp_pod.pos;
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

    // Boost check
    

    pod.ApplyGAAction(angle_shift, thrust);
}

Solution Evolution::RunGA(const vector<Pod>& base_pods, const vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config) {
    Solution pop[MAX_POPULATION];
    int start_idx = target_team * 2;
    int opp_start_idx = (1 - target_team) * 2;



    int pop_size = std::min(config.population, MAX_POPULATION);
    int horizon = std::min(config.horizon, MAX_HORIZON);

    for (int i = 0; i < pop_size; ++i) {
        pop[i].Randomize(horizon);
        // Seed the 0th solution with pure Direct Bot meta-genes for an instantly coherent plan
        if (i == 0) {
            for (int t = 0; t < horizon; ++t) {
                pop[0].moves[0][t].gene1 = 0.15; // Runner direct bot
                pop[0].moves[1][t].gene1 = 0.15; // Blocker direct bot
            }
        }
    }

    int simulations = 0;

    while (timer.ElapsedMs() < time_limit_ms) {
        for (int i = 0; i < pop_size; ++i) {
            if (pop[i].score != -1e9) continue;

            std::vector<Pod> sim_env = base_pods;

            for (int t = 0; t < horizon; ++t) {
                // Apply actions using DecodeGene
                DecodeGeneToAction(pop[i].moves[0][t], sim_env[start_idx], cps, sim_env, opp_start_idx, true);
                DecodeGeneToAction(pop[i].moves[1][t], sim_env[start_idx + 1], cps, sim_env, opp_start_idx, false);

                if (enemy_plan) {
                    DecodeGeneToAction(enemy_plan->moves[0][t], sim_env[opp_start_idx], cps, sim_env, start_idx, true);
                    DecodeGeneToAction(enemy_plan->moves[1][t], sim_env[opp_start_idx + 1], cps, sim_env, start_idx, false);
                } else {
                    ApplyBasicProxy(sim_env[opp_start_idx], cps);
                    ApplyBasicProxy(sim_env[opp_start_idx + 1], cps);
                }

                PhysicsSimulator::SimulateTurn(sim_env);

                for(int p = 0; p < 4; ++p) {
                    if (sim_env[p].pos.DistanceSq(cps[sim_env[p].next_cp_id]) < 360000) {
                        sim_env[p].next_cp_id = (sim_env[p].next_cp_id + 1) % cps.size();
                    }
                }
            }

            // Incorporate blocking/avoidance logic
            double block_score = 0;
            if (config.block_weight > 0) {
                const Pod& opp_runner = sim_env[opp_start_idx];
                const Pod& blocker = sim_env[start_idx + 1];
                
                int target_cp = opp_runner.next_cp_id;
                double opp_dist_to_cp = opp_runner.pos.Distance(cps[target_cp]);
                double blocker_dist_to_cp = blocker.pos.Distance(cps[target_cp]);
                
                if (opp_dist_to_cp < blocker_dist_to_cp - 1500) {
                    target_cp = (target_cp + 1) % cps.size();
                }
                
                double dist_to_intercept = blocker.pos.Distance(cps[target_cp]);
                block_score = -dist_to_intercept * 5.0; // Heavily weight interception
                
                if (dist_to_intercept < 1500) {
                    double speed = std::sqrt(blocker.vel.x*blocker.vel.x + blocker.vel.y*blocker.vel.y);
                    block_score -= speed * 2.0; // Encourage camping
                }
                
                // Bonus if we actually hit the opponent runner
                if (blocker.pos.Distance(opp_runner.pos) < 800) {
                    block_score += 50000.0;
                }
            }

            pop[i].score = EvaluatePod(sim_env[start_idx], cps, base_pods[start_idx].next_cp_id, config) + block_score;
            simulations++;
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

GABot::GABot(BotConfig config) : config_(config) {}
std::string GABot::GetName() const { return config_.name; }

void GABot::Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) {
    laps_ = laps;
    cp_count_ = cp_count;
    cps_ = cps;
    team_id_ = team_id;
}

std::vector<PodAction> GABot::GetActions(const std::vector<Pod>& pods) {
    Timer timer;
    timer.Start();

    int start_idx = team_id_ * 2;
    int opp_start_idx = (1 - team_id_) * 2;

    // The user's original logic gives 20ms to opponent, 65ms to us.
    // In a simulation Arena, we can give a fixed amount per turn.
    // For local evaluation, we can simulate 20ms opponent and 65ms self.
    // For fair bot vs bot, maybe they just get 40ms each.
    Solution opp_plan = Evolution::RunGA(pods, cps_, timer, 15.0, 1 - team_id_, nullptr, BotConfig()); // Baseline for opp
    Solution our_plan = Evolution::RunGA(pods, cps_, timer, 70.0, team_id_, &opp_plan, config_);

    std::vector<PodAction> actions(2);
    for (int i = 0; i < 2; i++) {
        Action a = our_plan.moves[i][0];
        Pod p = pods[start_idx + i];

        // We simulate the decode to find the final thrust/angle it decided to use
        DecodeGeneToAction(a, p, cps_, pods, opp_start_idx, i == 0);

        double target_angle = GameEngine::NormalizeAngle(p.angle);
        double tx = p.pos.x + cos_lut[(int)target_angle] * 10000.0;
        double ty = p.pos.y + sin_lut[(int)target_angle] * 10000.0;
        
        // TURN 1 FIX: If angle is -1, output exact checkpoint to perfectly utilize the instant-snap
        if (pods[start_idx + i].angle == -1) {
            tx = cps_[pods[start_idx + i].next_cp_id].x;
            ty = cps_[pods[start_idx + i].next_cp_id].y;
        }

        // Determine what the thrust effectively was after ApplyGAAction modified pod's vel/shield state
        // To accurately send output, we just send tx,ty and the thrust/shield state.
        // If it shielded, it set shield_cd = 3 (actually ApplyGAAction would have done it).
        // Since we are extracting output for the actual game server (which Arena mirrors),
        int out_thrust = 200; // Placeholder, need to deduce from gene if we can

        if (a.gene1 > 0.95 && p.shield_cd == 0) out_thrust = -1;
        
        else if (a.gene1 < 0.3) out_thrust = 200; // Direct bot always thrusts 200
        else {
            if (a.gene3 < 0.25) out_thrust = 0;
            else if (a.gene3 > 0.75) out_thrust = 200;
            else out_thrust = 200 * ((a.gene3 - 0.25) * 2.0);
        }

        actions[i] = {tx, ty, out_thrust};
    }
    return actions;
}

bool boost_available_0 = true;
bool boost_available_1 = true;
int shield_cd_track[4] = {0, 0, 0, 0};

