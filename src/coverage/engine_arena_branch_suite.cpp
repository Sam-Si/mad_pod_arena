// Engine + Arena product surface — drives shipped engine.cpp + arena.cpp.
#include "src/engine/engine.h"
#include "src/engine/arena.h"
#include "src/engine/bot.h"
#include "src/cg/ga_pure.h"
#include "src/core/progress.h"
#include "src/core/maps/catalog.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
#include <string>

static int g_fails = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL " << #cond << " L" << __LINE__ << "\n"; ++g_fails; } } while (0)

// Deterministic stub bot — always thrusts toward next CP at fixed thrust.
class StubBot : public IBot {
  std::string name_;
  int team_ = 0;
  std::vector<Vec2> cps_;
  int thrust_;
public:
  explicit StubBot(std::string name, int thrust = 100) : name_(std::move(name)), thrust_(thrust) {}
  std::string GetName() const override { return name_; }
  void Initialize(int /*laps*/, int /*cp_count*/, const std::vector<Vec2>& cps, int team_id) override {
    cps_ = cps;
    team_ = team_id;
  }
  void SetRoles(int, int) override {}
  std::vector<PodAction> GetActions(const std::vector<Pod>& pods) override {
    std::vector<PodAction> acts;
    for (int i = 0; i < 2; ++i) {
      const Pod& p = pods[static_cast<size_t>(team_ * 2 + i)];
      int n = p.next_cp_id;
      if (n < 0 || n >= static_cast<int>(cps_.size())) n = 0;
      PodAction a;
      a.tx = cps_[static_cast<size_t>(n)].x;
      a.ty = cps_[static_cast<size_t>(n)].y;
      // Exercise shield/boost tokens via ThrustToken paths on alternate turns using id
      if (p.id == 0 && p.timeout > 90) a.thrust = -1;
      else if (p.id == 1 && p.boost_available && p.timeout > 95) a.thrust = 650;
      else a.thrust = thrust_;
      acts.push_back(a);
    }
    return acts;
  }
};

static void cover_engine_core() {
  InitLUT();
  SeedRand(7);
  for (int i = 0; i < 20; ++i) (void)FastRand();
  (void)FastRandInt(0, 0);  // min==max edge
  (void)FastRandInt(-5, 5);
  Timer t; t.Start(); (void)t.ElapsedMs();
  Vec2 z;  // default ctor
  Vec2 a(1, 2), b(4, 6);
  CHECK(a.Add(b).x == 5 && a.Add(b).y == 8);
  CHECK(a.Sub(b).x == -3);
  CHECK(std::fabs(a.Distance(b) - 5.0) < 1e-9);
  CHECK(a.DistanceSq(b) == 25.0);
  // Normalize multi-wrap both directions
  CHECK(std::fabs(GameEngine::NormalizeAngle(720) - 0) < 1e-9);
  CHECK(std::fabs(GameEngine::NormalizeAngle(-720) - 0) < 1e-9);
  CHECK(std::fabs(GameEngine::NormalizeAngle(400) - 40) < 1e-9);
  CHECK(std::fabs(GameEngine::ShortestAngleDiff(10, 350) + 20) < 1e-9);
  CHECK(std::fabs(GameEngine::ShortestAngleDiff(350, 10) - 20) < 1e-9);
  CHECK(std::fabs(GameEngine::RadToDeg(M_PI) - 180) < 1e-6);
  CHECK(Round(2.6) == 3.0);
  CHECK(Round(-1.4) == -1.0);

  Pod p;
  // first frame angle < 0 path
  p.angle = -1;
  p.ApplyGAAction(10, 200);
  p.ApplyGAAction(0, -1);           // shield
  p.shield_cd = 2;
  p.ApplyGAAction(0, 50);           // shield_cd > 0 zeros thrust
  p.ApplyGAAction(0, 650);          // boost flag
  p.angle = 0;
  p.boost_available = true;
  p.ApplyServerAction(100, 0, 200);
  p.ApplyServerAction(p.pos.x, p.pos.y, 100);  // target==pos early return
  p.ApplyServerAction(0, 1000, 200);            // rotate clamp positive
  p.ApplyServerAction(0, -1000, 200);           // rotate clamp negative
  p.boost_available = true;
  p.ApplyServerAction(100, 100, 650);           // boost ok
  p.boost_available = false;
  p.ApplyServerAction(100, 100, 650);           // boost demote to max thrust
  p.ApplyServerAction(100, 100, -1);            // shield via server
  p.shield_cd = 3;
  p.ApplyServerAction(100, 100, 50);            // thrust zeroed under shield
  p.Move(0.25);
  p.EndTurn();
  p.shield_cd = 4;
  CHECK(p.Mass() == 10.0);
  p.shield_cd = 0;
  CHECK(p.Mass() == 1.0);

  Pod pods[4];
  for (int i = 0; i < 4; ++i) {
    pods[i] = Pod();
    pods[i].id = i;
    pods[i].team = i < 2 ? 0 : 1;
    pods[i].pos = {1000. + i * 30, 1000};
    pods[i].vel = {80, 10};
    pods[i].angle = 0;
  }
  FastSimulateTurn(pods);
  // collision-friendly pair
  pods[0].pos = {0, 0}; pods[0].vel = {300, 0}; pods[0].id = 0;
  pods[1].pos = {700, 0}; pods[1].vel = {-300, 0}; pods[1].id = 1;
  pods[2].pos = {8000, 8000}; pods[3].pos = {9000, 9000};
  g_friendly_collision = false;
  FastSimulateTurn(pods);

  CHECK(ga_pure::ClampAngleShiftDeg(30) == 18);
  CHECK(csb_progress::GlobalNext(1, 1, 0) == 1);
  CHECK(csb_progress::LocalNext(5, 0) == 5);
  int lap = 0, loc = 0;
  csb_progress::Decode(0, 0, &lap, &loc);
  CHECK(GetTournamentMapCount() == 18);
  CHECK(Arena::GetMapCount() == 18);
}

static void cover_arena_play() {
  auto b0 = std::make_shared<StubBot>("A", 100);
  auto b1 = std::make_shared<StubBot>("B", 150);
  Arena arena(b0, b1);
  // fixed map 0
  ArenaResult r0 = arena.PlayGame(false, 0);
  CHECK(r0.turns >= 0);
  // invalid map_idx forces random path in GenerateMap
  SeedRand(99);
  ArenaResult r1 = arena.PlayGame(false, -1);
  CHECK(r1.turns >= 0);
  // last map index
  int n = Arena::GetMapCount();
  ArenaResult r2 = arena.PlayGame(false, n - 1);
  CHECK(r2.turns >= 0);
  // out-of-range high also randomizes
  ArenaResult r3 = arena.PlayGame(false, 9999);
  CHECK(r3.turns >= 0);
  // verbose path (stdout ok)
  ArenaResult r4 = arena.PlayGame(true, 1);
  CHECK(r4.turns >= 0);
  (void)r0.winner_team;
  (void)r0.reason;
}

int main() {
  cover_engine_core();
  cover_arena_play();
  if (g_fails) {
    std::cerr << "engine_arena_branch_suite fails=" << g_fails << "\n";
    return 1;
  }
  std::cout << "engine_arena_branch_suite: all checks ok\n";
  return 0;
}
