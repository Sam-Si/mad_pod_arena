with open("cg_bot.cpp", "r") as f:
    text = f.read()

import re

# Add SetRoles to IBot and GABot
text = text.replace("virtual std::vector<PodAction> GetActions(const std::vector<Pod>& pods) = 0;", 
                    "virtual std::vector<PodAction> GetActions(const std::vector<Pod>& pods) = 0;\n    virtual void SetRoles(int runner_idx, int blocker_idx) {}")

text = text.replace("void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) override;",
                    "void Initialize(int laps, int cp_count, const std::vector<Vec2>& cps, int team_id) override;\n    void SetRoles(int runner_idx, int blocker_idx) override { runner_idx_ = runner_idx; blocker_idx_ = blocker_idx; }")

text = text.replace("int team_id_;", "int team_id_;\n    int runner_idx_ = 0;\n    int blocker_idx_ = 1;")

# Pass runner_idx_ and blocker_idx_ to RunGA or EvaluatePod?
# Let's pass it to RunGA
text = text.replace("static Solution RunGA(const std::vector<Pod>& base_pods, const std::vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config);",
                    "static Solution RunGA(const std::vector<Pod>& base_pods, const std::vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config, int runner_idx);")

# Update declaration and usage of RunGA in GABot::GetActions
text = text.replace("Solution opp_plan = Evolution::RunGA(pods, cps_, timer, 15.0, 1 - team_id_, nullptr, BotConfig());",
                    "Solution opp_plan = Evolution::RunGA(pods, cps_, timer, 15.0, 1 - team_id_, nullptr, BotConfig(), 0); // Assume opp 0 is runner for now")

text = text.replace("Solution our_plan = Evolution::RunGA(pods, cps_, timer, 70.0, team_id_, &opp_plan, config_);",
                    "Solution our_plan = Evolution::RunGA(pods, cps_, timer, 70.0, team_id_, &opp_plan, config_, runner_idx_);")

# Update Evolution::RunGA implementation signature
text = text.replace("Solution Evolution::RunGA(const vector<Pod>& base_pods, const vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config) {",
                    "Solution Evolution::RunGA(const vector<Pod>& base_pods, const vector<Vec2>& cps, Timer& timer, double time_limit_ms, int target_team, const Solution* enemy_plan, const BotConfig& config, int runner_idx) {")

# In RunGA, we evaluate the pods.
# Start idx is `target_team * 2`.
# The runner is `start_idx + runner_idx`.
# The blocker is `start_idx + (1 - runner_idx)`.
text = text.replace("EvaluatePod(sim_env[start_idx], cps, base_pods[start_idx].next_cp_id, config) + block_score;",
                    "EvaluatePod(sim_env[start_idx + runner_idx], cps, base_pods[start_idx + runner_idx].next_cp_id, config) + block_score;")

text = text.replace("const Pod& blocker = sim_env[start_idx + 1];",
                    "const Pod& blocker = sim_env[start_idx + 1 - runner_idx];")

# Make sure DecodeGeneToAction uses the correct runner
# Wait, GABot::GetActions needs to decode with the right runner flag
text = text.replace("DecodeGeneToAction(a, p, cps_, pods, opp_start_idx, i == 0);",
                    "DecodeGeneToAction(a, p, cps_, pods, opp_start_idx, i == runner_idx_);")

# In RunGA decoding:
text = text.replace("DecodeGeneToAction(pop[i].actions[t * 2 + p], sim_env[start_idx + p], cps, sim_env, opp_start_idx, p == 0);",
                    "DecodeGeneToAction(pop[i].actions[t * 2 + p], sim_env[start_idx + p], cps, sim_env, opp_start_idx, p == runner_idx);")

with open("cg_bot.cpp", "w") as f:
    f.write(text)

