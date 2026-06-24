import re

with open('src/cg/cg_bot.cpp', 'r') as f:
    content = f.read()

# Insert GenerateHeuristicOpponentModel right before GABot::GetActions
heuristic_func = """
static Solution GenerateHeuristicOpponentModel(const vector<Pod>& base_pods, const SimCtx& ctx, int horizon) {
    Solution sol;
    Pod opp_sim[4];
    std::memcpy(opp_sim, base_pods.data(), 4 * sizeof(Pod));

    int opp_run_idx = ctx.opp_start_idx;
    int opp_block_idx = ctx.opp_start_idx + 1;
    const auto& cps = *ctx.cps;
    
    int opp0_lin = opp_sim[opp_run_idx].laps_completed * ctx.cp_count + opp_sim[opp_run_idx].next_cp_id;
    int opp1_lin = opp_sim[opp_block_idx].laps_completed * ctx.cp_count + opp_sim[opp_block_idx].next_cp_id;
    double opp0_d = opp_sim[opp_run_idx].pos.Distance(cps[opp_sim[opp_run_idx].next_cp_id]);
    double opp1_d = opp_sim[opp_block_idx].pos.Distance(cps[opp_sim[opp_block_idx].next_cp_id]);
    int opp_runner = opp_run_idx;
    if (opp1_lin > opp0_lin || (opp0_lin == opp1_lin && opp1_d < opp0_d)) {
        opp_runner = opp_block_idx;
    }

    sol.runner_shield_step = MAX_HORIZON;
    sol.blocker_shield_step = MAX_HORIZON;

    for (int t = 0; t < horizon; ++t) {
        Vec2 cp_target = cps[opp_sim[opp_runner].next_cp_id];
        double dx_r = cp_target.x - opp_sim[opp_runner].pos.x;
        double dy_r = cp_target.y - opp_sim[opp_runner].pos.y;
        double target_angle_r = GameEngine::RadToDeg(atan2(dy_r, dx_r));
        double shift_r = GameEngine::ShortestAngleDiff(opp_sim[opp_runner].angle, target_angle_r);
        shift_r = std::max(-18.0, std::min(18.0, shift_r));
        int thrust_r = 200;

        sol.runner_moves[t] = {shift_r, thrust_r};

        opp_sim[opp_runner].ApplyGAAction(shift_r, thrust_r);
        opp_sim[opp_runner].pos.x += opp_sim[opp_runner].vel.x;
        opp_sim[opp_runner].pos.y += opp_sim[opp_runner].vel.y;
        opp_sim[opp_runner].vel.x = trunc(opp_sim[opp_runner].vel.x * 0.85);
        opp_sim[opp_runner].vel.y = trunc(opp_sim[opp_runner].vel.y * 0.85);
        opp_sim[opp_runner].pos.x = round(opp_sim[opp_runner].pos.x);
        opp_sim[opp_runner].pos.y = round(opp_sim[opp_runner].pos.y);

        if (opp_sim[opp_runner].pos.DistanceSq(cps[opp_sim[opp_runner].next_cp_id]) <= 360000) {
            opp_sim[opp_runner].next_cp_id = (opp_sim[opp_runner].next_cp_id + 1) % ctx.cp_count;
        }
    }
    return sol;
}

"""

get_actions_idx = content.find("vector<PodAction> GABot::GetActions")
content = content[:get_actions_idx] + heuristic_func + content[get_actions_idx:]

with open('src/cg/cg_bot.cpp', 'w') as f:
    f.write(content)

