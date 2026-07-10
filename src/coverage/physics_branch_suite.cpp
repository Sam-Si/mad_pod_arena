// Branch suite: core + fidelity + fast + ga_pure (shipped entry points).
#define CSB_PHYSICS_NO_GLOBAL_USING 1
#include "src/cg/ga_pure.h"
#include "src/core/progress.h"
#include "src/core/constants.h"
#include "src/core/maps/catalog.h"
#include "src/physics/fidelity_math.h"
#include "src/physics/fidelity_world_step.h"
#include "src/physics/fast.h"
#include "src/physics/fast_physics.h"
#include "src/physics/physics.h"
#include <cmath>
#include <iostream>
#include <vector>

static int g_fails = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL " #cond " L" << __LINE__ << "\n"; ++g_fails; } } while (0)
thread_local bool g_friendly_collision = false;

static void cover_ga_pure() {
  using namespace ga_pure;
  CHECK(std::fabs(NormalizeAngleDeg(720.0)) < 1e-9);
  CHECK(std::fabs(NormalizeAngleDeg(-720.0)) < 1e-9);
  CHECK(std::fabs(NormalizeAngleDeg(370.0) - 10.0) < 1e-9);
  CHECK(std::fabs(ShortestAngleDiffDeg(0.0, 400.0) - 40.0) < 1e-9);
  CHECK(std::fabs(ShortestAngleDiffDeg(0.0, -400.0) + 40.0) < 1e-9);
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
  CHECK(CpCrossedBonus(-1) == 0.0);
  CHECK(CpCrossedBonus(3) == 45000.0);
  CHECK(DistWeightTerm(2, 3) == 6.0);
  CHECK(AlignTerm(1, 0, 1, 0, 2) == 2.0);
  CHECK(LateralTerm(0, 1, 1, 0, 2) == 2.0);
  CHECK(AngleErrorTerm(2, 3) == 6.0);
  CHECK(SpeedTerm(0, 0, 5) == 0.0);
  CHECK(FastActPenalty(0) == 0.0);
  CHECK(FastActPenalty(1.5, 10) == 15.0);
  CHECK(FreeFlightFrictionVel(200) == 170.0);
  (void)RunnerKinematicsScore(1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1);
  (void)RunnerKinematicsScore(0, 0, 10, 0, 0, 100, 0, 1, 1, 1, 1);
}

static void cover_progress() {
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

static void cover_math() {
  using namespace csb;
  CHECK(std::fabs(goMod(5.5, 2.0) - 1.5) < 1e-12);
  CHECK(roundHalfUp(1.4) == 1.0);
  CHECK(roundHalfUp(1.5) == 2.0);
  CHECK(frictionTrunc(200) == 170);
  CHECK(snapNearInteger(180) == 180);
  CHECK(snapNearInteger(-180) == -180);
  CHECK(std::fabs(snapNearInteger(1.0 + 1e-15) - 1.0) < 1e-12);
  CHECK(snapNearInteger(1.5) == 1.5);
  (void)getAngle(0, 0, 1, 0);
  CHECK(cpCollide(0, 0, 0, 0, 10000, 10000) == false);
  CHECK(cpCollide(0, 0, 0, 0, 0, 0) == true);
  CHECK(cpCollide(0, 0, 1000, 0, 2000, 0) == false);
  CHECK(cpCollide(0, 0, 1000, 0, 500, 0) == true);
  // u<=0: closest is start (0,0); CP at (-100,0) is within 600 radius → hit
  CHECK(cpCollide(0, 0, 1000, 0, -100, 0) == true);
  // u<=0 miss: CP far from start
  CHECK(cpCollide(0, 0, 1000, 0, -10000, 0) == false);
  CHECK(cpCollide(0, 0, 10, 0, 0, 600.0) == false);
  CHECK(newCollideTime(0, 0, 0, 0, 100, 0, 0, 0) == 0.0);
  CHECK(newCollideTime(0, 0, 0, 0, 2000, 0, 100, 0) == 10.0);
  CHECK(newCollideTime(0, 0, 0, 0, 2000, 0, 0, 0) == 10.0);
  (void)newCollideTime(0, 0, 100, 0, 2000, 0, -100, 0);
  CHECK(newCollideTime(0, 0, 100, 0, 0, 5000, 100, 0) == 10.0);
}

static void cover_world() {
  using namespace csb;
  WorldPod pods[kPodCount];
  double gcx[8] = {1000, 5000, 9000, 1000};
  double gcy[8] = {1000, 1000, 5000, 8000};
  int timeout[2] = {kTimeoutLimit, kTimeoutLimit};
  int turn = 0;
  for (int i = 0; i < kPodCount; i++) {
    pods[i] = {};
    pods[i].px = 2000 + i * 50;
    pods[i].py = 2000;
    pods[i].vx = 100;
    pods[i].next = 1;
  }
  simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);
  CHECK(turn == 1);

  turn = 0;
  timeout[0] = timeout[1] = kTimeoutLimit;
  for (int i = 0; i < kPodCount; i++) pods[i] = {};
  pods[0].px = 0; pods[0].vx = 400; pods[0].next = 1; pods[0].shieldtimer = 4;
  pods[1].px = 900; pods[1].vx = -400; pods[1].next = 1; pods[1].shieldtimer = 4;
  pods[2].px = 5000; pods[2].py = 5000; pods[2].next = 1;
  pods[3].px = 6000; pods[3].py = 6000; pods[3].next = 1;
  simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

  turn = 0;
  for (int i = 0; i < kPodCount; i++) {
    pods[i] = {};
    pods[i].next = 0;
    pods[i].px = 10000;
    pods[i].py = 10000;
  }
  pods[0].px = 300; pods[0].py = 1000; pods[0].vx = 500; pods[0].next = 0;
  timeout[0] = timeout[1] = kTimeoutLimit;
  simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

  for (int i = 0; i < kPodCount; i++) {
    pods[i] = {};
    pods[i].next = 3;
    pods[i].px = 1000;
    pods[i].py = 8000;
  }
  pods[0].px = 900; pods[0].vx = 200; pods[0].next = 3;
  turn = 0;
  timeout[0] = timeout[1] = kTimeoutLimit;
  simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

  for (int i = 0; i < kPodCount; i++) {
    pods[i] = {};
    pods[i].next = 0;
    pods[i].px = 10000;
    pods[i].py = 10000;
  }
  pods[2].px = 300; pods[2].py = 1000; pods[2].vx = 500; pods[2].next = 0;
  turn = 0;
  timeout[0] = timeout[1] = kTimeoutLimit;
  simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);

  for (int i = 0; i < kPodCount; i++) {
    pods[i] = {};
    pods[i].px = 100;
    pods[i].py = 100;
    pods[i].next = 1;
  }
  pods[0].vx = 10;
  pods[1].vx = -10;
  turn = 0;
  timeout[0] = timeout[1] = kTimeoutLimit;
  simulateFidelityWorld(pods, gcx, gcy, 4, timeout, &turn);
}

static void cover_fast() {
  csb::fast::Pod pods[4];
  for (int i = 0; i < 4; i++) {
    pods[i] = {};
    pods[i].id = i;
    pods[i].team = i < 2 ? 0 : 1;
    pods[i].pos = {1000. + i * 50, 1000};
    pods[i].vel = {200, 0};
    pods[i].angle = 0;
  }
  g_friendly_collision = false;
  csb::fast::SimulateTurn(pods);

  pods[0].pos = {0, 0}; pods[0].vel = {300, 0}; pods[0].id = 0;
  pods[1].pos = {700, 0}; pods[1].vel = {-300, 0}; pods[1].id = 1;
  pods[2].pos = {5000, 5000}; pods[2].id = 2;
  pods[3].pos = {6000, 6000}; pods[3].id = 3;
  g_friendly_collision = false;
  csb::fast::SimulateTurn(pods);

  pods[0].shield_cd = 4; pods[1].shield_cd = 4;
  pods[0].pos = {0, 0}; pods[0].vel = {400, 0};
  pods[1].pos = {750, 0}; pods[1].vel = {-400, 0};
  csb::fast::SimulateTurn(pods);

  csb::fast::Pod a{}, b{};
  a.pos = {0, 0}; b.pos = {10000, 0};
  CHECK(csb::fast::GetCollisionTime(a, b) < 0);
  a.vel = {0, 0}; b.pos = {1000, 0}; b.vel = {0, 0};
  CHECK(csb::fast::GetCollisionTime(a, b) < 0);
  a.vel = {10, 0}; b.vel = {10, 0}; b.pos = {900, 0};
  (void)csb::fast::GetCollisionTime(a, b);
  a.pos = {0, 0}; a.vel = {200, 0}; b.pos = {1600, 0}; b.vel = {-200, 0};
  (void)csb::fast::GetCollisionTime(a, b);
  a.pos = {0, 0}; a.vel = {-10, 0}; b.pos = {2000, 0}; b.vel = {10, 0};
  (void)csb::fast::GetCollisionTime(a, b);

  csb::fast::Pod m{};
  m.shield_cd = 4;
  CHECK(m.Mass() == csb::fast::kShieldMassFast);
  m.shield_cd = 0;
  CHECK(m.Mass() == csb::fast::kNormalMassFast);
  m.Move(0.5);
  m.EndTurn();
}

static void cover_fp_game() {
  csb::fast_physics::Game g;
  g.clear();
  const double xy[] = {1000, 1000, 5000, 1000, 5000, 5000, 1000, 5000};
  g.setTrack(xy, 0, 0);
  g.setTrack(xy, 4, 3);
  for (int i = 0; i < 4; i++) g.setPod(i, 2000 + i * 50, 2000, 100, 0, 0.0, 1, 0, 0);
  csb::fast_physics::Move moves[4];
  for (int i = 0; i < 4; i++) {
    moves[i] = {};
    moves[i].tx = 5000;
    moves[i].ty = 1000;
    moves[i].thrust = 200;
  }
  g.step(moves);
  moves[0].shield = true; moves[0].thrust = 0; g.step(moves);
  moves[0].shield = false; moves[0].boost = true; g.step(moves);
  moves[0].boost = false; moves[0].thrust = 0; g.step(moves);
  moves[0].thrust = 200;
  moves[0].tx = g.pods[0].px;
  moves[0].ty = g.pods[0].py;
  g.step(moves);
  for (int t = 0; t < 8; t++) g.step(moves);
  csb::fast_physics::Snapshot s;
  g.saveSnapshot(s);
  g.restoreSnapshot(s);
  csb::fast_physics::Game g2;
  g2.copyFrom(g);
  moves[0].tx = 0; moves[0].ty = 10000; moves[0].thrust = 200;
  g.step(moves);
}

static void cover_fid_game() {
  csb::Game g;
  std::vector<csb::Point> cps = {{1000, 1000}, {8000, 1000}, {8000, 7000}, {1000, 7000}};
  g.initialize(cps, 3);
  for (int i = 0; i < 4; i++) g.setPodState(i, 2000 + i * 40, 2000, 50, 0, 0.0, 1, 0, 0);
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
  g.step(csb::StepOptions{csb::PhysicsProfile::Fidelity});
  g.step(csb::StepOptions{csb::PhysicsProfile::Fast});
  g.applyAction(0, 1000, 1000, "99999");
  g.applyAction(1, 1000, 1000, "not_a_move");
  g.applyAction(-1, 0, 0, "0");
  g.applyAction(0, 1000, 1000, "-5");
  g.nextTurn();
  (void)g.teamAlive(0);
  (void)g.teamAlive(1);
  g.setPlayerTimeouts(50, 50);
  // empty track intentionally not exercised (initializeFromTrack assumes non-empty track)
}

static void cover_maps() {
  CHECK(GetTournamentMapCount() == 18);
  const auto& maps = GetTournamentMapsRaw();
  CHECK(!maps.empty());
  for (const auto& m : maps) {
    CHECK(!m.empty());
    (void)m[0].x;
  }
  (void)csb_constants::kFriction;
  (void)csb_constants::kShieldMassFactorFidelity;
}

// Extra public API surface for branch/function coverage of remaining methods.
static void cover_remaining_public_apis() {
  using namespace csb;
  Point a{0, 0}, b{3, 4};
  (void)a.norm();
  (void)a.dot(b);
  (void)a.dist(b);
  (void)(a == b);
  (void)(a == a);

  Pod pod{};
  pod.p = {0, 0};
  pod.s = {10, 0};
  pod.angle = 0;
  (void)pod.diffAngle(Point{100, 0});
  pod.applyRotate(Point{100, 100});
  pod.applyRotate(Point{0, 0});  // same point path if any
  pod.applyThrust(200);
  pod.applyThrust(0);
  pod.applyThrust(kBoostThrust);
  (void)pod.frictionTrunc(100);
  pod.endTurn();
  Pod other = pod;
  other.p = {500, 0};
  (void)pod.newCollide(&other, kPodCollisionRsq);
  int to[2] = {100, 100};
  pod.passCheckpoint(0, 10, to);
  pod.passCheckpoint(2, 10, to);
  (void)pod.localNextCp(4);
  (void)pod.localNextCp(0);

  Game g;
  std::vector<Point> cps = {{1000, 1000}, {5000, 1000}, {5000, 5000}};
  g.initialize(cps, 2);
  for (int i = 0; i < 4; i++) g.setPodState(i, 1500 + i * 30, 1500, 80, 10, 0.1, 1, 0, 0);
  PlayerMove pm[4];
  for (int i = 0; i < 4; i++) {
    pm[i] = {};
    pm[i].target = {5000, 1000};
    pm[i].thrust = 100;
    pm[i].valid = true;
  }
  g.step(pm);
  (void)g.teamWon(0);
  (void)g.teamWon(1);
  (void)g.checkWinner();

  PlayerMove parsed = parseMove("100 200 50");
  (void)parsed;
  parsed = parseMove("SHIELD");
  parsed = parseMove("BOOST");
  parsed = parseMove("bad");
  (void)cpCollide(Point{0, 0}, Point{10, 0}, Point{5, 0});
  (void)almostEq(1.0, 1.0 + 1e-12, 1e-9);
  (void)almostEq(1.0, 2.0, 1e-9);

  PodSnapshot snap{};
  snap.x = g.pods[0].p.x;
  snap.y = g.pods[0].p.y;
  snap.vx = g.pods[0].s.x;
  snap.vy = g.pods[0].s.y;
  snap.angle = g.pods[0].angle;
  snap.next_cp = g.pods[0].next;
  (void)comparePod(g.pods[0], snap, 5, 3, 0.01, true, 0);

  // fast_physics remaining methods
  csb::fast_physics::Game fg;
  fg.clear();
  const double xy[] = {1000, 1000, 8000, 1000, 8000, 7000, 1000, 7000};
  fg.setTrack(xy, 4, 3);
  for (int i = 0; i < 4; i++) fg.setPod(i, 2000, 2000 + i * 40, 50, 0, 0.0, 1);
  fg.setTimeouts(80, 80);
  csb::fast_physics::Pod& p0 = fg.pods[0];
  fg.applyRotateByClampedDelta(p0, 0.5);
  fg.applyRotateByClampedDelta(p0, 10.0);  // clamp
  fg.applyAbsoluteAngle(p0, 1.0);
  fg.applyGAActionDegrees(p0, 5.0, 200);
  fg.applyGAActionDegrees(p0, -30.0, -1);  // shield-ish path if thrust -1 handled
  csb::fast_physics::Move batch[8];
  for (int i = 0; i < 8; i++) {
    batch[i] = {};
    batch[i].tx = 5000;
    batch[i].ty = 1000;
    batch[i].thrust = 100;
  }
  csb::fast_physics::Game games[2];
  games[0].copyFrom(fg);
  games[1].copyFrom(fg);
  csb::fast_physics::step_batch(games, batch, 2);
  (void)csb::fast_physics::statesEqual(games[0], games[1], 1e-6);
  games[1].pods[0].px += 100;
  (void)csb::fast_physics::statesEqual(games[0], games[1], 1e-6);

  // More multi-collision free-flight pressure on world step
  WorldPod w[kPodCount];
  double gcx[4] = {1000, 5000, 9000, 1000};
  double gcy[4] = {1000, 1000, 5000, 8000};
  int timeout[2] = {kTimeoutLimit, kTimeoutLimit};
  int turn = 0;
  for (int i = 0; i < kPodCount; i++) {
    w[i] = {};
    w[i].px = 1000 + i * 10;
    w[i].py = 1000 + i * 5;
    w[i].vx = 50 * (i % 2 == 0 ? 1 : -1);
    w[i].vy = 30 * (i % 2 == 0 ? -1 : 1);
    w[i].next = 1;
    w[i].shieldtimer = (i == 0) ? 4 : 0;
  }
  for (int k = 0; k < 20; k++) simulateFidelityWorld(w, gcx, gcy, 4, timeout, &turn);

  // Exact-on-circle CP edge (dist^2 == rsq path in world epilogue)
  for (int i = 0; i < kPodCount; i++) {
    w[i] = {};
    w[i].px = 1000 + 600;  // exactly on circle of CP0 if next points there — adjust
    w[i].py = 1000;
    w[i].vx = 0;
    w[i].vy = 0;
    w[i].next = 0;
  }
  timeout[0] = timeout[1] = kTimeoutLimit;
  simulateFidelityWorld(w, gcx, gcy, 4, timeout, &turn);
}

int main() {
  cover_ga_pure();
  cover_progress();
  cover_math();
  cover_world();
  cover_fast();
  cover_fp_game();
  cover_fid_game();
  cover_maps();
  cover_remaining_public_apis();
  if (g_fails) {
    std::cerr << "physics_branch_suite fails=" << g_fails << "\n";
    return 1;
  }
  std::cout << "physics_branch_suite: all checks ok\n";
  return 0;
}
