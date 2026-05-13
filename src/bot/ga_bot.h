#pragma once
#include "src/engine/bot.h"

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
    static Solution RunGA(const std::vector<Pod>& base_pods, const std::vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config, int runner_idx = 0, const Solution* warm_start = nullptr);
};

// Heuristic blocker: deterministic CHASE/RAM/SHIELD state machine
struct HeuristicBlocker {
    static PodAction GetAction(const Pod& blocker, const std::vector<Pod>& pods, const std::vector<Vec2>& cps, int opp_start_idx, const BotConfig& config);
};

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
    int best_boost_cp_ = -1; // Pre-computed optimal boost segment
public:
    GABot(BotConfig config = BotConfig());
    std::string GetName() const override;
    void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) override;
    void SetRoles(int runner_idx, int blocker_idx) override { runner_idx_ = runner_idx; blocker_idx_ = blocker_idx; }
    std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override;
};
