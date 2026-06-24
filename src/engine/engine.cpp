#include "src/engine/engine.h"

const double PI = 3.14159265358979323846;
double cos_lut[360];
double sin_lut[360];
thread_local uint32_t xor_state = 2463534242;
thread_local bool g_friendly_collision = false;
thread_local int g_runner_id = 0;

void InitLUT() {
    for (int i = 0; i < 360; ++i) {
        cos_lut[i] = std::cos(i * PI / 180.0);
        sin_lut[i] = std::sin(i * PI / 180.0);
    }
}

void SeedRand(uint32_t seed) {
    xor_state = seed;
}

uint32_t FastRand() {
    xor_state ^= xor_state << 13;
    xor_state ^= xor_state >> 17;
    xor_state ^= xor_state << 5;
    return xor_state;
}

int FastRandInt(int min, int max) {
    uint32_t range = max - min + 1;
    uint64_t multi = (uint64_t)FastRand() * range;
    return min + (int)(multi >> 32);
}

void Timer::Start() { start_time = std::chrono::high_resolution_clock::now(); }
double Timer::ElapsedMs() const { return std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start_time).count(); }

Vec2::Vec2() : x(0), y(0) {}
Vec2::Vec2(double x, double y) : x(x), y(y) {}
Vec2 Vec2::Add(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
Vec2 Vec2::Sub(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
double Vec2::DistanceSq(const Vec2& o) const { return (x - o.x)*(x - o.x) + (y - o.y)*(y - o.y); }
double Vec2::Distance(const Vec2& o) const { return std::sqrt(DistanceSq(o)); }

double GameEngine::NormalizeAngle(double a) {
    while (a >= 360.0) a -= 360.0;
    while (a < 0.0) a += 360.0;
    return a;
}
double GameEngine::ShortestAngleDiff(double current, double target) {
    double diff = target - current;
    while (diff > 180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;
    return diff;
}
double GameEngine::RadToDeg(double radians) { return radians * 180.0 / PI; }

double Round(double x) {
    return std::floor(x + 0.5);
}

bool CheckpointCollide(const Vec2& p1, const Vec2& p2, const Vec2& cp) {
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    Vec2 pp = p1;
    double pd2 = dx * dx + dy * dy;
    if (pd2 != 0.0) {
        double u = ((cp.x - p1.x) * dx + (cp.y - p1.y) * dy) / pd2;
        if (u > 1.0) {
            pp = p2;
        } else if (u > 0.0) {
            pp.x = p1.x + u * dx;
            pp.y = p1.y + u * dy;
        }
    }
    double diff_x = pp.x - cp.x;
    double diff_y = pp.y - cp.y;
    return (diff_x * diff_x + diff_y * diff_y) < 360000.0; // cpRSQ = 600 * 600
}

Pod::Pod() : id(0), team(0), pos(0,0), vel(0,0), angle(-1.0), next_cp_id(0), boost_available(true), shield_cd(0), timeout(0), laps_completed(0) {}
double Pod::Mass() const { return (shield_cd == 4) ? 10.0 : 1.0; }

void Pod::ApplyGAAction(double angle_shift, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 4; thrust_val = 0; }
    else if (shield_cd > 0) { thrust_val = 0; }
    if (thrust_val == 650) boost_available = false;

    if (angle < 0) angle = 0;
    else angle = GameEngine::NormalizeAngle(angle + angle_shift);

    // Use precise trig to match ApplyServerAction (arena accuracy)
    double rad = angle * PI / 180.0;
    vel.x += std::cos(rad) * thrust_val;
    vel.y += std::sin(rad) * thrust_val;
}

void Pod::ApplyServerAction(double tx, double ty, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 4; thrust_val = 0; }
    else if (shield_cd > 0) { thrust_val = 0; }
    if (thrust_val == 650) { 
        if (boost_available) { thrust_val = 650; boost_available = false; }
        else thrust_val = 200;
    }

    // Reference skips rotation+thrust when target equals current position
    if (tx == pos.x && ty == pos.y) return;

    double target_angle = GameEngine::RadToDeg(std::atan2(ty - pos.y, tx - pos.x));
    
    if (angle < 0) {
        angle = GameEngine::NormalizeAngle(target_angle);
    } else {
        double diff = GameEngine::ShortestAngleDiff(angle, target_angle);
        if (diff > 18.0) diff = 18.0;
        if (diff < -18.0) diff = -18.0;
        angle = GameEngine::NormalizeAngle(angle + diff);
    }

    double rad = angle * PI / 180.0;
    vel.x += std::cos(rad) * thrust_val;
    vel.y += std::sin(rad) * thrust_val;
}

void Pod::Move(double t) {
    pos.x += vel.x * t;
    pos.y += vel.y * t;
}

void Pod::EndTurn() {
    pos.x = Round(pos.x);
    pos.y = Round(pos.y);
    vel.x = std::trunc(vel.x * 0.85);
    vel.y = std::trunc(vel.y * 0.85);
    if (shield_cd > 0) shield_cd--;
}

double PhysicsSimulator::GetCollisionTime(const Pod& p1, const Pod& p2) {
    double px = p2.pos.x - p1.pos.x;
    double py = p2.pos.y - p1.pos.y;
    double pLength2 = px * px + py * py;
    double rsq = 640000.0; // 800 * 800

    if (pLength2 <= rsq) {
        return 0.0;
    }

    double vx = p2.vel.x - p1.vel.x;
    double vy = p2.vel.y - p1.vel.y;
    double dot = px * vx + py * vy;

    if (dot > 0.0) {
        return 10.0;
    }

    double vLength2 = vx * vx + vy * vy;
    if (vLength2 == 0.0) {
        return 10.0;
    }

    double disc = dot * dot - vLength2 * (pLength2 - rsq);
    if (disc < 0.0) {
        return 10.0;
    }

    double discdist = std::sqrt(disc);
    double t = (-dot - discdist) / vLength2;
    return t;
}

void PhysicsSimulator::ResolveCollision(Pod& p1, Pod& p2) {
    if ((p1.id == 0 && p2.id == 1) || (p1.id == 2 && p2.id == 3)) {
        g_friendly_collision = true;
    }
    
    double m1 = (p1.shield_cd == 4) ? 0.1 : 1.0;
    double m2 = (p2.shield_cd == 4) ? 0.1 : 1.0;
    
    double nx = p2.pos.x - p1.pos.x;
    double ny = p2.pos.y - p1.pos.y;
    double dd = std::sqrt(nx * nx + ny * ny);
    double normal_x = nx / dd;
    double normal_y = ny / dd;
    
    double relv_x = p1.vel.x - p2.vel.x;
    double relv_y = p1.vel.y - p2.vel.y;
    
    double force = (normal_x * relv_x + normal_y * relv_y) / (m1 + m2);
    if (force < 120.0) {
        force += 120.0;
    } else {
        force += force;
    }
    
    double impulse_x = normal_x * -force;
    double impulse_y = normal_y * -force;
    
    p1.vel.x += impulse_x * m1;
    p1.vel.y += impulse_y * m1;
    p2.vel.x -= impulse_x * m2;
    p2.vel.y -= impulse_y * m2;
    
    if (dd <= 800.0) {
        double diff = dd - 800.0;
        const double EPS = 0.00001;
        p1.pos.x += normal_x * -(-diff / 2.0 + EPS);
        p1.pos.y += normal_y * -(-diff / 2.0 + EPS);
        p2.pos.x += normal_x * (-diff / 2.0 + EPS);
        p2.pos.y += normal_y * (-diff / 2.0 + EPS);
    }
}

void PhysicsSimulator::SimulateTurn(Pod* p, const std::vector<Vec2>& cps) {
    double t_current = 0.0;
    int col_count = 0;
    int cp_count = cps.size();
    
    std::vector<Vec2> curps(4);
    for (int i = 0; i < 4; ++i) {
        curps[i] = p[i].pos;
    }
    
    while (t_current < 1.0 && col_count < 10) {
        double first_col_t = 2.0;
        int cli = 0;
        int clj = 0;
        
        for (int i = 3; i > 0; --i) {
            for (int j = i - 1; j >= 0; --j) {
                double tx = GetCollisionTime(p[i], p[j]);
                if (tx <= first_col_t) {
                    first_col_t = tx;
                    cli = i;
                    clj = j;
                }
            }
        }
        
        if (first_col_t > 1.0 - t_current) {
            p[0].Move(1.0 - t_current);
            p[1].Move(1.0 - t_current);
            p[2].Move(1.0 - t_current);
            p[3].Move(1.0 - t_current);
            t_current = 1.0;
            break;
        }
        
        p[0].Move(first_col_t);
        p[1].Move(first_col_t);
        p[2].Move(first_col_t);
        p[3].Move(first_col_t);
        
        t_current += first_col_t;
        
        if (cli != clj) {
            ResolveCollision(p[cli], p[clj]);
        }
        
        if (t_current < 1.0) {
            for (int i = 0; i < 4; ++i) {
                if (CheckpointCollide(curps[i], p[i].pos, cps[p[i].next_cp_id])) {
                    p[i].timeout = 0;
                    p[i].next_cp_id++;
                    if (p[i].next_cp_id >= cp_count) {
                        p[i].next_cp_id = 0;
                        p[i].laps_completed++;
                    }
                }
                curps[i] = p[i].pos;
            }
        }
        col_count++;
    }
    
    for (int i = 0; i < 4; ++i) {
        p[i].EndTurn();
        if (CheckpointCollide(curps[i], p[i].pos, cps[p[i].next_cp_id])) {
            p[i].timeout = 0;
            p[i].next_cp_id++;
            if (p[i].next_cp_id >= cp_count) {
                p[i].next_cp_id = 0;
                p[i].laps_completed++;
            }
        }
    }
}

// ======== GA-OPTIMIZED PHYSICS (for internal bot search) ========
// These deliberately differ from the reference PhysicsSimulator:
// - No checkpoint crossing checks
// - No overlap separation in collision resolution
// - Geometric early-exit heuristics in collision detection
// - ~2x faster for the GA evaluation loop

double GAPhysicsSimulator::GetCollisionTime(const Pod& p1, const Pod& p2) {
    double x = p1.pos.x - p2.pos.x;
    double y = p1.pos.y - p2.pos.y;
    double c = x * x + y * y - 640000.0; 

    // High-performance Geometric Early Exit: pods > 2000 units apart cannot collide
    if (c > 3360000.0) return -1.0;

    double vx = p1.vel.x - p2.vel.x;
    double vy = p1.vel.y - p2.vel.y;

    double a = vx * vx + vy * vy;
    if (a < 0.00001) return -1.0;

    double b = 2.0 * (x * vx + y * vy);

    // High-performance early exit: if outside radius and moving apart, they will never collide
    if (c >= 0.0 && b >= 0.0) return -1.0;

    double delta = b * b - 4.0 * a * c;
    if (delta < 0.0) return -1.0;

    double t = (-b - std::sqrt(delta)) / (2.0 * a);
    if (t < 0.0) return -1.0;
    return t;
}

void GAPhysicsSimulator::ResolveCollision(Pod& p1, Pod& p2) {
    if ((p1.id == 0 && p2.id == 1) || (p1.id == 2 && p2.id == 3)) {
        g_friendly_collision = true;
    }
    double m1 = p1.Mass();
    double m2 = p2.Mass();
    double mcoeff = (m1 + m2) / (m1 * m2);
    
    double nx = p1.pos.x - p2.pos.x;
    double ny = p1.pos.y - p2.pos.y;
    double nxnysquare = nx * nx + ny * ny;
    
    double dvx = p1.vel.x - p2.vel.x;
    double dvy = p1.vel.y - p2.vel.y;
    
    double product = nx * dvx + ny * dvy;
    double fx = (nx * product) / (nxnysquare * mcoeff);
    double fy = (ny * product) / (nxnysquare * mcoeff);
    
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
    
    double impulse = std::sqrt(fx * fx + fy * fy);
    if (impulse < 120.0) {
        fx = fx * 120.0 / impulse;
        fy = fy * 120.0 / impulse;
    }
    
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
}

void GAPhysicsSimulator::SimulateTurn(Pod* p) {
    double t_current = 0.0;
    int col_count = 0;
    while (t_current < 1.0 && col_count < 10) {
        double first_col_t = 2.0;
        Pod* col_p1 = nullptr;
        Pod* col_p2 = nullptr;

        // Fully unrolled collision time calculations (exactly 6 pairs for 4 pods)
        double t;
        t = GetCollisionTime(p[0], p[1]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[1]; }
        t = GetCollisionTime(p[0], p[2]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[2]; }
        t = GetCollisionTime(p[0], p[3]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[3]; }
        t = GetCollisionTime(p[1], p[2]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[1]; col_p2 = &p[2]; }
        t = GetCollisionTime(p[1], p[3]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[1]; col_p2 = &p[3]; }
        t = GetCollisionTime(p[2], p[3]);
        if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[2]; col_p2 = &p[3]; }

        if (first_col_t > 1.0 - t_current) {
            p[0].Move(1.0 - t_current);
            p[1].Move(1.0 - t_current);
            p[2].Move(1.0 - t_current);
            p[3].Move(1.0 - t_current);
            t_current = 1.0;
            break;
        }

        if (first_col_t < 0.0001) first_col_t = 0.0001;

        p[0].Move(first_col_t);
        p[1].Move(first_col_t);
        p[2].Move(first_col_t);
        p[3].Move(first_col_t);

        if (col_p1 && col_p2) ResolveCollision(*col_p1, *col_p2);
        t_current += first_col_t;
        col_count++;
    }
    
    if (t_current < 1.0) {
        p[0].Move(1.0 - t_current);
        p[1].Move(1.0 - t_current);
        p[2].Move(1.0 - t_current);
        p[3].Move(1.0 - t_current);
    }
    
    p[0].EndTurn();
    p[1].EndTurn();
    p[2].EndTurn();
    p[3].EndTurn();
}
