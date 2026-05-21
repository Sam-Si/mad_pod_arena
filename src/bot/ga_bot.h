#pragma once
#include "src/engine/bot.h"
#include <vector>
#include <string>

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
