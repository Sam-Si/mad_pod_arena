with open("ga_bot.cpp", "r") as f:
    text = f.read()

old_out_thrust = """        if (a.gene1 > 0.95) out_thrust = -1;
        else if (a.gene3 > 0.95 && p.boost_available) out_thrust = 650;"""

new_out_thrust = """        if (a.gene1 > 0.95 && p.shield_cd == 0) out_thrust = -1;
        else if (a.gene3 > 0.95 && p.boost_available) out_thrust = 650;"""

text = text.replace(old_out_thrust, new_out_thrust)

with open("ga_bot.cpp", "w") as f:
    f.write(text)

