import re

with open("cg_bot.cpp", "r") as f:
    text = f.read()

# Fix EvaluatePod target
eval_old = """    Vec2 target = cps[pod.next_cp_id];
    score -= pod.pos.Distance(target) * config.dist_weight;"""

eval_new = """    Vec2 target = cps[pod.next_cp_id];
    int next_next = (pod.next_cp_id + 1) % cps.size();
    double to_next_x = cps[next_next].x - target.x;
    double to_next_y = cps[next_next].y - target.y;
    double to_next_len = std::sqrt(to_next_x*to_next_x + to_next_y*to_next_y);
    if (to_next_len > 0.0) {
        target.x += (to_next_x / to_next_len) * 400.0;
        target.y += (to_next_y / to_next_len) * 400.0;
    }
    score -= pod.pos.Distance(target) * config.dist_weight;"""

text = text.replace(eval_old, eval_new)

with open("cg_bot.cpp", "w") as f:
    f.write(text)
