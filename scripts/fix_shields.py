with open("cg_bot.cpp", "r") as f:
    text = f.read()

import re

old_main_vars = """bool boost_available_0 = true;
bool boost_available_1 = true;

int main() {"""

new_main_vars = """bool boost_available_0 = true;
bool boost_available_1 = true;
int shield_cd_track[4] = {0, 0, 0, 0};

int main() {"""

text = text.replace(old_main_vars, new_main_vars)


old_shield_cd = """            env[i].next_cp_id = next_cp_id;
            // Shield CD management in actual CG is hidden, but we assume 0 here initially
            // You can infer shield CD if you track previous turns. 
        }"""

new_shield_cd = """            env[i].next_cp_id = next_cp_id;
            env[i].shield_cd = shield_cd_track[i];
            if (shield_cd_track[i] > 0) shield_cd_track[i]--;
        }"""

text = text.replace(old_shield_cd, new_shield_cd)

old_out_thrust = """        if (a.gene1 > 0.95) out_thrust = -1;
        else if (a.gene3 > 0.95 && p.boost_available) out_thrust = 650;"""

new_out_thrust = """        if (a.gene1 > 0.95 && p.shield_cd == 0) out_thrust = -1;
        else if (a.gene3 > 0.95 && p.boost_available) out_thrust = 650;"""

text = text.replace(old_out_thrust, new_out_thrust)

old_cout_shield = """            } else if (actions[i].thrust == -1) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " SHIELD" << endl;
            }"""

new_cout_shield = """            } else if (actions[i].thrust == -1) {
                cout << (int)actions[i].tx << " " << (int)actions[i].ty << " SHIELD" << endl;
                shield_cd_track[i] = 3;
            }"""

text = text.replace(old_cout_shield, new_cout_shield)

with open("cg_bot.cpp", "w") as f:
    f.write(text)

