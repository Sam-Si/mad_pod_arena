with open("cg_bot.cpp", "r") as f:
    text = f.read()

text = text.replace("double dist = env[0].pos.Distance(cps[env[0].next_cp_id]);", "double dist = env[i].pos.Distance(cps[env[i].next_cp_id]);")
text = text.replace("double target_angle = GameEngine::RadToDeg(std::atan2(cps[env[0].next_cp_id].y - env[0].pos.y, cps[env[0].next_cp_id].x - env[0].pos.x));", "double target_angle = GameEngine::RadToDeg(std::atan2(cps[env[i].next_cp_id].y - env[i].pos.y, cps[env[i].next_cp_id].x - env[i].pos.x));")
text = text.replace("double diff = std::abs(GameEngine::ShortestAngleDiff(env[0].angle, target_angle));", "double diff = std::abs(GameEngine::ShortestAngleDiff(env[i].angle, target_angle));")

with open("cg_bot.cpp", "w") as f:
    f.write(text)
