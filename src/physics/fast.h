#pragma once
// =============================================================================
// csb::fast — GA search collision fragment (degrees pods, engine-isomorphic).
// Line-faithful port of former GAPhysicsSimulator::SimulateTurn (engine.cpp).
// Does NOT implement CP / timeout / won / rotate / thrust (caller ApplyGAAction).
// Authoritative Fast entrypoint: csb::fast::SimulateTurn — NOT Game::step(Fast).
// =============================================================================

#include <cmath>

// Single global symbol (production def in engine.cpp; tests define in test TU).
extern thread_local bool g_friendly_collision;

namespace csb {
namespace fast {

// Named constants MUST match src/core/constants.h (enforced by sim/check_ssot_policy.py).
// Amalgam paste cannot #include other headers, so values are mirrored here.
inline constexpr double kFriction = 0.85;              // csb_constants::kFriction
inline constexpr double kPodCollisionRsq = 640000.0;   // kPodCollisionRsq
inline constexpr double kGaFarApartC = 3360000.0;       // kGaFarApartC
inline constexpr double kMinImpulse = 120.0;            // kMinImpulse
inline constexpr double kShieldMassFast = 10.0;         // csb_constants::kShieldMassFast
inline constexpr double kNormalMassFast = 1.0;          // csb_constants::kNormalMassFast
inline constexpr int kShieldTimerActivate = 4;          // csb_constants::kShieldTimerActivate

// Degrees / pos / vel — field set matches engine::Pod for trivial assign / layout.
struct Pod {
    int id = 0;
    int team = 0;
    struct Vec2 {
        double x = 0;
        double y = 0;
        Vec2() = default;
        Vec2(double x_, double y_) : x(x_), y(y_) {}
    } pos, vel;
    double angle = -1.0;
    int next_cp_id = 0;
    bool boost_available = true;
    int shield_cd = 0;
    int timeout = 0;
    int laps_completed = 0;

    double Mass() const {
        return (shield_cd == kShieldTimerActivate) ? kShieldMassFast : kNormalMassFast;
    }

    void Move(double t) {
        pos.x += vel.x * t;
        pos.y += vel.y * t;
    }

    // Round = floor(x + 0.5) — same as engine Round(); not std::round halfway ties.
    void EndTurn() {
        pos.x = std::floor(pos.x + 0.5);
        pos.y = std::floor(pos.y + 0.5);
        vel.x = std::trunc(vel.x * kFriction);
        vel.y = std::trunc(vel.y * kFriction);
        if (shield_cd > 0) shield_cd--;
    }
};

inline double GetCollisionTime(const Pod& p1, const Pod& p2) {
    double x = p1.pos.x - p2.pos.x;
    double y = p1.pos.y - p2.pos.y;
    double c = x * x + y * y - kPodCollisionRsq;

    if (c > kGaFarApartC) return -1.0;

    double vx = p1.vel.x - p2.vel.x;
    double vy = p1.vel.y - p2.vel.y;

    double a = vx * vx + vy * vy;
    if (a < 0.00001) return -1.0;

    double b = 2.0 * (x * vx + y * vy);

    if (c >= 0.0 && b >= 0.0) return -1.0;

    double delta = b * b - 4.0 * a * c;
    if (delta < 0.0) return -1.0;

    double t = (-b - std::sqrt(delta)) / (2.0 * a);
    if (t < 0.0) return -1.0;
    return t;
}

inline void ResolveCollision(Pod& p1, Pod& p2) {
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
    if (impulse < kMinImpulse) {
        fx = fx * kMinImpulse / impulse;
        fy = fy * kMinImpulse / impulse;
    }

    p1.vel.x -= fx / m1;
    p1.vel.y -= fy / m1;
    p2.vel.x += fx / m2;
    p2.vel.y += fy / m2;
}

// pods points to array of length 4
inline void SimulateTurn(Pod* p) {
    double t_current = 0.0;
    int col_count = 0;
    while (t_current < 1.0 && col_count < 10) {
        double first_col_t = 2.0;
        Pod* col_p1 = nullptr;
        Pod* col_p2 = nullptr;

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

}  // namespace fast
}  // namespace csb
