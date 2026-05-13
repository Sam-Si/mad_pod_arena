with open("cg_bot.cpp", "r") as f:
    text = f.read()

text = text.replace("actions[i].x", "actions[i].tx").replace("actions[i].y", "actions[i].ty")

with open("cg_bot.cpp", "w") as f:
    f.write(text)
