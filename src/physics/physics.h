#pragma once
// =============================================================================
// Coders Strike Back — referee-faithful physics simulator
// =============================================================================
// Single source of truth for game physics. Mirrors the Go referee in
// third_party/referees/coders-strike-back-referee/csbref.go and is validated
// against Codingame leaderboard battle frames (battles/leaderboard_battles).
//
// Turn order (per pod actions, then world step):
//   1. Parse move: SHIELD / BOOST / thrust (int). Clamp thrust to [0,200] for
//      normal output; BOOST=650 if still available else 200; SHIELD sets timer=4.
//   2. If shieldtimer > 0, thrust is forced to 0.
//   3. If target == position, skip rotate+thrust entirely.
//   4. Rotate: first turn snaps via applyRotateFirst(diffAngle); else applyRotate
//      with max 18° (Go-style diffAngle via double-mod).
//   5. Apply thrust along facing (double angle → cos/sin → add to velocity).
//   6. nextTurn: collisions entirely in double; then endTurn rounds COORDINATES:
//      velocity trunc(v*0.85), position round-half-up — the only integer commit.
//
// Angle vs coordinates (user/CG contract):
//   - Facing angle is ALWAYS kept as double (radians). Never snap angle to int
//     degrees — that is not how CG commits state and breaks gate A.
//   - When angle is *applied* into space (thrust → velocity, integrate → position),
//     the committed game-turn result must be CG-accurate integers: pos rounded,
//     vel truncated. That is the "make it a coordinate → ensure rounded" rule.
//
// CodinGame battle JSON framing — verify one GAME TURN, not one JSON frame:
//   1 game turn  ==  2 frames in the replay JSON
//     Frame 0              init keyframe (not a game turn)
//     Frame 2T+1           player 0 stdout for game turn T  (often non-keyframe)
//     Frame 2T+2           player 1 stdout + keyframe view AFTER game turn T
//   Physics steps once per game turn; compare to that post-turn keyframe only.
// =============================================================================

#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <sstream>
#include <cctype>
#include <cassert>

#include "fast.h"
#include "fidelity_math.h"
#include "fidelity_world_step.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace csb {

// ---- constants: single numeric SSOT via fidelity_math.h → core/constants.h ----
// (kPodRadius, kFriction, kMaxRotate, kSnap*, … declared in fidelity_math.h)

// Legacy aliases (existing call sites)
inline constexpr double podRSQ = kPodCollisionRsq;
inline constexpr double cpRSQ = kCpRsq;
inline constexpr int podCount = kPodCount;
inline constexpr double minImpulse = kMinImpulse;
inline constexpr double frictionVal = kFriction;
inline constexpr double radToDeg = kRadToDeg;
inline constexpr double degToRad = kDegToRad;
inline constexpr double maxRotate = kMaxRotate;
inline constexpr double EPSILON = kEpsilon;

// ---- math -------------------------------------------------------------------
struct Point {
    double x = 0.0;
    double y = 0.0;

    double norm() const { return std::sqrt(x * x + y * y); }
    double dot(const Point& n) const { return x * n.x + y * n.y; }
    double dist(const Point& n) const {
        double dx = x - n.x, dy = y - n.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

inline double getAngle(const Point& start, const Point& end) {
    return csb::getAngle(start.x, start.y, end.x, end.y);
}

// goMod / roundHalfUp / frictionTrunc / snapNearInteger / getAngle / cpCollide /
// newCollideTime: see fidelity_math.h (Extract Function / Duplicated Code).

// ---- move -------------------------------------------------------------------
struct PlayerMove {
    Point target{0.0, 0.0};
    int thrust = 0;      // numeric thrust as-is (may be <0 or >200 for InvalidInput)
    bool shield = false;
    bool boost = false;
    bool valid = true;   // false if line incomplete / unparsable
    bool invalid_input = false;  // thrust < 0 or > 200 (or non-numeric non-keyword)
};

// Parse a single bot output line: "x y thrust|SHIELD|BOOST [optional comment]"
// Numeric thrust is passed through as-is. Incomplete lines => valid=false, thrust 0.
// thrust < 0 or > 200 => invalid_input (skip rotate+thrust; no shield activation).
inline PlayerMove parseMove(const std::string& line) {
    PlayerMove m;
    std::stringstream ss(line);
    double tx = 0, ty = 0;
    std::string thr;
    if (!(ss >> tx >> ty)) {
        m.valid = false;
        return m;
    }
    m.target = {tx, ty};
    if (!(ss >> thr)) {
        // Incomplete: target only (seen in scraped battles). Treat as thrust 0.
        m.thrust = 0;
        m.valid = false;
        return m;
    }
    if (thr == "SHIELD") {
        m.shield = true;
        m.thrust = 0;
    } else if (thr == "BOOST") {
        m.boost = true;
        m.thrust = 0;
    } else {
        try {
            m.thrust = std::stoi(thr);
            if (m.thrust < 0 || m.thrust > kMaxThrust) {
                m.invalid_input = true;
            }
        } catch (...) {
            m.thrust = 0;
            m.valid = false;
            m.invalid_input = true;
        }
    }
    return m;
}

// ---- pod --------------------------------------------------------------------
struct Pod {
    Point p{0.0, 0.0};
    Point s{0.0, 0.0};
    // Facing is logically "double" for API/compare, stored/computed in a way that
    // keeps full precision in `angle` (double). Coordinates (p, s) are double
    // during the turn and committed to integers only in endTurn.
    double angle = -1.0;   // radians; negative means uninitialized (pre-first turn)
    int next = 1;          // index into globalCp (track * laps + final CP0)
    int shieldtimer = 0;   // 4 on activation frame, decrements each endTurn
    int boosted = 0;       // 1 after boost consumed (per pod, not per team)
    bool won = false;
    bool hasRotated = false;  // false until first non-skipped rotate (per-pod first turn)

    // Go: math.Mod(2*da, 2*pi) - da (SSOT fidelityDiffAngle).
    double diffAngle(Point target) const {
        return fidelityDiffAngle(angle, p.x, p.y, target.x, target.y);
    }

    void canonicalizeAngle() { canonicalizeAngleRad(angle); }

    // SSOT: applyFidelityRotate / applyFidelityThrust in fidelity_math.h.
    void applyRotate(Point target) {
        applyFidelityRotate(angle, p.x, p.y, target.x, target.y);
    }

    // First successful rotate: face geometric target (CG spawn / hasRotated=false).
    void applyRotateFirst(double target_angle) {
        angle = target_angle;
        canonicalizeAngle();
    }

    static double snapNearInteger(double v, double band = 4e-14) {
        return csb::snapNearInteger(v, band);
    }

    void applyThrust(int t) { applyFidelityThrust(s.x, s.y, angle, t); }

    // End of one GAME TURN: commit CG integer coordinates (angle stays double).
    // Friction: trunc only (thrust already ULP-snapped). Exact ±100 pre-fric
    // (885827873) still mismatches CG (-85 vs -84); nextafter-on-exact-100 was
    // tried and mass-regresses gate A — do not revive without a tighter predicate.
    static double frictionTrunc(double v) { return csb::frictionTrunc(v); }

    void endTurn() {
        s.x = frictionTrunc(s.x);
        s.y = frictionTrunc(s.y);
        p.x = roundHalfUp(p.x);
        p.y = roundHalfUp(p.y);
        if (shieldtimer > 0) {
            shieldtimer--;
        }
    }

    // Time to collide with b (radius sq). Returns 0 if already overlapping,
    // 10 if no collision within this turn segment.
    double newCollide(const Pod* b, double rsq) const {
        return newCollideTime(p.x, p.y, s.x, s.y, b->p.x, b->p.y, b->s.x, b->s.y, rsq);
    }

    // Referee sets timeout so that after the mandatory end-of-turn -- it reads 100
    // on the next frame (CG frames show 100 after a pass). Go source sets 100 then
    // decrements; we set 101 so the same -- yields the observed 100.
    void passCheckpoint(int podn, int globalNumCp, int* playerTimeout) {
        next = next + 1;
        if (next >= globalNumCp) {
            next = globalNumCp - 1;
            won = true;
        }
        if (podn < 2) {
            playerTimeout[0] = kTimeoutLimit + 1;
        } else {
            playerTimeout[1] = kTimeoutLimit + 1;
        }
    }

    // Local track index as shown in CG viewer / player input (modulo track size).
    int localNextCp(int trackSize) const {
        if (trackSize <= 0) return next;
        return next % trackSize;
    }

    // Apply one player move — SSOT applyFidelityMove (fidelity_math.h).
    void applyMove(const PlayerMove& move, bool /*is_first_turn_global*/) {
        applyFidelityMove(p.x, p.y, s.x, s.y, angle, shieldtimer, boosted, hasRotated,
                          move.target.x, move.target.y, move.thrust, move.shield,
                          move.boost, move.invalid_input);
    }
};

// ---- checkpoint segment test ------------------------------------------------
// True iff the segment from previous position to current position touches the CP
// disk (movement path came strictly inside radius 600). Closest-point-on-segment
// projection matches Go referee cpCollide in csbref.go.
//
// Strict radius (< rsq), not inclusive: golden divergence battle_884515945 turn 9
// has closest dist exactly 600.0 with positions matching GT but GT does not pass;
// inclusive <= falsely advances next_cp. Go uses < as well.
inline bool cpCollide(Point previous, Point current, Point cp, double rsq = kCpRsq) {
    return csb::cpCollide(previous.x, previous.y, current.x, current.y, cp.x, cp.y, rsq);
}

// ---- profiles (SSOT) ---------------------------------------------------------
// PhysicsProfile::Fast is reserved / unsupported on Game — Fast search uses
// csb::fast::SimulateTurn on degrees pods (see fast.h). Fidelity == nextTurn().
enum class PhysicsProfile { Fidelity, Fast };

struct StepOptions {
    PhysicsProfile profile = PhysicsProfile::Fidelity;
};

// ---- game -------------------------------------------------------------------
struct Game {
    std::array<Pod, kPodCount> pods{};
    std::vector<Point> track;      // single lap checkpoints
    std::vector<Point> globalCp;   // track * laps + track[0]
    int laps = kDefaultLaps;
    int playerTimeout[2] = {kTimeoutLimit, kTimeoutLimit};
    int turn = 0;                  // completed turns (0 = not yet started)

    // Pending per-pod moves for incremental applyAction() / nextTurn() driver API.
    PlayerMove pendingMoves[kPodCount]{};
    bool hasPendingMove[kPodCount] = {false, false, false, false};

    Game() = default;

    static std::vector<Point> buildGlobalCp(const std::vector<Point>& track_in, int laps_in) {
        std::vector<Point> g;
        if (track_in.empty()) return g;
        for (int i = 0; i < laps_in; ++i) {
            for (const auto& cp : track_in) g.push_back(cp);
        }
        g.push_back(track_in[0]);
        return g;
    }

    void setTrack(const std::vector<Point>& track_in, int laps_in = kDefaultLaps) {
        track = track_in;
        laps = laps_in;
        globalCp = buildGlobalCp(track, laps);
    }

    // Alias used by replay_driver text protocol.
    void initialize(const std::vector<Point>& cps, int laps_in = kDefaultLaps) {
        initializeFromTrack(cps, laps_in);
        for (int i = 0; i < kPodCount; ++i) hasPendingMove[i] = false;
    }

    void setPlayerTimeouts(int t0, int t1) {
        playerTimeout[0] = t0;
        playerTimeout[1] = t1;
    }

    // Standard CG spawn relative to CP0→CP1 direction (Arena / Go referee).
    void initializeFromTrack(const std::vector<Point>& track_in, int laps_in = kDefaultLaps) {
        setTrack(track_in, laps_in);
        static const Point startPointMult[4] = {
            {500.0, -500.0}, {-500.0, 500.0}, {1500.0, -1500.0}, {-1500.0, 1500.0}
        };
        double dx = track[1].x - track[0].x;
        double dy = track[1].y - track[0].y;
        double dd = std::sqrt(dx * dx + dy * dy);
        Point cp1minus0{dx / dd, dy / dd};

        for (int podN = 0; podN < kPodCount; ++podN) {
            Pod& po = pods[podN];
            po.angle = -1.0 * kDegToRad;
            po.next = 1;
            po.shieldtimer = 0;
            po.boosted = 0;
            po.won = false;
            po.hasRotated = false;
            po.s = {0.0, 0.0};
            po.p.x = roundHalfUp(track[0].x + cp1minus0.y * startPointMult[podN].x);
            po.p.y = roundHalfUp(track[0].y + cp1minus0.x * startPointMult[podN].y);
        }
        playerTimeout[0] = kTimeoutLimit;
        playerTimeout[1] = kTimeoutLimit;
        turn = 0;
        for (int i = 0; i < kPodCount; ++i) hasPendingMove[i] = false;
    }

    // Seed pods from known state (battle frame 0 or mid-game resync).
    // Uninit facing (~-1° sentinel or ~0) → hasRotated=false (reference isFirstTurn).
    void setPodState(int i, double x, double y, double vx, double vy,
                     double ang_rad, int next_cp_global,
                     int shield = 0, int boost_used = 0) {
        Pod& po = pods[i];
        po.p = {x, y};
        po.s = {vx, vy};
        po.angle = ang_rad;
        po.next = next_cp_global;
        po.shieldtimer = shield;
        po.boosted = boost_used;
        po.won = false;
        po.hasRotated = !(std::fabs(ang_rad - kInitAngleSentinel) < kInitAngleSentinelTol ||
                          std::fabs(ang_rad) < kInitAngleSentinelTol);
    }

    // Incremental driver API: queue one pod's action (thrust as string).
    void applyAction(int pod_idx, int tx, int ty, const std::string& thrust_str) {
        if (pod_idx < 0 || pod_idx >= kPodCount) return;
        PlayerMove m;
        m.target = {static_cast<double>(tx), static_cast<double>(ty)};
        m.thrust = 0;
        m.shield = false;
        m.boost = false;
        m.valid = true;
        m.invalid_input = false;
        if (thrust_str == "SHIELD") {
            m.shield = true;
        } else if (thrust_str == "BOOST") {
            m.boost = true;
        } else {
            try {
                m.thrust = std::stoi(thrust_str);
                if (m.thrust < 0 || m.thrust > kMaxThrust) {
                    m.invalid_input = true;
                }
            } catch (...) {
                m.thrust = 0;
                m.valid = false;
                m.invalid_input = true;
            }
        }
        pendingMoves[pod_idx] = m;
        hasPendingMove[pod_idx] = true;
    }

    // World step only — single SSOT in fidelity_world_step.h (bounce/forwardTime live there).
    void simulateWorld() {
        WorldPod wp[kPodCount];
        for (int i = 0; i < kPodCount; ++i) {
            wp[i].px = pods[i].p.x;
            wp[i].py = pods[i].p.y;
            wp[i].vx = pods[i].s.x;
            wp[i].vy = pods[i].s.y;
            wp[i].next = pods[i].next;
            wp[i].shieldtimer = pods[i].shieldtimer;
            wp[i].won = pods[i].won;
        }
        const int n = static_cast<int>(globalCp.size());
        double gcx_buf[64];
        double gcy_buf[64];
        // global_n for multi-lap tracks is small (≤ 8*5+1); stack buffer is enough.
        for (int i = 0; i < n && i < 64; ++i) {
            gcx_buf[i] = globalCp[static_cast<size_t>(i)].x;
            gcy_buf[i] = globalCp[static_cast<size_t>(i)].y;
        }
        simulateFidelityWorld(wp, gcx_buf, gcy_buf, n, playerTimeout, &turn);
        for (int i = 0; i < kPodCount; ++i) {
            pods[i].p.x = wp[i].px;
            pods[i].p.y = wp[i].py;
            pods[i].s.x = wp[i].vx;
            pods[i].s.y = wp[i].vy;
            pods[i].next = wp[i].next;
            pods[i].shieldtimer = wp[i].shieldtimer;
            pods[i].won = wp[i].won;
        }
    }

    // Driver API: apply any pending per-pod actions then simulate the world step.
    void nextTurn() {
        const bool first = (turn == 0);
        bool any_pending = false;
        for (int i = 0; i < kPodCount; ++i) {
            if (hasPendingMove[i]) {
                any_pending = true;
                break;
            }
        }
        if (any_pending) {
            for (int i = 0; i < kPodCount; ++i) {
                if (hasPendingMove[i]) {
                    pods[i].applyMove(pendingMoves[i], first);
                    hasPendingMove[i] = false;
                }
            }
        }
        simulateWorld();
    }

    // Apply 4 moves (pods 0,1 = player0; 2,3 = player1) then simulate world step.
    void step(const PlayerMove moves[kPodCount]) {
        const bool first = (turn == 0);
        for (int i = 0; i < kPodCount; ++i) {
            pods[i].applyMove(moves[i], first);
            hasPendingMove[i] = false;
        }
        simulateWorld();
    }

    // Win / elimination helpers
    bool teamWon(int team) const {
        for (int i = team * 2; i < team * 2 + 2; ++i) {
            if (pods[i].won) return true;
        }
        return false;
    }

    bool teamAlive(int team) const {
        return playerTimeout[team] > 0;
    }

    // Returns winner team 0/1, or -1 if not finished.
    int checkWinner() const {
        bool w0 = teamWon(0), w1 = teamWon(1);
        if (w0 && w1) return -1;
        if (w0) return 0;
        if (w1) return 1;
        bool a0 = teamAlive(0), a1 = teamAlive(1);
        if (!a0 && !a1) return -1;
        if (!a0) return 1;
        if (!a1) return 0;
        return -2;  // ongoing
    }

    // Fidelity == nextTurn(). Fast is unsupported on Game (use csb::fast::SimulateTurn).
    void step(const StepOptions& opt) {
        if (opt.profile == PhysicsProfile::Fast) {
#ifndef NDEBUG
            assert(false && "Game::step(Fast) unsupported; call csb::fast::SimulateTurn");
#endif
            return;  // leave state + pendingMoves unchanged; NEVER nextTurn()
        }
        nextTurn();
    }
};

// ---- battle frame helpers (viewer / player I/O) -----------------------------
// CG viewer next_cp is global index in early frames but often equals the global
// next value directly (starts at 1, increments). Compare with tolerance on
// pos/vel; next_cp compared exactly on global index when available.

struct PodSnapshot {
    double x = 0, y = 0, vx = 0, vy = 0;
    double angle = -1.0;  // radians; -1 = null
    int next_cp = 1;
    int shield_flag = 0;  // viewer field (1 if shield active this frame)
};

inline bool almostEq(double a, double b, double tol) {
    return std::fabs(a - b) <= tol;
}

struct CompareResult {
    bool ok = true;
    std::string detail;
};

// track_size: if >0, viewer next_cp is compared as local index (global % track_size).
// CG frames store the player-visible nextCheckPointId (0..track_size-1), not the
// internal globalCp index used for multi-lap simulation.
inline CompareResult comparePod(const Pod& sim, const PodSnapshot& exp,
                                double pos_tol = 0.01, double vel_tol = 0.01,
                                double ang_tol = 0.001, bool check_ncp = true,
                                int track_size = 0) {
    CompareResult r;
    std::ostringstream oss;
    if (!almostEq(sim.p.x, exp.x, pos_tol) || !almostEq(sim.p.y, exp.y, pos_tol)) {
        r.ok = false;
        oss << "pos sim=(" << sim.p.x << "," << sim.p.y << ") exp=(" << exp.x << "," << exp.y << ") ";
    }
    if (!almostEq(sim.s.x, exp.vx, vel_tol) || !almostEq(sim.s.y, exp.vy, vel_tol)) {
        r.ok = false;
        oss << "vel sim=(" << sim.s.x << "," << sim.s.y << ") exp=(" << exp.vx << "," << exp.vy << ") ";
    }
    if (exp.angle >= 0.0 && sim.angle >= 0.0) {
        double da = sim.angle - exp.angle;
        while (da > M_PI) da -= kFullCircle;
        while (da < -M_PI) da += kFullCircle;
        if (std::fabs(da) > ang_tol) {
            r.ok = false;
            oss << "ang sim=" << sim.angle << " exp=" << exp.angle << " ";
        }
    }
    // Viewer sometimes emits next_cp=0 at game end / desync; ignore that sentinel.
    if (check_ncp && exp.next_cp != 0) {
        int sim_ncp = sim.next;
        if (track_size > 0) {
            sim_ncp = sim.next % track_size;
            // When global next lands exactly on a lap boundary, % yields 0 but viewer
            // shows track_size equivalent as 0 only at win; normal play shows 1..n-1.
            // If sim_ncp==0 and not won, treat as track_size (back at CP0 target = start of next lap)
            // Actually CG nextCheckPointId for CP0 is 0. Viewer often shows 1 at start (next is CP1).
            // Keep simple modulo; only fail if both nonzero and differ.
        }
        if (sim_ncp != exp.next_cp) {
            // Also accept direct global match (early frames / some exports)
            if (!(track_size > 0 && sim.next == exp.next_cp)) {
                r.ok = false;
                oss << "ncp sim=" << sim.next << "(loc=" << sim_ncp << ") exp=" << exp.next_cp << " ";
            }
        }
    }
    r.detail = oss.str();
    return r;
}

}  // namespace csb

// ---- global-namespace compatibility shims (legacy physics.h API) ------------
// Keep older code compiling without csb:: prefix where practical.
// Skip when included alongside engine.h (engine also defines Pod) — arena/bot.
#ifndef CSB_PHYSICS_NO_GLOBAL_USING
using csb::Point;
using csb::Pod;
using csb::Game;
using csb::PlayerMove;
using csb::cpCollide;
using csb::podRSQ;
using csb::cpRSQ;
using csb::podCount;
using csb::frictionVal;
using csb::maxRotate;
using csb::degToRad;
using csb::radToDeg;
using csb::EPSILON;
#endif
