#pragma once
#include "src/engine/bot.h"

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
