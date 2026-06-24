#pragma once
#include "src/engine/engine.h"
#include <vector>
#include <string>
#include <thread>

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

    double runner_bypass_weight = 20.0;
    double blocker_stay_in_front_weight = 30.0;
    double blocker_facing_weight = 30.0;
    double runner_evasion_weight = 1.0;

    // Time allocation (configurable for benchmarking)
    double turn_time_limit_ms = 75.0;        // Per-turn time budget
    double first_turn_time_limit_ms = 1000.0; // First turn time budget

    int num_threads = std::max(1, (int)std::thread::hardware_concurrency());

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
