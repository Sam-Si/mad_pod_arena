#include "src/engine/engine.h"

const double PI = 3.14159265358979323846;
double cos_lut[360];
double sin_lut[360];
thread_local uint32_t xor_state = 2463534242;
thread_local bool g_friendly_collision = false;

void InitLUT() {
    for (int i = 0; i < 360; ++i) {
        cos_lut[i] = std::cos(i * PI / 180.0);
        sin_lut[i] = std::sin(i * PI / 180.0);
    }
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

Pod::Pod() : id(0), team(0), pos(0,0), vel(0,0), angle(-1.0), next_cp_id(0), boost_available(true), shield_cd(0), timeout(0), laps_completed(0) {}
double Pod::Mass() const { return (shield_cd > 0) ? 10.0 : 1.0; }

void Pod::ApplyGAAction(double angle_shift, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 3; thrust_val = 0; }
    else if (shield_cd > 0) { shield_cd--; thrust_val = 0; }
    if (thrust_val == 650) boost_available = false;

    if (angle < 0) angle = 0;
    else angle = GameEngine::NormalizeAngle(angle + angle_shift);

    // Use precise trig to match ApplyServerAction (arena accuracy)
    double rad = angle * PI / 180.0;
    vel.x += std::cos(rad) * thrust_val;
    vel.y += std::sin(rad) * thrust_val;
}

void Pod::ApplyServerAction(double tx, double ty, int thrust_val) {
    if (thrust_val == -1) { shield_cd = 3; thrust_val = 0; }
    else if (shield_cd > 0) { shield_cd--; thrust_val = 0; }
    if (thrust_val == 650) { 
        if (boost_available) { thrust_val = 650; boost_available = false; }
        else thrust_val = 200;
    }

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
    pos.x = std::round(pos.x);
    pos.y = std::round(pos.y);
    vel.x = std::trunc(vel.x * 0.85);
    vel.y = std::trunc(vel.y * 0.85);
}

double PhysicsSimulator::GetCollisionTime(const Pod& p1, const Pod& p2) {
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

void PhysicsSimulator::ResolveCollision(Pod& p1, Pod& p2) {
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
    
    // Apply ONCE (raw physics)
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
    
    // Normalize to 120 if below threshold
    double impulse = std::sqrt(fx * fx + fy * fy);
    if (impulse < 120.0) {
        fx = fx * 120.0 / impulse;
        fy = fy * 120.0 / impulse;
    }
    
    // Apply SECOND time (min impulse enforcement)
    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
}

void PhysicsSimulator::SimulateTurn(Pod* p) {
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
