import re

with open("cg_bot.cpp", "r") as f:
    text = f.read()

# Fix boost to use runner_idx
text = text.replace("if (i == 0 && boost_available_0) {", "if (i == runner_idx && ((runner_idx == 0 && boost_available_0) || (runner_idx == 1 && boost_available_1))) {")
text = text.replace("boost_available_0 = false;", "if (runner_idx == 0) boost_available_0 = false; else boost_available_1 = false;")

with open("cg_bot.cpp", "w") as f:
    f.write(text)

