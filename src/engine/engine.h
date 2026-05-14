#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <chrono>

extern const double PI;
extern double cos_lut[360];
extern double sin_lut[360];

void InitLUT();
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
    void ApplyGAAction(int angle_shift, int thrust);
    void ApplyServerAction(double tx, double ty, int thrust_val);
    void Move(double t);
    void EndTurn();
};

class PhysicsSimulator {
public:
    static double GetCollisionTime(const Pod& p1, const Pod& p2);
    static void ResolveCollision(Pod& p1, Pod& p2);
    static void SimulateTurn(std::vector<Pod>& pods);
};
