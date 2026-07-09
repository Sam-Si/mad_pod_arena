// Branch-coverage suite for first-party product headers + engine API.
// Drives SHIPPED entry points only (Feathers/Fowler self-testing).
#include "src/cg/ga_pure.h"
#include "src/core/progress.h"
#include "src/core/constants.h"
#include "src/core/maps/catalog.h"
#include "src/physics/fidelity_math.h"
#include "src/physics/fidelity_world_step.h"
#include "src/physics/fast.h"
#include "src/physics/fast_physics.h"
#include "src/physics/physics.h"
#include "src/engine/engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

static int g_fails = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL " << #cond << " @" << __LINE__ << "\n"; ++g_fails; } } while (0)

// g_friendly_collision defined in engine.cpp when linked

static void cover_ga_pure_all_branches() {
    using namespace ga_pure;
    CHECK(std::fabs(NormalizeAngleDeg(720.0) - 0.0) < 1e-9);
    CHECK(std::fabs(NormalizeAngleDeg(-720.0) - 0.0) < 1e-9);
    CHECK(std::fabs(NormalizeAngleDeg(370.0) - 10.0) < 1e-9);
    CHECK(std::fabs(ShortestAngleDiffDeg(0.0, 400.0) - 40.0) < 1e-9);
    CHECK(std::fabs(ShortestAngleDiffDeg(0.0, -400.0) - (-40.0)) < 1e-9);
    CHECK(ClampAngleShiftDeg(0.0) == 0.0);
    CHECK(ClampAngleShiftDeg(100.0) == 18.0);
    CHECK(ClampAngleShiftDeg(-100.0) == -18.0);
    CHECK(ClampAngleShiftDeg(9.0, 5.0) == 5.0);
    CHECK(std::fabs(AngleShiftTowardTarget(0, 0, 0, 100, 0)) < 1e-9);
    CHECK(AngleShiftTowardTarget(0, 0, 0, 0, 100) == 18.0);
    CHECK(AngleShiftTowardTarget(0, 0, 0, 0, -100) == -18.0);
    CHECK(BoundsPenalty(0, 0) == 0.0);
    CHECK(BoundsPenalty(-1001, 0) == kBoundsPenalty);
    CHECK(BoundsPenalty(17001, 0) == kBoundsPenalty);
    CHECK(BoundsPenalty(0, -1001) == kBoundsPenalty);
    CHECK(BoundsPenalty(0, 10001) == kBoundsPenalty);
    CHECK(CpCrossedBonus(0) == 0.0);
    CHECK(CpCrossedBonus(-3) == 0.0);
    CHECK(CpCrossedBonus(3) == 45000.0);
    CHECK(DistWeightTerm(2, 3) == 6.0);
    CHECK(AlignTerm(1, 0, 1, 0, 2) == 2.0);
    CHECK(LateralTerm(0, 1, 1, 0, 2) == 2.0);
    CHECK(AngleErrorTerm(2, 3) == 6.0);
    CHECK(SpeedTerm(0, 0, 5) == 0.0);
    CHECK(FastActPenalty(0) == 0.0);
    CHECK(FastActPenalty(1.5, 10) == 15.0);
    CHECK(FreeFlightFrictionVel(200) == 170.0);
    CHECK(std::fabs(RunnerKinematicsScore(1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1) - 0.0) < 1e-9);
    (void)RunnerKinematicsScore(0, 0, 10, 0, 0, 100, 0, 1, 1, 1, 1);
}

static void cover_progress_all_branches() {
    using namespace csb_progress;
    CHECK(GlobalNext(1, 2, 0) == 2);
    CHECK(GlobalNext(1, 2, 4) == 6);
    int lap = -1, loc = -1;
    Decode(5, 0, &lap, &loc);
    CHECK(lap == 0 && loc == 5);
    Decode(5, 0, nullptr, nullptr);
    Decode(7, 3, &lap, &loc);
    CHECK(lap == 2 && loc == 1);
    Decode(7, 3, nullptr, &loc);
    Decode(7, 3, &lap, nullptr);
    CHECK(LocalNext(7, 0) == 7);
    CHECK(LocalNext(7, 3) == 1);
}

static void cover_fidelity_math_all_branches() {
    using namespace csb;
    CHECK(std::fabs(goMod(5.5, 2.0) - 1.5) < 1e-12);
    CHECK(roundHalfUp(1.4) == 1.0);
    CHECK(roundHalfUp(1.5) == 2.0);
    CHECK(frictionTrunc(200.0) == 170.0);
    CHECK(snapNearInteger(180.0) == 180.0);
    CHECK(snapNearInteger(-180.0) == -180.0);
    CHECK(std::fabs(snapNearInteger(1.0 + 1e-15) - 1.0) < 1e-12);
    CHECK(snapNearInteger(1.5) == 1.5);
    (void)getAngle(0, 0, 1, 0);
    CHECK(cpCollide(0, 0, 0, 0, 10000, 10000) == false);
    CHECK(cpCollide(0, 0, 0, 0, 0, 0) == true);
    CHECK(cpCollide(0, 0, 1000, 0, 2000, 0) == false);
    CHECK(cpCollide(0, 0, 1000, 0, 500, 0) == true);
    CHECK(cpCollide(0, 0, 1000, 0, -100, 0) == false);
    CHECK(cpCollide(0, 0, 10, 0, 0, 600.0) == false);
    CHECK(newCollideTime(0, 0, 0, 0, 100, 0, 0, 0) == 0.0);
    CHECK(newCollideTime(0, 0, 0, 0, 2000, 0, 100, 0) == 10.0);
    CHECK(newCollideTime(0, 0, 0, 0, 2000, 0, 0, 0) == 10.0);
    (void)newCollideTime(0, 0, 100, 0, 2000, 0, -100, 0);
    CHECK(newCollideTime(0, 0, 100, 0, 0, 5000, 100, 0) == 10.0);
}

static void cover_world_step_scenarios() {
    using namespace csb;
    WorldPod pods[kPodCount];
    double gcx[8] = {1000, 5000, 9000, 1000};
    double gcy[8] = {1000, 1000, 5000, 8000};
    int timeout[2] = {kTimeoutLimit, kTimeoutLimit};
    int turn = 0;
    for (int i = 0; i < kPodCount; ++i) {
        pods[i] = {};
        pods[i].px = 2000 + i * 50; pods[i].py = 2000; pods[i].vx = 100; pods[i].vy = 0; pods[i].next = 1;
    }
    simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);
    CHECK(turn == 1);

    turn = 0; timeout[0] = timeout[1] = kTimeoutLimit;
    for (int i = 0; i < kPodCount; ++i) pods[i] = {};
    pods[0].px = 0; pods[0].py = 0; pods[0].vx = 400; pods[0].next = 1; pods[0].shieldtimer = 4;
    pods[1].px = 900; pods[1].py = 0; pods[1].vx = -400; pods[1].next = 1; pods[1].shieldtimer = 4;
    pods[2].px = 5000; pods[2].py = 5000; pods[2].next = 1;
    pods[3].px = 6000; pods[3].py = 6000; pods[3].next = 1;
    simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

    turn = 0;
    for (int i = 0; i < kPodCount; ++i) { pods[i] = {}; pods[i].next = 0; pods[i].px = 10000; pods[i].py = 10000; }
    pods[0].px = 1000 - 700; pods[0].py = 1000; pods[0].vx = 500; pods[0].next = 0;
    timeout[0] = timeout[1] = kTimeoutLimit;
    simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

    for (int i = 0; i < kPodCount; ++i) { pods[i] = {}; pods[i].next = 3; pods[i].px = 1000; pods[i].py = 8000; }
    pods[0].px = 1000 - 100; pods[0].vx = 200; pods[0].next = 3;
    timeout[0] = timeout[1] = kTimeoutLimit; turn = 0;
    simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

    for (int i = 0; i < kPodCount; ++i) { pods[i] = {}; pods[i].next = 0; pods[i].px = 10000; pods[i].py = 10000; }
    pods[2].px = 1000 - 700; pods[2].py = 1000; pods[2].vx = 500; pods[2].next = 0;
    timeout[0] = timeout[1] = kTimeoutLimit; turn = 0;
    simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

    for (int i = 0; i < kPodCount; ++i) { pods[i] = {}; pods[i].px = 100; pods[i].py = 100; pods[i].next = 1; }
    pods[0].vx = 10; pods[1].vx = -10;
    timeout[0] = timeout[1] = kTimeoutLimit; turn = 0;
    simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);
}

static void cover_fast_simulate() {
    using namespace csb::fast;
    Pod pods[4];
    for (int i = 0; i < 4; ++i) {
        pods[i] = {}; pods[i].id = i; pods[i].team = i < 2 ? 0 : 1;
        pods[i].pos = {1000.0 + i * 50, 1000.0}; pods[i].vel = {200.0, 0.0}; pods[i].angle = 0;
    }
    g_friendly_collision = false; SimulateTurn(pods);
    pods[0].pos = {0, 0}; pods[0].vel = {300, 0}; pods[0].id = 0;
    pods[1].pos = {700, 0}; pods[1].vel = {-300, 0}; pods[1].id = 1;
    pods[2].pos = {5000, 5000}; pods[2].id = 2; pods[3].pos = {6000, 6000}; pods[3].id = 3;
    g_friendly_collision = false; SimulateTurn(pods);
    pods[0].shield_cd = 4; pods[1].shield_cd = 4;
    pods[0].pos = {0, 0}; pods[0].vel = {400, 0};
    pods[1].pos = {750, 0}; pods[1].vel = {-400, 0};
    SimulateTurn(pods);
    Pod a{}, b{}; a.pos = {0, 0}; b.pos = {10000, 0}; CHECK(GetCollisionTime(a, b) < 0);
    a.pos = {0, 0}; a.vel = {0, 0}; b.pos = {1000, 0}; b.vel = {0, 0}; CHECK(GetCollisionTime(a, b) < 0);
    a.vel = {10, 0}; b.vel = {10, 0}; b.pos = {900, 0}; (void)GetCollisionTime(a, b);
    Pod m{}; m.shield_cd = 4; CHECK(m.Mass() == kShieldMassFast);
    m.shield_cd = 0; CHECK(m.Mass() == kNormalMassFast);
    m.Move(0.5); m.EndTurn();
}

static void cover_fast_physics_game() {
    using namespace csb::fast_physics;
    Game g;
    g.clear();
    const double xy[] = {1000, 1000, 5000, 1000, 5000, 5000, 1000, 5000};
    g.setTrack(xy, 4, 3);
    for (int i = 0; i < 4; ++i) g.setPod(i, 2000 + i * 50, 2000, 100, 0, 0.0, 1, 0, 0);
    Move moves[4];
    for (int i = 0; i < 4; ++i) { moves[i] = {}; moves[i].tx = 5000; moves[i].ty = 1000; moves[i].thrust = 200; }
    g.step(moves);
    moves[0].shield = true; moves[0].thrust = 0; g.step(moves);
    moves[0].shield = false; moves[0].boost = true; g.step(moves);
    moves[0].boost = false; moves[0].thrust = 0; g.step(moves);
    for (int t = 0; t < 5; ++t) g.step(moves);
    Snapshot snap; g.saveSnapshot(snap); g.restoreSnapshot(snap);
    Game g2; g2.copyFrom(g);
}

static void cover_fidelity_game() {
    using namespace csb;
    Game g;
    std::vector<Point> cps = {{1000, 1000}, {8000, 1000}, {8000, 7000}, {1000, 7000}};
    g.initialize(cps, 3);
    for (int i = 0; i < 4; ++i) g.setPodState(i, 2000 + i * 40, 2000, 50, 0, 0.0, 1, 0, 0);
    g.applyAction(0, 2000, 1000, "200");
    g.applyAction(1, 2000, 1000, "200");
    g.applyAction(2, 8000, 1000, "200");
    g.applyAction(3, 8000, 1000, "200");
    g.nextTurn();
    g.applyAction(0, 2000, 1000, "BOOST");
    g.applyAction(1, 2000, 1000, "SHIELD");
    g.applyAction(2, 8000, 1000, "0");
    g.applyAction(3, 8000, 1000, "100");
    g.nextTurn();
    g.step(StepOptions{PhysicsProfile::Fidelity});
    g.step(StepOptions{PhysicsProfile::Fast});
    g.applyAction(0, 1000, 1000, "99999");
    g.applyAction(1, 1000, 1000, "not_a_move");
    g.applyAction(-1, 0, 0, "0");  // invalid pod idx
    g.nextTurn();
    (void)g.winner();
}

static void cover_maps_catalog() {
    CHECK(GetTournamentMapCount() == 18);
    const auto& maps = GetTournamentMapsRaw();
    CHECK(!maps.empty());
    for (const auto& m : maps) { CHECK(!m.empty()); (void)m[0].x; }
    (void)csb_constants::kFriction;
    (void)csb_constants::kBoostThrust;
}

static void cover_engine_api() {
    InitLUT();
    SeedRand(42);
    (void)FastRand();
    (void)FastRandInt(0, 10);
    Timer t; t.Start(); (void)t.ElapsedMs();
    Vec2 a(1, 2), b(4, 6);
    (void)a.Add(b); (void)a.Sub(b); (void)a.Distance(b); (void)a.DistanceSq(b);
    CHECK(std::fabs(GameEngine::NormalizeAngle(370.0) - 10.0) < 1e-9);
    CHECK(std::fabs(GameEngine::NormalizeAngle(-10.0) - 350.0) < 1e-9);
    CHECK(std::fabs(GameEngine::ShortestAngleDiff(350, 10) - 20.0) < 1e-9);
    CHECK(std::fabs(GameEngine::RadToDeg(M_PI) - 180.0) < 1e-6);
    CHECK(Round(1.4) == 1.0);
    Pod p;
    p.pos = {0, 0}; p.vel = {0, 0}; p.angle = -1;
    p.ApplyGAAction(5, 200);
    p.ApplyGAAction(0, -1);
    p.ApplyGAAction(0, 650);
    p.angle = 0;
    p.ApplyServerAction(100, 0, 200);
    p.ApplyServerAction(0, 0, 200);
    p.ApplyServerAction(100, 100, 650);
    p.boost_available = false;
    p.ApplyServerAction(100, 100, 650);
    p.Move(0.5);
    p.EndTurn();
    (void)p.Mass();
    Pod pods[4];
    for (int i = 0; i < 4; ++i) {
        pods[i] = Pod(); pods[i].id = i; pods[i].team = i < 2 ? 0 : 1;
        pods[i].pos = {1000.0 + i * 100, 1000}; pods[i].vel = {100, 0}; pods[i].angle = 0;
    }
    FastSimulateTurn(pods);
}

int main() {
    cover_ga_pure_all_branches();
    cover_progress_all_branches();
    cover_fidelity_math_all_branches();
    cover_world_step_scenarios();
    cover_fast_simulate();
    cover_fast_physics_game();
    cover_fidelity_game();
    cover_maps_catalog();
    cover_engine_api();
    if (g_fails) { std::cerr << "src_branch_suite fails=" << g_fails << "\n"; return 1; }
    std::cout << "src_branch_suite: all checks ok\n";
    return 0;
}
