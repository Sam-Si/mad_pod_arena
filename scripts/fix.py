with open("cg_bot.cpp", "r") as f:
    text = f.read()

text = text.replace("target_x", "x").replace("target_y", "y")

with open("cg_bot.cpp", "w") as f:
    f.write(text)
