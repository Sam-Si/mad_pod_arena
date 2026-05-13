import re

with open("cg_bot.cpp", "r") as f:
    text = f.read()

corner_cut_old = """        Vec2 to_next = cps[next_next] - target;
        double to_next_len = std::sqrt(to_next.x*to_next.x + to_next.y*to_next.y);
        if (to_next_len > 0.0) {
            target.x += (to_next.x / to_next_len) * 400.0;
            target.y += (to_next.y / to_next_len) * 400.0;
        }"""
        
corner_cut_new = """        double to_next_x = cps[next_next].x - target.x;
        double to_next_y = cps[next_next].y - target.y;
        double to_next_len = std::sqrt(to_next_x*to_next_x + to_next_y*to_next_y);
        if (to_next_len > 0.0) {
            target.x += (to_next_x / to_next_len) * 400.0;
            target.y += (to_next_y / to_next_len) * 400.0;
        }"""

text = text.replace(corner_cut_old, corner_cut_new)

with open("cg_bot.cpp", "w") as f:
    f.write(text)
