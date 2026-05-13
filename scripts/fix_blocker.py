with open("cg_bot.cpp", "r") as f:
    text = f.read()

import re

old_eval = """            // Incorporate blocking/avoidance logic
            double block_score = 0;
            if (config.block_weight > 0) {
                // Try to make the second pod block the opponent's best pod
                double dist_to_opp = sim_env[start_idx + 1].pos.Distance(sim_env[opp_start_idx].pos);
                block_score = -dist_to_opp * config.block_weight;
            }

            pop[i].score = EvaluatePod(sim_env[start_idx], cps, base_pods[start_idx].next_cp_id, config) +
                           EvaluatePod(sim_env[start_idx + 1], cps, base_pods[start_idx + 1].next_cp_id, config) + block_score;"""

new_eval = """            // Incorporate blocking/avoidance logic
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

            pop[i].score = EvaluatePod(sim_env[start_idx], cps, base_pods[start_idx].next_cp_id, config) + block_score;"""

text = text.replace(old_eval, new_eval)

with open("cg_bot.cpp", "w") as f:
    f.write(text)

