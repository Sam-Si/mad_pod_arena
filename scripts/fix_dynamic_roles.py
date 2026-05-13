with open("cg_bot.cpp", "r") as f:
    text = f.read()

# Add logging
log_str = """    for (int i = 0; i < cp_count; i++) {
        int checkpoint_x, checkpoint_y;
        cin >> checkpoint_x >> checkpoint_y; cin.ignore();
        cps[i] = Vec2(checkpoint_x, checkpoint_y);
    }
    
    // Debug print map
    cerr << "Laps: " << laps << endl;
    cerr << "Checkpoint Count: " << cp_count << endl;
    for (int i = 0; i < cp_count; i++) {
        cerr << "CP[" << i << "] = (" << cps[i].x << ", " << cps[i].y << ")" << endl;
    }
"""
text = text.replace("""    for (int i = 0; i < cp_count; i++) {
        int checkpoint_x, checkpoint_y;
        cin >> checkpoint_x >> checkpoint_y; cin.ignore();
        cps[i] = Vec2(checkpoint_x, checkpoint_y);
    }""", log_str)

# Change is_runner to be dynamic
# Wait, we need to know how many CPs they passed. But CodinGame only gives next_cp_id.
# We don't know the exact lap unless we track it over turns!
# However, we can just use the baseline rank heuristic for the current turn:
# Actually, the user just asked about it. Since tracking exact laps requires state, I can add a simple state tracker.
# Let's just track laps in main().
lap_tracker = """    vector<int> pod_laps(4, 0);
    vector<int> prev_cp(4, 1);

    // game loop
    while (1) {"""

text = text.replace("    // game loop\n    while (1) {", lap_tracker)

role_logic = """        for (int i = 0; i < 4; i++) {
            if (env[i].next_cp_id == 1 && prev_cp[i] == cp_count - 1) pod_laps[i]++;
            prev_cp[i] = env[i].next_cp_id;
        }

        // Determine who is the runner dynamically
        int runner_idx = 0;
        int blocker_idx = 1;
        
        double score0 = pod_laps[0] * 50000 + env[0].next_cp_id * 1000 - env[0].pos.Distance(cps[env[0].next_cp_id]);
        double score1 = pod_laps[1] * 50000 + env[1].next_cp_id * 1000 - env[1].pos.Distance(cps[env[1].next_cp_id]);
        
        if (score1 > score0 + 1500) { // Add hysteresis to prevent flickering
            runner_idx = 1;
            blocker_idx = 0;
        }
        
        // Let the GA know who the runner is
        bot.SetRoles(runner_idx, blocker_idx);
        
        vector<PodAction> actions = bot.GetActions(env);
"""

# Wait, we need to add SetRoles to GABot.
# Let's just modify GetActions to evaluate roles.
old_get_actions = "        vector<PodAction> actions = bot.GetActions(env);"

text = text.replace(old_get_actions, role_logic)

with open("cg_bot.cpp", "w") as f:
    f.write(text)

