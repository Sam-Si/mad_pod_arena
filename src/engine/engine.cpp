#include "src/engine/engine.h"
#include "src/core/constants.h"

const double PI = csb_constants::kPi;
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

Pod::Pod() : id(0), team(0), pos(0,0), vel(0,0), angle(-1.0), next_cp_id(0), boost_available(true), shield_cd(0), timeout(0), laps_completed(0) {}
double Pod::Mass() const {
    return (shield_cd == csb_constants::kShieldTimerActivate)
               ? csb_constants::kShieldMassFast
               : csb_constants::kNormalMassFast;
}

void Pod::ApplyGAAction(double angle_shift, int thrust_val) {
    if (thrust_val == -1) { shield_cd = csb_constants::kShieldTimerActivate; thrust_val = 0; }
    else if (shield_cd > 0) { thrust_val = 0; }
    if (thrust_val == csb_constants::kBoostThrust) boost_available = false;

    if (angle < 0) angle = 0;
    else angle = GameEngine::NormalizeAngle(angle + angle_shift);

    // Use precise trig to match ApplyServerAction (arena accuracy)
    double rad = angle * PI / 180.0;
    vel.x += std::cos(rad) * thrust_val;
    vel.y += std::sin(rad) * thrust_val;
}

void Pod::ApplyServerAction(double tx, double ty, int thrust_val) {
    if (thrust_val == -1) { shield_cd = csb_constants::kShieldTimerActivate; thrust_val = 0; }
    else if (shield_cd > 0) { thrust_val = 0; }
    if (thrust_val == csb_constants::kBoostThrust) {
        if (boost_available) { thrust_val = csb_constants::kBoostThrust; boost_available = false; }
        else thrust_val = csb_constants::kMaxThrust;
    }

    // Reference skips rotation+thrust when target equals current position
    if (tx == pos.x && ty == pos.y) return;

    double target_angle = GameEngine::RadToDeg(std::atan2(ty - pos.y, tx - pos.x));
    
    if (angle < 0) {
        angle = GameEngine::NormalizeAngle(target_angle);
    } else {
        double diff = GameEngine::ShortestAngleDiff(angle, target_angle);
        const double max_rot = csb_constants::kMaxRotateDeg;
        if (diff > max_rot) diff = max_rot;
        if (diff < -max_rot) diff = -max_rot;
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
    vel.x = std::trunc(vel.x * csb_constants::kFriction);
    vel.y = std::trunc(vel.y * csb_constants::kFriction);
    if (shield_cd > 0) shield_cd--;
}
