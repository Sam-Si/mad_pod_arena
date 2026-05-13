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
    static Solution RunGA(const std::vector<Pod>& base_pods, const std::vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config);
};

class GABot : public IBot {
    int laps_;
    int cp_count_;
    std::vector<Vec2> cps_;
    int team_id_;
    BotConfig config_;
public:
    GABot(BotConfig config = BotConfig());
    std::string GetName() const override;
    void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) override;
    std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override;
};
