import re

with open('src/cg/cg_bot.cpp', 'r') as f:
    content = f.read()

# We need to replace the multi-stage execution block in GetActions.
# The block starts around line 1550 with "Solution opp_sol = RunGAParallel"
# and ends with "prev_best_ = best;"

pattern = r"(Solution opp_sol = RunGAParallel\(.*?has_prev_best_ = true;)"

replacement = """Solution opp_sol;
    if (tight_budget) {
        opp_sol = GenerateHeuristicOpponentModel(pods, ctx, config_.horizon);
        for (int t = 0; t < MAX_HORIZON; ++t) {
            ctx.opp_moves[t] = opp_sol.runner_moves[t];
        }
    } else {
        opp_sol = RunGAParallel(pods, timer, t0_limit, opp_ctx, opp_config, nullptr);
        
        // Populate prediction moves for Stage 1 using Stage 0 opponent runner moves
        for (int t = 0; t < MAX_HORIZON; ++t) {
            ctx.opp_moves[t] = opp_sol.runner_moves[t];
        }
    }

    Solution baseline_sol;
    if (!tight_budget) {
        baseline_sol = RunGAParallel(pods, timer, t1_limit, ctx, config_, has_prev_best_ ? &prev_best_ : nullptr);

        // === STAGE 2: ADAPTIVE OPPONENT PREDICTION ===
        Solution adaptive_opp_sol;
        for (int t = 0; t < MAX_HORIZON; ++t) {
            opp_ctx.opp_moves[t] = baseline_sol.runner_moves[t];
        }
        adaptive_opp_sol = RunGAParallel(pods, timer, t2_limit, opp_ctx, opp_config, nullptr, &opp_sol);
        
        // Populate prediction moves using the adaptive opponent moves
        for (int t = 0; t < MAX_HORIZON; ++t) {
            ctx.opp_moves[t] = adaptive_opp_sol.runner_moves[t];
        }
    }

    // === STAGE 3: FINAL OUR RESPONSE ===
    Solution best = RunGAParallel(pods, timer, total_budget, ctx, config_,
                                  has_prev_best_ ? &prev_best_ : nullptr,
                                  tight_budget ? nullptr : &baseline_sol);

    prev_best_ = best;
    has_prev_best_ = true;"""

new_content = re.sub(pattern, replacement, content, flags=re.DOTALL)

with open('src/cg/cg_bot.cpp', 'w') as f:
    f.write(new_content)

