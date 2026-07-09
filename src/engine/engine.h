#pragma once
#include <cstddef>
#include "fast.h"
#include <vector>
#include <cmath>
#include <cstdint>
#include <chrono>

extern const double PI;
extern double cos_lut[360];
extern double sin_lut[360];
extern thread_local bool g_friendly_collision;
extern thread_local int g_runner_id;

void InitLUT();
void SeedRand(uint32_t seed);
uint32_t FastRand();
int FastRandInt(int min, int max);

class Timer {
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
public:
    void Start();
    double ElapsedMs() const;
};

struct Vec2 {
    double x, y;
    Vec2();
    Vec2(double x, double y);
    Vec2 Add(const Vec2& o) const;
    Vec2 Sub(const Vec2& o) const;
    double DistanceSq(const Vec2& o) const;
    double Distance(const Vec2& o) const;
};

class GameEngine {
public:
    static double NormalizeAngle(double a);
    static double ShortestAngleDiff(double current, double target);
    static double RadToDeg(double radians);
};

struct PodAction {
    double tx, ty;
    int thrust; 
};

struct Pod {
    int id;
    int team;
    Vec2 pos, vel;
    double angle;
    int next_cp_id;
    bool boost_available;
    int shield_cd;
    int timeout; // To track 100 turns limit
    int laps_completed;

    Pod();
    double Mass() const;
    void ApplyGAAction(double angle_shift, int thrust);
    void ApplyServerAction(double tx, double ty, int thrust_val);
    void Move(double t);
    void EndTurn();
};

double Round(double x);
// Checkpoint geometry SSOT: csb::cpCollide in physics/fidelity_math.h (not duplicated here).

// Bridge engine Pod <-> csb::fast::Pod (layout-compatible degrees pods).
inline void FastSimulateTurn(Pod* pods) {
    static_assert(sizeof(Pod) == sizeof(csb::fast::Pod), "Pod layout must match csb::fast::Pod");
    static_assert(offsetof(Pod, id) == offsetof(csb::fast::Pod, id), "id offset");
    static_assert(offsetof(Pod, team) == offsetof(csb::fast::Pod, team), "team offset");
    static_assert(offsetof(Pod, pos) == offsetof(csb::fast::Pod, pos), "pos offset");
    static_assert(offsetof(Pod, vel) == offsetof(csb::fast::Pod, vel), "vel offset");
    static_assert(offsetof(Pod, angle) == offsetof(csb::fast::Pod, angle), "angle offset");
    static_assert(offsetof(Pod, next_cp_id) == offsetof(csb::fast::Pod, next_cp_id), "next_cp_id offset");
    static_assert(offsetof(Pod, boost_available) == offsetof(csb::fast::Pod, boost_available), "boost_available offset");
    static_assert(offsetof(Pod, shield_cd) == offsetof(csb::fast::Pod, shield_cd), "shield_cd offset");
    static_assert(offsetof(Pod, timeout) == offsetof(csb::fast::Pod, timeout), "timeout offset");
    static_assert(offsetof(Pod, laps_completed) == offsetof(csb::fast::Pod, laps_completed), "laps_completed offset");
    csb::fast::SimulateTurn(reinterpret_cast<csb::fast::Pod*>(pods));
}
