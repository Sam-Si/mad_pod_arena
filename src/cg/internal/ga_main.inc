#ifndef CG_BOT_NO_MAIN
int main() {
    InitLUT();
    
    // Log platform hardware threads to confirm single-core environment constraints
    cerr << "Hardware Concurrency (CPU Cores): " << std::thread::hardware_concurrency() << endl;
    
    int laps;
    cin >> laps; cin.ignore();
    int cp_count;
    cin >> cp_count; cin.ignore();
    vector<Vec2> cps(cp_count);
    for (int i = 0; i < cp_count; i++) {
        cin >> cps[i].x >> cps[i].y; cin.ignore();
    }

    cerr << "Laps: " << laps << endl;
    cerr << "Checkpoint Count: " << cp_count << endl;
    for (int i = 0; i < cp_count; i++) {
        cerr << "CP[" << i << "] = ("
             << cps[i].x << ", "
             << cps[i].y << ")" << endl;
    }

    BotConfig config;
    config.name = "DefaultGA";
    config.horizon = 6;
    config.population = 48;
    config.dist_weight = 1.5;
    config.align_weight = 3.0;
    config.speed_bonus = 0.5;
    config.lateral_penalty = 0.5;
    config.angle_penalty = 25;
    config.corner_cut_dist = 300;
    config.block_weight = 1.0;
    config.shield_penalty = 50;
    config.shield_ram_dist = 850;
    config.opp_penalty = 0.5;
    config.opp_model_ms = 0;

    GABot bot(config);
    GABot::verbose = true;
    bot.Initialize(laps, cp_count, cps, 0);
    vector<int> pod_laps(4, 0);
    vector<int> prev_cp(4, 1);

    bool has_prediction = false;
    vector<Pod> predicted(4);

    vector<OutputAction> last_output(2);
    bool pred_had_opp_collision[2] = {false, false};
    double max_pos_err_clean = 0, max_vel_err_clean = 0;
    int verify_turn = 0;
    int shield_cd_track[4] = {0, 0, 0, 0};
    bool boost_available_0 = true;
    bool boost_available_1 = true;

    while (1) {
        vector<Pod> env(4);
        for (int i = 0; i < 4; i++) {
            int x, y, vx, vy, angle, next_cp_id;
            cin >> x >> y >> vx >> vy >> angle >> next_cp_id; cin.ignore();
            env[i].pos = Vec2(x, y);
            env[i].vel = Vec2(vx, vy);
            env[i].angle = (double)angle;
            env[i].next_cp_id = next_cp_id;
            
            if (angle < 0) {
                Vec2 target_cp = cps[next_cp_id];
                double rad = std::atan2(target_cp.y - env[i].pos.y, target_cp.x - env[i].pos.x);
                env[i].angle = rad * 180.0 / PI;
                if (env[i].angle < 0) env[i].angle += 360.0;
            }
            env[i].id = i; // Initialize ID to index!
            env[i].shield_cd = shield_cd_track[i];
            if (shield_cd_track[i] > 0) shield_cd_track[i]--;
        }
        env[0].boost_available = boost_available_0;
        env[1].boost_available = boost_available_1;
        
        // Track laps manually so GA role assignment works
        for (int i = 0; i < 4; i++) {
            if (env[i].next_cp_id == 1 && prev_cp[i] == cp_count - 1) pod_laps[i]++;
            prev_cp[i] = env[i].next_cp_id;
            env[i].laps_completed = pod_laps[i];
        }

        if (has_prediction) {
            verify_turn++;
            cerr << "--- PHYSICS VERIFY T" << verify_turn << " ---" << endl;
            bool any_our_bug = false;
            for (int i = 0; i < 2; i++) {
                double pos_dx = env[i].pos.x - predicted[i].pos.x;
                double pos_dy = env[i].pos.y - predicted[i].pos.y;
                double pos_err = std::sqrt(pos_dx*pos_dx + pos_dy*pos_dy);
                double vel_dx = env[i].vel.x - predicted[i].vel.x;
                double vel_dy = env[i].vel.y - predicted[i].vel.y;
                double vel_err = std::sqrt(vel_dx*vel_dx + vel_dy*vel_dy);
                double angle_err = 0;
                if (env[i].angle >= 0 && predicted[i].angle >= 0) {
                    angle_err = std::abs(GameEngine::ShortestAngleDiff((int)env[i].angle, (int)std::round(predicted[i].angle)));
                }

                bool had_opp_col = pred_had_opp_collision[i];
                
                if (pos_err > 0.5 || vel_err > 0.5 || angle_err > 0.5) {
                    if (had_opp_col) {
                        cerr << "  Pod" << i << " COLLISION-EXPECTED: pos_err=" << fixed << setprecision(1) 
                             << pos_err << " vel_err=" << vel_err << endl;
                    } else {
                        any_our_bug = true;
                        cerr << "  *** Pod" << i << " PHYSICS BUG ***:" << endl;
                        cerr << "    Pos: predicted(" << (int)predicted[i].pos.x << "," << (int)predicted[i].pos.y 
                             << ") actual(" << (int)env[i].pos.x << "," << (int)env[i].pos.y 
                             << ") err=" << pos_err << endl;
                        cerr << "    Vel: predicted(" << (int)predicted[i].vel.x << "," << (int)predicted[i].vel.y 
                             << ") actual(" << (int)env[i].vel.x << "," << (int)env[i].vel.y 
                             << ") err=" << vel_err << endl;
                        if (angle_err > 0) {
                            cerr << "    Angle: predicted=" << (int)predicted[i].angle 
                                 << " actual=" << (int)env[i].angle 
                                 << " err=" << angle_err << endl;
                        }
                        max_pos_err_clean = std::max(max_pos_err_clean, pos_err);
                        max_vel_err_clean = std::max(max_vel_err_clean, vel_err);
                    }
                }
            }
            if (!any_our_bug) {
                cerr << "  OUR PODS OK (max_clean_pos=" << fixed << setprecision(1) << max_pos_err_clean 
                     << " max_clean_vel=" << max_vel_err_clean << ")" << endl;
            }
        }

        cerr << "--- STATE DUMP ---" << endl;
        for (int i = 0; i < 4; i++) {
            cerr << "Pod " << i << ": Pos(" << env[i].pos.x << ", " << env[i].pos.y 
                 << ") Vel(" << env[i].vel.x << ", " << env[i].vel.y 
                 << ") Angle: " << env[i].angle << " NextCP: " << env[i].next_cp_id << endl;
        }

        // Let GABot compute actions for both pods (runner GA, blocker heuristic)
        vector<PodAction> actions = bot.GetActions(env);
        
        int runner_idx = 0;
        int blocker_idx = 1;
        double score0 = pod_laps[0] * 50000 + env[0].next_cp_id * 1000 - env[0].pos.Distance(cps[env[0].next_cp_id]);
        double score1 = pod_laps[1] * 50000 + env[1].next_cp_id * 1000 - env[1].pos.Distance(cps[env[1].next_cp_id]);
        if (score1 > score0 + 1500) {
            runner_idx = 1;
            blocker_idx = 0;
        }

        for (int i = 0; i < 2; i++) {
            bool use_boost = (actions[i].thrust == 650);
            if (use_boost) {
                if (i == 0) boost_available_0 = false;
                else boost_available_1 = false;
            }

            int out_thrust = actions[i].thrust;
            
            if (env[i].angle < 0 && out_thrust != -1) {
                out_thrust = 200;
            }

            last_output[i].tx = (int)actions[i].tx;
            last_output[i].ty = (int)actions[i].ty;
            last_output[i].was_boost = use_boost;
            last_output[i].was_shield = (out_thrust == -1);
            last_output[i].thrust = use_boost ? 650 : out_thrust;

            string msg = GetTrollMessage(i, runner_idx, blocker_idx, env, last_output[i], cp_count);
            if (use_boost) {
                cout << last_output[i].tx << " " << last_output[i].ty << " BOOST " << msg << endl;
                shield_cd_track[i] = 0;
            } else if (out_thrust == -1) {
                cout << last_output[i].tx << " " << last_output[i].ty << " SHIELD " << msg << endl;
                shield_cd_track[i] = 4;
            } else {
                cout << last_output[i].tx << " " << last_output[i].ty << " " << out_thrust << " " << msg << endl;
            }
        }
        
        // Predict next state
        predicted = env;
        for (int i = 0; i < 2; i++) {
            if (last_output[i].was_shield) {
                predicted[i].shield_cd = 4;
            }
        }
        
        {
            int thrust0 = last_output[0].thrust;
            if (last_output[0].was_shield) thrust0 = -1;
            predicted[0].ApplyServerAction((double)last_output[0].tx, (double)last_output[0].ty, thrust0);
        }
        {
            int thrust1 = last_output[1].thrust;
            if (last_output[1].was_shield) thrust1 = -1;
            predicted[1].ApplyServerAction((double)last_output[1].tx, (double)last_output[1].ty, thrust1);
        }
        
        Evolution::ApplyBasicProxy(predicted[2], cps);
        Evolution::ApplyBasicProxy(predicted[3], cps);
        
        pred_had_opp_collision[0] = false;
        pred_had_opp_collision[1] = false;
        
        FastSimulateTurn(predicted.data());
        

        for (int our = 0; our < 2; our++) {
            for (int opp = 2; opp < 4; opp++) {
                double dx = predicted[our].pos.x - predicted[opp].pos.x;
                double dy = predicted[our].pos.y - predicted[opp].pos.y;
                double dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 1200.0) {
                    pred_had_opp_collision[our] = true;
                }
                dx = env[our].pos.x - env[opp].pos.x;
                dy = env[our].pos.y - env[opp].pos.y;
                dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 1600.0) {
                    pred_had_opp_collision[our] = true;
                }
            }
        }
        
        for (int p = 0; p < 4; p++) {
            if (predicted[p].pos.DistanceSq(cps[predicted[p].next_cp_id]) <= 360000) {
                predicted[p].next_cp_id = (predicted[p].next_cp_id + 1) % cp_count;
            }
        }
        
        has_prediction = true;
    }
}
#endif // CG_BOT_NO_MAIN
