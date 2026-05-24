#pragma once

#include <vector>
#include <string>
#include <memory>
#include "src/engine/bot.h"

// =============================================================================
// CG Bot Wrapper
//
// Adapts the monolithic cg_bot.cpp (the CodinGame submission file) to the
// engine's IBot interface so it can be used in local benchmarks.
//
// Technique: rename every symbol that collides with src/engine/ via #define,
// include cg_bot.cpp, then #undef.  This is the same approach used by
// legacy_wrapper.h for the legacy bot.
// =============================================================================

// --- Rename colliding symbols from cg_bot.cpp ---------------------------------
#define PI                CG_PI
#define cos_lut           cg_cos_lut
#define sin_lut           cg_sin_lut
#define xor_state         cg_xor_state
#define g_friendly_collision cg_g_friendly_collision
#define g_runner_id       cg_g_runner_id
#define InitLUT           CG_InitLUT
#define FastRand          CG_FastRand
#define FastRandInt       CG_FastRandInt
#define Timer             CG_Timer
#define Vec2              CG_Vec2
#define GameEngine        CG_GameEngine
#define PodAction         CG_PodAction
#define Pod               CG_Pod
#define PhysicsSimulator  CG_PhysicsSimulator
#define BotConfig         CG_BotConfig
#define IBot              CG_IBot
#define GABot             CG_GABot
#define Action            CG_Action
#define Solution          CG_Solution
#define Evolution         CG_Evolution
#define MakeGoToTarget    CG_MakeGoToTarget
#define main              cg_main

#include "cg/cg_bot.cpp"

#undef main
#undef MakeGoToTarget
#undef Evolution
#undef Solution
#undef Action
#undef GABot
#undef IBot
#undef BotConfig
#undef PhysicsSimulator
#undef Pod
#undef PodAction
#undef GameEngine
#undef Vec2
#undef Timer
#undef FastRandInt
#undef FastRand
#undef InitLUT
#undef g_runner_id
#undef g_friendly_collision
#undef xor_state
#undef sin_lut
#undef cos_lut
#undef PI

// Provide definition for the extern-declared but unused g_runner_id
thread_local int cg_g_runner_id = 0;

// --- Adapter class: maps CG types to engine types ----------------------------

class CGBotWrapper : public ::IBot {
    CG_GABot bot_;

public:
    CGBotWrapper() {
        CG_InitLUT();
    }

    std::string GetName() const override {
        return "CGBot";
    }

    void Initialize(int laps, int cp_count, const std::vector<::Vec2>& cps,
                    int team_id) override {
        std::vector<CG_Vec2> cg_cps(cp_count);
        for (int i = 0; i < cp_count; ++i) {
            cg_cps[i] = CG_Vec2(cps[i].x, cps[i].y);
        }
        bot_.Initialize(laps, cp_count, cg_cps, team_id);
    }

    std::vector<::PodAction> GetActions(const std::vector<::Pod>& pods) override {
        std::vector<CG_Pod> cg_pods(4);
        for (int i = 0; i < 4; ++i) {
            cg_pods[i].id             = pods[i].id;
            cg_pods[i].team           = pods[i].team;
            cg_pods[i].pos            = CG_Vec2(pods[i].pos.x, pods[i].pos.y);
            cg_pods[i].vel            = CG_Vec2(pods[i].vel.x, pods[i].vel.y);
            cg_pods[i].angle          = pods[i].angle;
            cg_pods[i].next_cp_id     = pods[i].next_cp_id;
            cg_pods[i].boost_available = pods[i].boost_available;
            cg_pods[i].shield_cd      = pods[i].shield_cd;
            cg_pods[i].timeout        = pods[i].timeout;
            cg_pods[i].laps_completed = pods[i].laps_completed;
        }

        std::vector<CG_PodAction> cg_actions = bot_.GetActions(cg_pods);

        std::vector<::PodAction> actions(2);
        for (int i = 0; i < 2; ++i) {
            actions[i].tx     = cg_actions[i].tx;
            actions[i].ty     = cg_actions[i].ty;
            actions[i].thrust = cg_actions[i].thrust;
        }
        return actions;
    }
};
