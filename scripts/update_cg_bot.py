with open("cg_bot.cpp", "r") as f:
    text = f.read()

# 1. Add state dump
old_state_loop = """        env[0].boost_available = boost_available_0;
        env[1].boost_available = boost_available_1;

        vector<PodAction> actions = bot.GetActions(env);"""

new_state_loop = """        env[0].boost_available = boost_available_0;
        env[1].boost_available = boost_available_1;

        cerr << "--- STATE DUMP ---" << endl;
        for (int i = 0; i < 4; i++) {
            cerr << "Pod " << i << ": Pos(" << env[i].pos.x << ", " << env[i].pos.y 
                 << ") Vel(" << env[i].vel.x << ", " << env[i].vel.y 
                 << ") Angle: " << env[i].angle << " NextCP: " << env[i].next_cp_id << endl;
        }

        vector<PodAction> actions = bot.GetActions(env);"""

text = text.replace(old_state_loop, new_state_loop)

# 2. Update evaluate pod
old_eval = """    Vec2 dir(target.x - pod.pos.x, target.y - pod.pos.y);
    double dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
    if (dir_len > 0) {
        double dot = (pod.vel.x * (dir.x/dir_len)) + (pod.vel.y * (dir.y/dir_len));
        score += dot * config.align_weight;
    }"""

new_eval = """    Vec2 dir(target.x - pod.pos.x, target.y - pod.pos.y);
    double dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
    if (dir_len > 0) {
        double dot = (pod.vel.x * (dir.x/dir_len)) + (pod.vel.y * (dir.y/dir_len));
        score += dot * config.align_weight;
        
        // Orbital penalty: penalize velocity that is perpendicular to the target
        double cross = (pod.vel.x * (dir.y/dir_len)) - (pod.vel.y * (dir.x/dir_len));
        score -= std::abs(cross) * 0.5; // Punish drifting sideways to prevent orbiting
    }"""

text = text.replace(old_eval, new_eval)

with open("cg_bot.cpp", "w") as f:
    f.write(text)

