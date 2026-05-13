with open("ga_bot.cpp", "r") as f:
    text = f.read()

old_eval = """    Vec2 dir(target.x - pod.pos.x, target.y - pod.pos.y);
    double dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
    if (dir_len > 0) {
        double dot = (pod.vel.x * (dir.x/dir_len)) + (pod.vel.y * (dir.y/dir_len));
        score += dot * config.align_weight; 
    }"""

new_eval = """    Vec2 dir(target.x - pod.pos.x, target.y - pod.pos.y);
    double dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
    if (dir_len > 0) {
        double dot = (pod.vel.x * (dir.x/dir_len)) + (pod.vel.y * (dir.y/dir_len));
        score += dot * config.align_weight; 
        
        // Orbital penalty: penalize velocity that is perpendicular to the target
        double cross = (pod.vel.x * (dir.y/dir_len)) - (pod.vel.y * (dir.x/dir_len));
        score -= std::abs(cross) * 0.5; // Punish drifting sideways to prevent orbiting
    }"""

text = text.replace(old_eval, new_eval)

with open("ga_bot.cpp", "w") as f:
    f.write(text)

