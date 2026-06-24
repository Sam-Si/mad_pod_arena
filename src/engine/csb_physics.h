// =============================================================================
// NOT the referee / CG-server source of truth.
// Referee-faithful physics lives ONLY in src/physics/physics.h (battle-verified).
// This header is an experimental / fast-path helper used by engine benchmarks only.
// Bot search physics SSoT: src/engine/engine.h + engine.cpp.
// =============================================================================

#pragma once

#include <cmath>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <cstdint>

namespace csb {

// Math Constants
constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;
constexpr double MAX_ROTATE_DEG = 18.0;

// Structures
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    constexpr Vec2(double x, double y) : x(x), y(y) {}

    inline Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    inline Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    inline Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    inline Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    inline Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }

    inline double DistanceSq(const Vec2& o) const {
        double dx = x - o.x;
        double dy = y - o.y;
        return dx * dx + dy * dy;
    }

    inline double Distance(const Vec2& o) const {
        return std::sqrt(DistanceSq(o));
    }

    inline double LengthSq() const { return x * x + y * y; }
    inline double Length() const { return std::sqrt(x * x + y * y); }
    inline double Dot(const Vec2& o) const { return x * o.x + y * o.y; }
};

inline double Round(double x) {
    return std::floor(x + 0.5);
}

// Global LUT structures for sin and cos
struct TrigLUT {
    double cos_val[3600];
    double sin_val[3600];

    TrigLUT() {
        for (int i = 0; i < 3600; ++i) {
            double rad = (i * 0.1) * DEG_TO_RAD;
            cos_val[i] = std::cos(rad);
            sin_val[i] = std::sin(rad);
        }
    }

    // High performance lookup with precision up to 0.1 degrees
    inline void Lookup(double deg, double& out_cos, double& out_sin) const {
        double a = deg;
        while (a >= 360.0) a -= 360.0;
        while (a < 0.0) a += 360.0;
        int idx = static_cast<int>(a * 10.0 + 0.5);
        if (idx >= 3600) idx = 0;
        out_cos = cos_val[idx];
        out_sin = sin_val[idx];
    }
};

inline const TrigLUT& GetTrigLUT() {
    static TrigLUT lut;
    return lut;
}

// Game structure representations
struct PodAction {
    double tx = 0.0;
    double ty = 0.0;
    int thrust = 0; // thrust value: 0 to 200, or 650 (BOOST), or -1 (SHIELD)
    bool shield = false;
    bool boost = false;
};

struct Pod {
    int id = 0;
    int team = 0;
    Vec2 pos;
    Vec2 vel;
    double angle = -1.0; // Angle in degrees. -1.0 means uninitialized
    int next_cp_id = 0;
    bool boost_available = true;
    int shield_cd = 0;
    int timeout = 0;
    int laps_completed = 0;
    bool won = false;

    Pod() = default;

    inline double Mass() const {
        return (shield_cd == 4) ? 10.0 : 1.0;
    }

    inline double NormalizedAngle() const {
        double a = angle;
        while (a >= 360.0) a -= 360.0;
        while (a < 0.0) a += 360.0;
        return a;
    }

    inline double ShortestAngleDiff(double target_angle) const {
        double diff = target_angle - angle;
        while (diff > 180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        return diff;
    }

    // Apply rotation and thrust using LUT (for high speed search)
    inline void ApplyActionFast(double angle_shift, int thrust_val) {
        if (thrust_val == -1) {
            shield_cd = 4;
            thrust_val = 0;
        } else if (shield_cd > 0) {
            thrust_val = 0;
        }
        if (thrust_val == 650) {
            boost_available = false;
        }

        if (angle < 0) angle = 0;
        else {
            angle += angle_shift;
            while (angle >= 360.0) angle -= 360.0;
            while (angle < 0.0) angle += 360.0;
        }

        double c, s;
        GetTrigLUT().Lookup(angle, c, s);
        vel.x += c * thrust_val;
        vel.y += s * thrust_val;
    }

    // Apply standard server target coordinates (referee mode)
    inline void ApplyActionReferee(double tx, double ty, int thrust_val, bool shield_act = false, bool boost_act = false) {
        if (shield_act) {
            shield_cd = 4;
            thrust_val = 0;
        }

        if (boost_act) {
            if (boost_available) {
                thrust_val = 650;
                boost_available = false;
            } else {
                thrust_val = 200;
            }
        }

        if (shield_cd > 0 && !shield_act) {
            thrust_val = 0;
        }

        if (tx == pos.x && ty == pos.y) return;

        double target_angle = RAD_TO_DEG * std::atan2(ty - pos.y, tx - pos.x);
        if (angle < 0) {
            angle = target_angle;
            while (angle >= 360.0) angle -= 360.0;
            while (angle < 0.0) angle += 360.0;
        } else {
            double diff = ShortestAngleDiff(target_angle);
            if (diff > MAX_ROTATE_DEG) diff = MAX_ROTATE_DEG;
            if (diff < -MAX_ROTATE_DEG) diff = -MAX_ROTATE_DEG;
            angle += diff;
            while (angle >= 360.0) angle -= 360.0;
            while (angle < 0.0) angle += 360.0;
        }

        double rad = angle * DEG_TO_RAD;
        vel.x += std::cos(rad) * thrust_val;
        vel.y += std::sin(rad) * thrust_val;
    }

    inline void Move(double t) {
        pos.x += vel.x * t;
        pos.y += vel.y * t;
    }

    inline void EndTurn() {
        pos.x = Round(pos.x);
        pos.y = Round(pos.y);
        vel.x = std::trunc(vel.x * 0.85);
        vel.y = std::trunc(vel.y * 0.85);
        if (shield_cd > 0) shield_cd--;
    }
};

struct GameState {
    std::array<Pod, 4> pods;
    std::vector<Vec2> cps;
    int cp_count = 0;
    int laps = 3;
};

// Physics Simulator Class
class PhysicsEngine {
public:
    // Check if a line segment crosses the checkpoint radius
    static inline bool CheckpointCollide(const Vec2& p1, const Vec2& p2, const Vec2& cp) {
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

    // Referee Mode: collision time prediction
    static inline double GetCollisionTimeReferee(const Pod& p1, const Pod& p2) {
        double px = p2.pos.x - p1.pos.x;
        double py = p2.pos.y - p1.pos.y;
        double pLength2 = px * px + py * py;
        constexpr double rsq = 640000.0; // 800 * 800

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
        return (-dot - discdist) / vLength2;
    }

    // Referee Mode: collision resolution with overlap separation
    static inline void ResolveCollisionReferee(Pod& p1, Pod& p2) {
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
            constexpr double EPS = 0.00001;
            p1.pos.x += normal_x * -(-diff / 2.0 + EPS);
            p1.pos.y += normal_y * -(-diff / 2.0 + EPS);
            p2.pos.x += normal_x * (-diff / 2.0 + EPS);
            p2.pos.y += normal_y * (-diff / 2.0 + EPS);
        }
    }

    // Simulate 1 turn in full Referee compliance (using map checkpoints and absolute timing)
    static inline void SimulateTurnReferee(std::array<Pod, 4>& pods, const std::vector<Vec2>& cps, int cp_count) {
        double t_current = 0.0;
        int col_count = 0;

        std::array<Vec2, 4> curps;
        for (int i = 0; i < 4; ++i) {
            curps[i] = pods[i].pos;
        }

        while (t_current < 1.0 && col_count < 10) {
            double first_col_t = 2.0;
            int cli = 0;
            int clj = 0;

            for (int i = 3; i > 0; --i) {
                for (int j = i - 1; j >= 0; --j) {
                    double tx = GetCollisionTimeReferee(pods[i], pods[j]);
                    if (tx <= first_col_t) {
                        first_col_t = tx;
                        cli = i;
                        clj = j;
                    }
                }
            }

            if (first_col_t > 1.0 - t_current) {
                for (int i = 0; i < 4; ++i) pods[i].Move(1.0 - t_current);
                t_current = 1.0;
                break;
            }

            for (int i = 0; i < 4; ++i) pods[i].Move(first_col_t);
            t_current += first_col_t;

            if (cli != clj) {
                ResolveCollisionReferee(pods[cli], pods[clj]);
            }

            if (t_current < 1.0) {
                for (int i = 0; i < 4; ++i) {
                    if (CheckpointCollide(curps[i], pods[i].pos, cps[pods[i].next_cp_id])) {
                        pods[i].timeout = 0;
                        pods[i].next_cp_id++;
                        if (pods[i].next_cp_id >= cp_count) {
                            pods[i].next_cp_id = 0;
                            pods[i].laps_completed++;
                        }
                    }
                    curps[i] = pods[i].pos;
                }
            }
            col_count++;
        }

        for (int i = 0; i < 4; ++i) {
            pods[i].EndTurn();
            if (CheckpointCollide(curps[i], pods[i].pos, cps[pods[i].next_cp_id])) {
                pods[i].timeout = 0;
                pods[i].next_cp_id++;
                if (pods[i].next_cp_id >= cp_count) {
                    pods[i].next_cp_id = 0;
                    pods[i].laps_completed++;
                }
            }
        }
    }

    // ==========================================
    // ULTRA OPTIMIZED GA SEARCH PHYSICS ENGINE
    // ==========================================

    // High performance geometric early exit & math collision check
    static inline double GetCollisionTimeFast(const Pod& p1, const Pod& p2) {
        double x = p1.pos.x - p2.pos.x;
        double y = p1.pos.y - p2.pos.y;
        double c = x * x + y * y - 640000.0;

        // Bounding check: if pods are > 2000 units apart, they cannot collide in 1 unit of time
        if (c > 3360000.0) return -1.0;

        double vx = p1.vel.x - p2.vel.x;
        double vy = p1.vel.y - p2.vel.y;

        double a = vx * vx + vy * vy;
        if (a < 0.00001) return -1.0;

        double b = 2.0 * (x * vx + y * vy);

        // Moving apart check: if outside collision distance and moving away, they'll never collide
        if (c >= 0.0 && b >= 0.0) return -1.0;

        double delta = b * b - 4.0 * a * c;
        if (delta < 0.0) return -1.0;

        double t = (-b - std::sqrt(delta)) / (2.0 * a);
        if (t < 0.0) return -1.0;
        return t;
    }

    // Resolve collision without overlap push for extra speed in GA search
    static inline void ResolveCollisionFast(Pod& p1, Pod& p2) {
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

    // Fully unrolled, super fast collision loop (no checkpoint crossing, no allocation)
    static inline void SimulateTurnFast(Pod* p) {
        double t_current = 0.0;
        int col_count = 0;
        while (t_current < 1.0 && col_count < 10) {
            double first_col_t = 2.0;
            Pod* col_p1 = nullptr;
            Pod* col_p2 = nullptr;

            double t;
            t = GetCollisionTimeFast(p[0], p[1]);
            if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[1]; }
            t = GetCollisionTimeFast(p[0], p[2]);
            if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[2]; }
            t = GetCollisionTimeFast(p[0], p[3]);
            if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[0]; col_p2 = &p[3]; }
            t = GetCollisionTimeFast(p[1], p[2]);
            if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[1]; col_p2 = &p[2]; }
            t = GetCollisionTimeFast(p[1], p[3]);
            if (t >= 0.0 && t + t_current < 1.0 && t < first_col_t) { first_col_t = t; col_p1 = &p[1]; col_p2 = &p[3]; }
            t = GetCollisionTimeFast(p[2], p[3]);
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

            if (col_p1 && col_p2) ResolveCollisionFast(*col_p1, *col_p2);
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
};

} // namespace csb
