#pragma GCC optimize("O3,inline,omit-frame-pointer,unroll-loops")

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace std;


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
    int angle;
    int next_cp_id;
    bool boost_available;
    int shield_cd;
    int timeout; // To track 100 turns limit
    int laps_completed;

    Pod();
    double Mass() const;
    void ApplyGAAction(int angle_shift, int thrust);
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

Pod::Pod() : id(0), team(0), pos(0,0), vel(0,0), angle(-1), next_cp_id(0), boost_available(true), shield_cd(0), timeout(0), laps_completed(0) {}
double Pod::Mass() const { return (shield_cd > 0) ? 10.0 : 1.0; }

void Pod::ApplyGAAction(int angle_shift, int thrust_val) {
    if (shield_cd > 0) { shield_cd--; thrust_val = 0; }
    if (thrust_val == -1) { shield_cd = 3; thrust_val = 0; }
    

    if (angle == -1) angle = 0;
    else angle = (int)GameEngine::NormalizeAngle(angle + angle_shift);

    vel.x += cos_lut[angle] * thrust_val;
    vel.y += sin_lut[angle] * thrust_val;
}

void Pod::ApplyServerAction(double tx, double ty, int thrust_val) {
    if (shield_cd > 0) { shield_cd--; thrust_val = 0; }
    if (thrust_val == -1) { shield_cd = 3; thrust_val = 0; }
    

    double target_angle = GameEngine::RadToDeg(std::atan2(ty - pos.y, tx - pos.x));

    if (angle == -1) {
        angle = std::round(GameEngine::NormalizeAngle(target_angle));
    } else {
        double diff = GameEngine::ShortestAngleDiff(angle, target_angle);
        if (diff > 18.0) diff = 18.0;
        if (diff < -18.0) diff = -18.0;
        angle = std::round(GameEngine::NormalizeAngle(angle + diff));
    }

    vel.x += cos_lut[angle] * thrust_val;
    vel.y += sin_lut[angle] * thrust_val;
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

    double delta = b * b - 4.0 * a * c;
    if (delta < 0.0) return -1.0;

    double t = (-b - std::sqrt(delta)) / (2.0 * a);
    if (t < 0.0) return -1.0;
    return t;
}

void PhysicsSimulator::ResolveCollision(Pod& p1, Pod& p2) {
    double nx = p1.pos.x - p2.pos.x;
    double ny = p1.pos.y - p2.pos.y;
    double dist = std::sqrt(nx * nx + ny * ny);
    nx /= dist; ny /= dist;

    double vx = p1.vel.x - p2.vel.x;
    double vy = p1.vel.y - p2.vel.y;
    double impact = vx * nx + vy * ny;

    if (impact >= 0.0) return;

    double m1 = p1.Mass();
    double m2 = p2.Mass();
    double mass_coeff = (m1 * m2) / (m1 + m2);

    double impulse = mass_coeff * impact * 2.0;
    if (impulse > -120.0) impulse = -120.0;

    double fx = nx * impulse;
    double fy = ny * impulse;

    p1.vel.x -= fx / m1; p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2; p2.vel.y += fy / m2;
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

        if (first_col_t < 0.0001) first_col_t = 0.0001; // Avoid infinite loops

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

struct BotConfig {
    int horizon = 6;
    int population = 50;
    double dist_weight = 1.0;
    double align_weight = 3.0;
    double block_weight = 0.0;
    double shield_penalty = 0.0;
    std::string name = "DefaultGA";
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
const int MAX_POPULATION = 100;

struct Action {
    double gene1; // Meta / Shield
    double gene2; // Steering
    double gene3; // Thrust

    void Randomize();
    void Mutate();
};

struct Solution {
    double score;
    Action moves[2][MAX_HORIZON];

    Solution();
    void Randomize(int horizon);
    void MutateFrom(const Solution& parent, int horizon);
};

class Evolution {
public:
    static double EvaluatePod(const Pod& pod, const std::vector<Vec2>& cps, int initial_cp, const BotConfig& config);
    static void ApplyBasicProxy(Pod& p, const std::vector<Vec2>& cps);
    static Solution RunGA(const std::vector<Pod>& base_pods, const std::vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config, int runner_idx);
};

class GABot : public IBot {
    int laps_;
    int cp_count_;
    std::vector<Vec2> cps_;
    int team_id_;
    int runner_idx_ = 0;
    int blocker_idx_ = 1;
    BotConfig config_;
public:
    GABot(BotConfig config = BotConfig());
    std::string GetName() const override;
    void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) override;
    void SetRoles(int runner_idx, int blocker_idx) override { runner_idx_ = runner_idx; blocker_idx_ = blocker_idx; }
    std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override;
};


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
    int next_next = (pod.next_cp_id + 1) % cps.size();
    double to_next_x = cps[next_next].x - target.x;
    double to_next_y = cps[next_next].y - target.y;
    double to_next_len = std::sqrt(to_next_x*to_next_x + to_next_y*to_next_y);
    if (to_next_len > 0.0) {
        target.x += (to_next_x / to_next_len) * 400.0;
        target.y += (to_next_y / to_next_len) * 400.0;
    }
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
        int next_next = (pod.next_cp_id + 1) % cps.size();
        double to_next_x = cps[next_next].x - target.x;
        double to_next_y = cps[next_next].y - target.y;
        double to_next_len = std::sqrt(to_next_x*to_next_x + to_next_y*to_next_y);
        if (to_next_len > 0.0) {
            target.x += (to_next_x / to_next_len) * 400.0;
            target.y += (to_next_y / to_next_len) * 400.0;
        }
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

Solution Evolution::RunGA(const vector<Pod>& base_pods, const vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config, int runner_idx) {
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
                const Pod& blocker = sim_env[start_idx + 1 - runner_idx];
                
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

            pop[i].score = EvaluatePod(sim_env[start_idx + runner_idx], cps, base_pods[start_idx + runner_idx].next_cp_id, config) + block_score;
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
    Solution opp_plan = Evolution::RunGA(pods, cps_, timer, 15.0, 1 - team_id_, nullptr, BotConfig(), 0); // Assume opp 0 is runner for now // Baseline for opp
    Solution our_plan = Evolution::RunGA(pods, cps_, timer, 70.0, team_id_, &opp_plan, config_, runner_idx_);

    std::vector<PodAction> actions(2);
    for (int i = 0; i < 2; i++) {
        Action a = our_plan.moves[i][0];
        Pod p = pods[start_idx + i];

        // We simulate the decode to find the final thrust/angle it decided to use
        DecodeGeneToAction(a, p, cps_, pods, opp_start_idx, i == runner_idx_);

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

    // Debug print
    cerr << "Laps: " << laps << endl;
    cerr << "Checkpoint Count: " << cp_count << endl;
    for (int i = 0; i < cp_count; i++) {
        cerr << "CP[" << i << "] = ("
             << cps[i].x << ", "
             << cps[i].y << ")" << endl;
    }

    BotConfig config;
    config.name = "Bot_449";
    config.horizon = 6;
    config.population = 40;
    config.dist_weight = 1.0;
    config.align_weight = 1.0;
    config.block_weight = 5.0;
    config.shield_penalty = 50.0;

    GABot bot(config);
    bot.Initialize(laps, cp_count, cps, 0);
    vector<int> pod_laps(4, 0);
    vector<int> prev_cp(4, 1);

    while (1) {
        vector<Pod> env(4);
        for (int i = 0; i < 4; i++) {
            int x, y, vx, vy, angle, next_cp_id;
            cin >> x >> y >> vx >> vy >> angle >> next_cp_id; cin.ignore();
            env[i].pos = Vec2(x, y);
            env[i].vel = Vec2(vx, vy);
            env[i].angle = angle;
            env[i].next_cp_id = next_cp_id;
            env[i].shield_cd = shield_cd_track[i];
            if (shield_cd_track[i] > 0) shield_cd_track[i]--;
        }
        env[0].boost_available = boost_available_0;
        env[1].boost_available = boost_available_1;

        cerr << "--- STATE DUMP ---" << endl;
        for (int i = 0; i < 4; i++) {
            cerr << "Pod " << i << ": Pos(" << env[i].pos.x << ", " << env[i].pos.y 
                 << ") Vel(" << env[i].vel.x << ", " << env[i].vel.y 
                 << ") Angle: " << env[i].angle << " NextCP: " << env[i].next_cp_id << endl;
        }

        for (int i = 0; i < 4; i++) {
            if (env[i].next_cp_id == 1 && prev_cp[i] == cp_count - 1) pod_laps[i]++;
            prev_cp[i] = env[i].next_cp_id;
        }

        // Determine who is the runner dynamically
        int runner_idx = 0;
        int blocker_idx = 1;
        
        double score0 = pod_laps[0] * 50000 + env[0].next_cp_id * 1000 - env[0].pos.Distance(cps[env[0].next_cp_id]);
        double score1 = pod_laps[1] * 50000 + env[1].next_cp_id * 1000 - env[1].pos.Distance(cps[env[1].next_cp_id]);
        
        if (score1 > score0 + 1500) { // Add hysteresis to prevent flickering
            runner_idx = 1;
            blocker_idx = 0;
        }
        
        // Let the GA know who the runner is
        bot.SetRoles(runner_idx, blocker_idx);
        
        vector<PodAction> actions = bot.GetActions(env);


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
            
            // TURN 1 FORCED ACCELERATION
            if (env[i].angle == -1 && out_thrust != -1) {
                out_thrust = 200; // Always blast 200 on Turn 1 if not shielded
            }

            if (use_boost) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " BOOST" << endl;
            } else if (out_thrust == -1) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " SHIELD" << endl;
                shield_cd_track[i] = 3;
            } else {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " " << out_thrust << endl;
            }
        }
    }
}
