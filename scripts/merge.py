import re

files = ["engine.h", "engine.cpp", "bot.h", "ga_bot.h", "ga_bot.cpp"]

includes = set()
code = []

for f in files:
    with open(f, 'r') as file:
        for line in file:
            line = line.rstrip()
            if line.startswith("#pragma once"):
                continue
            if line.startswith("#include"):
                if "arena.h" in line or "engine.h" in line or "bot.h" in line or "ga_bot.h" in line:
                    continue
                includes.add(line)
                continue
            code.append(line)

final_code = "#pragma GCC optimize(\"O3,inline,omit-frame-pointer,unroll-loops\")\n\n"
for inc in sorted(list(includes)):
    final_code += inc + "\n"

final_code += "\nusing namespace std;\n\n"

for line in code:
    if line == "using namespace std;":
        continue
    final_code += line + "\n"

# Append the CodinGame main loop
main_loop = """
bool boost_available_0 = true;
bool boost_available_1 = true;

int main() {
    InitLUT();
    
    int laps;
    cin >> laps; cin.ignore();
    int cp_count;
    cin >> cp_count; cin.ignore();
    vector<Vec2> cps(cp_count);
    for (int i = 0; i < cp_count; i++) {
        cin >> cps[i].x >> cps[i].y; cin.ignore();
    }

    BotConfig config;
    config.name = "Bot_848";
    config.horizon = 6;
    config.population = 20;
    config.dist_weight = 2.0;
    config.align_weight = 3.0;
    config.block_weight = 0.0;
    config.shield_penalty = 50.0;

    GABot bot(config);
    bot.Initialize(laps, cp_count, cps, 0);

    while (1) {
        vector<Pod> env(4);
        for (int i = 0; i < 4; i++) {
            int x, y, vx, vy, angle, next_cp_id;
            cin >> x >> y >> vx >> vy >> angle >> next_cp_id; cin.ignore();
            env[i].pos = Vec2(x, y);
            env[i].vel = Vec2(vx, vy);
            env[i].angle = angle;
            env[i].next_cp_id = next_cp_id;
            // Shield CD management in actual CG is hidden, but we assume 0 here initially
            // You can infer shield CD if you track previous turns. 
        }
        env[0].boost_available = boost_available_0;
        env[1].boost_available = boost_available_1;

        vector<PodAction> actions = bot.GetActions(env);

        for (int i = 0; i < 2; i++) {
            if (actions[i].thrust == 650) {
                cout << (int)actions[i].target_x << " " << (int)actions[i].target_y << " BOOST" << endl;
                if (i == 0) boost_available_0 = false;
                else boost_available_1 = false;
            } else if (actions[i].thrust == -1) {
                cout << (int)actions[i].target_x << " " << (int)actions[i].target_y << " SHIELD" << endl;
            } else {
                cout << (int)actions[i].target_x << " " << (int)actions[i].target_y << " " << actions[i].thrust << endl;
            }
        }
    }
}
"""

final_code += main_loop

with open("cg_bot.cpp", "w") as f:
    f.write(final_code)
