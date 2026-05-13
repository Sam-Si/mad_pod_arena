import re

with open("cg_bot.cpp", "r") as f:
    text = f.read()

start = text.find('void Action::Randomize()')
end = text.find('int main()')

ga_content = """#include "ga_bot.h"
#include "engine.h"
#include <cmath>
#include <algorithm>
#include <iostream>

using namespace std;

""" + text[start:end]

with open("ga_bot.cpp", "w") as f:
    f.write(ga_content)
