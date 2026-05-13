import re

with open("cg_bot.cpp", "r") as f:
    text = f.read()

# 1. Update Map Logging
map_log_old = """    vector<Vec2> cps(cp_count);
    for (int i = 0; i < cp_count; i++) {
        cin >> cps[i].x >> cps[i].y; cin.ignore();
    }"""
map_log_new = """    vector<Vec2> cps(cp_count);
    for (int i = 0; i < cp_count; i++) {
        cin >> cps[i].x >> cps[i].y; cin.ignore();
    }

    // Debug print
    cerr << "Laps: " << laps << endl;
    cerr << "Checkpoint Count: " << cp_count << endl;
    for (int i = 0; i < cp_count; i++) {
        cerr << "CP[" << i << "] = ("
             << cps[i].x << ", "
             << cps[i].y << ")" << endl;
    }"""
text = text.replace(map_log_old, map_log_new)

# 2. Turn 1 Forced Acceleration
turn_1_old = """            if (use_boost) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " BOOST" << endl;
            } else if (actions[i].thrust == -1) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " SHIELD" << endl;
                shield_cd_track[i] = 3;
            } else {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " " << actions[i].thrust << endl;
            }"""
turn_1_new = """            int out_thrust = actions[i].thrust;
            
            // TURN 1 FORCED ACCELERATION
            if (env[i].angle == -1 && out_thrust != -1) {
                out_thrust = 200; // Always blast 200 on Turn 1 if not shielded
            }

            if (use_boost) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " BOOST" << endl;
            } else if (out_thrust == -1) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " SHIELD" << endl;
                shield_cd_track[i] = 3;
            } else {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " " << out_thrust << endl;
            }"""
text = text.replace(turn_1_old, turn_1_new)

# 3. Corner Cutting
corner_cut_old = """        // Direct bot to next CP
        Vec2 target = cps[pod.next_cp_id];
        double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - pod.pos.y, target.x - pod.pos.x));"""
corner_cut_new = """        // Direct bot to next CP
        Vec2 target = cps[pod.next_cp_id];
        int next_next = (pod.next_cp_id + 1) % cps.size();
        Vec2 to_next = cps[next_next] - target;
        double to_next_len = std::sqrt(to_next.x*to_next.x + to_next.y*to_next.y);
        if (to_next_len > 0.0) {
            target.x += (to_next.x / to_next_len) * 400.0;
            target.y += (to_next.y / to_next_len) * 400.0;
        }
        double desired_angle = GameEngine::RadToDeg(std::atan2(target.y - pod.pos.y, target.x - pod.pos.x));"""
text = text.replace(corner_cut_old, corner_cut_new)

with open("cg_bot.cpp", "w") as f:
    f.write(text)
