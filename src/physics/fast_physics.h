#pragma once
// =============================================================================
// csb::fast_physics — Fidelity-exact world step, optimized for search / rollout
// =============================================================================
// Bit-level intent: same rules as csb::Game in physics.h (GATE/EXACT oracle).
// Differences: fixed buffers, no std::vector/string in hot path, aggressive
// inlining, optional GCC optimize pragmas. Validate with bench_fast_physics
// and --exact battle compare (must match Fidelity).
//
// NOT the same as csb::fast (GA collision fragment in fast.h).
// =============================================================================

// Speed knobs (-DCSB_FP_OPT_*=0/1). PAIR_REUSE default off. SINCOS off on Apple.
#ifndef CSB_FP_OPT_ALL
#define CSB_FP_OPT_ALL 1
#endif
#ifndef CSB_FP_OPT_SINCOS
#define CSB_FP_OPT_SINCOS CSB_FP_OPT_ALL
#endif
#ifndef CSB_FP_OPT_TRIG_CACHE
#define CSB_FP_OPT_TRIG_CACHE CSB_FP_OPT_ALL
#endif
#ifndef CSB_FP_OPT_FREE_FLIGHT
#define CSB_FP_OPT_FREE_FLIGHT CSB_FP_OPT_ALL
#endif
#ifndef CSB_FP_OPT_FAST_EPILOGUE
#define CSB_FP_OPT_FAST_EPILOGUE CSB_FP_OPT_ALL
#endif
#if defined(__APPLE__) && !defined(CSB_FP_FORCE_SINCOS)
#undef CSB_FP_OPT_SINCOS
#define CSB_FP_OPT_SINCOS 0
#endif


#if defined(__GNUC__) || defined(__clang__)
#pragma GCC optimize("O3,unroll-loops,inline,omit-frame-pointer")
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#if !defined(__APPLE__)
// AVX2 only (no fma target) to keep contraction aligned with physics.h.
#pragma GCC target("avx2,bmi,bmi2")
#endif
#endif
#endif

#include <cmath>
#include <cstring>
#include <cstdint>
#include "fidelity_math.h"
#include "fidelity_world_step.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace csb {
namespace fast_physics {

// Layout limits for fixed buffers (not game physics constants).
inline constexpr int kPodCount = ::csb::kPodCount;
inline constexpr int kMaxTrackCp = 8;
inline constexpr int kMaxLaps = 5;
inline constexpr int kMaxGlobalCp = kMaxTrackCp * kMaxLaps + 1;

// Physics constants: SSOT via fidelity_math.h → core/constants.h
inline constexpr double kPodCollisionRsq = ::csb::kPodCollisionRsq;
inline constexpr double kCpRsq = ::csb::kCpRsq;
inline constexpr double kCpRadius = ::csb::kCpRadius;
inline constexpr double kRadToDeg = ::csb::kRadToDeg;
inline constexpr double kMinImpulse = ::csb::kMinImpulse;
inline constexpr double kFriction = ::csb::kFriction;
inline constexpr double kFullCircle = ::csb::kFullCircle;
inline constexpr double kInvFullCircle = 1.0 / kFullCircle;
inline constexpr double kDegToRad = ::csb::kDegToRad;
inline constexpr double kMaxRotate = ::csb::kMaxRotate;
inline constexpr double kEpsilon = ::csb::kEpsilon;
inline constexpr int kTimeoutLimit = ::csb::kTimeoutLimit;
inline constexpr int kMaxThrust = ::csb::kMaxThrust;
inline constexpr int kBoostThrust = ::csb::kBoostThrust;
inline constexpr double kSnapCos = ::csb::kSnapCos;
inline constexpr double kSnapSin = ::csb::kSnapSin;
inline constexpr double kSnapTrigTol = ::csb::kSnapTrigTol;
inline constexpr double kInitAngleSentinel = ::csb::kInitAngleSentinel;
inline constexpr double kInitAngleSentinelTol = ::csb::kInitAngleSentinelTol;

#if defined(__GNUC__) || defined(__clang__)
#define CSB_FP_INLINE inline __attribute__((always_inline))
#else
#define CSB_FP_INLINE inline
#endif

struct Pod {
    double px = 0, py = 0;
    double vx = 0, vy = 0;
    double angle = -1.0;
    int next = 1;
    int shieldtimer = 0;
    int boosted = 0;
    bool won = false;
    bool hasRotated = false;
#if CSB_FP_OPT_TRIG_CACHE
    double cached_cc = 1.0;
    double cached_cs = 0.0;
    bool trig_valid = false;
#endif
};

struct Snapshot {
    Pod pods[kPodCount];
    int playerTimeout[2];
    int turn;
};

struct Move {
    double tx = 0, ty = 0;
    int thrust = 0;       // numeric; ignored if shield/boost
    bool shield = false;
    bool boost = false;
    bool invalid_input = false;
};

struct Game {
    Pod pods[kPodCount];
    double gcx[kMaxGlobalCp];
    double gcy[kMaxGlobalCp];
    int global_n = 0;
    int track_n = 0;
    int laps = 3;
    int playerTimeout[2] = {kTimeoutLimit, kTimeoutLimit};
    int turn = 0;

    CSB_FP_INLINE void clear() {
        std::memset(this, 0, sizeof(*this));
        for (int i = 0; i < kPodCount; ++i) {
            pods[i].angle = -1.0;
            pods[i].next = 1;
        }
        playerTimeout[0] = playerTimeout[1] = kTimeoutLimit;
        laps = 3;
    }

    CSB_FP_INLINE void copyFrom(const Game& o) { std::memcpy(this, &o, sizeof(Game)); }

    CSB_FP_INLINE void saveSnapshot(Snapshot& s) const {
        std::memcpy(s.pods, pods, sizeof(pods));
        s.playerTimeout[0] = playerTimeout[0];
        s.playerTimeout[1] = playerTimeout[1];
        s.turn = turn;
    }

    CSB_FP_INLINE void restoreSnapshot(const Snapshot& s) {
        std::memcpy(pods, s.pods, sizeof(pods));
        playerTimeout[0] = s.playerTimeout[0];
        playerTimeout[1] = s.playerTimeout[1];
        turn = s.turn;
    }

    // Build global CP list from track (x,y interleaved length 2*n).
    void setTrack(const double* xy, int n_cp, int laps_in) {
        if (n_cp < 1) n_cp = 1;
        if (n_cp > kMaxTrackCp) n_cp = kMaxTrackCp;
        if (laps_in < 1) laps_in = 1;
        if (laps_in > kMaxLaps) laps_in = kMaxLaps;
        track_n = n_cp;
        laps = laps_in;
        global_n = 0;
        for (int L = 0; L < laps_in; ++L) {
            for (int i = 0; i < n_cp; ++i) {
                gcx[global_n] = xy[2 * i];
                gcy[global_n] = xy[2 * i + 1];
                ++global_n;
            }
        }
        gcx[global_n] = xy[0];
        gcy[global_n] = xy[1];
        ++global_n;
    }

    void setPod(int i, double x, double y, double vx_, double vy_, double ang_rad,
                int next_global, int shield = 0, int boost_used = 0) {
        Pod& p = pods[i];
        p.px = x;
        p.py = y;
        p.vx = vx_;
        p.vy = vy_;
        p.angle = ang_rad;
        p.next = next_global;
        p.shieldtimer = shield;
        p.boosted = boost_used;
        p.won = false;
        p.hasRotated = !(std::fabs(ang_rad - kInitAngleSentinel) < kInitAngleSentinelTol ||
                         std::fabs(ang_rad) < kInitAngleSentinelTol);
#if CSB_FP_OPT_TRIG_CACHE
        p.trig_valid = false;
#endif
    }

    void setTimeouts(int t0, int t1) {
        playerTimeout[0] = t0;
        playerTimeout[1] = t1;
    }

    CSB_FP_INLINE static double snapNearInteger(double v) {
        return ::csb::snapNearInteger(v);
    }

    CSB_FP_INLINE static double getAngle(double px, double py, double tx, double ty) {
        return ::csb::getAngle(px, py, tx, ty);
    }

#if CSB_FP_OPT_TRIG_CACHE
    CSB_FP_INLINE static void invalidateTrig(Pod& p) { p.trig_valid = false; }
#else
    CSB_FP_INLINE static void invalidateTrig(Pod&) {}
#endif

    CSB_FP_INLINE static void evalSinCos(double angle, double* cs, double* cc) {
#if CSB_FP_OPT_SINCOS
#if defined(__APPLE__)
        __sincos(angle, cs, cc);
#elif defined(__GLIBC__)
        sincos(angle, cs, cc);
#else
        *cs = std::sin(angle);
        *cc = std::cos(angle);
#endif
#else
        *cs = std::sin(angle);
        *cc = std::cos(angle);
#endif
    }

    // SSOT rotate (fidelity_math.h) — same rules as csb::Game / GATE oracle.
    CSB_FP_INLINE void applyRotate(Pod& p, double tx, double ty) {
        ::csb::applyFidelityRotate(p.angle, p.px, p.py, tx, ty);
        invalidateTrig(p);
    }

    CSB_FP_INLINE void applyRotateByClampedDelta(Pod& p, double da) {
        if (da <= -kMaxRotate) p.angle -= kMaxRotate;
        else if (da >= kMaxRotate) p.angle += kMaxRotate;
        else p.angle += da;
        ::csb::canonicalizeAngleRad(p.angle);
        p.hasRotated = true;
        invalidateTrig(p);
    }

    CSB_FP_INLINE void applyAbsoluteAngle(Pod& p, double angle_rad) {
        p.angle = angle_rad;
        ::csb::canonicalizeAngleRad(p.angle);
        p.hasRotated = true;
        invalidateTrig(p);
    }

    // SSOT thrust + ULP lattice (fidelity_math.h). Trig cache intentionally
    // bypassed: lattice depends on exact thrustCosSin + snap/nextafter predicates.
    CSB_FP_INLINE void applyThrust(Pod& p, int t) {
        ::csb::applyFidelityThrust(p.vx, p.vy, p.angle, t);
        invalidateTrig(p);
    }

    // SSOT applyFidelityMove (fidelity_math.h) — same as csb::Pod::applyMove.
    CSB_FP_INLINE void applyMove(Pod& p, const Move& m) {
        ::csb::applyFidelityMove(p.px, p.py, p.vx, p.vy, p.angle, p.shieldtimer,
                                 p.boosted, p.hasRotated, m.tx, m.ty, m.thrust,
                                 m.shield, m.boost, m.invalid_input);
        invalidateTrig(p);
    }

    CSB_FP_INLINE void applyGAActionDegrees(Pod& p, double angle_shift_deg, int thrust_val) {
        if (thrust_val == -1) { p.shieldtimer = 4; thrust_val = 0; }
        else if (p.shieldtimer > 0) thrust_val = 0;
        if (thrust_val == 650) p.boosted = 1;
        if (p.angle < 0) { p.angle = 0.0; p.hasRotated = true; invalidateTrig(p); }
        else {
            double shift = angle_shift_deg * kDegToRad;
            if (shift > kMaxRotate) shift = kMaxRotate;
            if (shift < -kMaxRotate) shift = -kMaxRotate;
            p.angle += shift;
            while (p.angle >= kFullCircle) p.angle -= kFullCircle;
            while (p.angle < 0.0) p.angle += kFullCircle;
            p.hasRotated = true;
            invalidateTrig(p);
        }
        applyThrust(p, thrust_val);
    }

    // World collision/CP/commit: only via simulateFidelityWorld (no local bounce loop).

    // Hot path: apply 4 moves then world step (Fidelity-equivalent).
    void step(const Move moves[kPodCount]) {
        applyMove(pods[0], moves[0]);
        applyMove(pods[1], moves[1]);
        applyMove(pods[2], moves[2]);
        applyMove(pods[3], moves[3]);
        simulateWorld();
    }

    // World step — single SSOT: csb::simulateFidelityWorld (same as referee Game).
    void simulateWorld() {
        ::csb::WorldPod wp[kPodCount];
        for (int i = 0; i < kPodCount; ++i) {
            wp[i].px = pods[i].px;
            wp[i].py = pods[i].py;
            wp[i].vx = pods[i].vx;
            wp[i].vy = pods[i].vy;
            wp[i].next = pods[i].next;
            wp[i].shieldtimer = pods[i].shieldtimer;
            wp[i].won = pods[i].won;
        }
        ::csb::simulateFidelityWorld(wp, gcx, gcy, global_n, playerTimeout, &turn);
        for (int i = 0; i < kPodCount; ++i) {
            pods[i].px = wp[i].px;
            pods[i].py = wp[i].py;
            pods[i].vx = wp[i].vx;
            pods[i].vy = wp[i].vy;
            pods[i].next = wp[i].next;
            pods[i].shieldtimer = wp[i].shieldtimer;
            pods[i].won = wp[i].won;
        }
    }
};

inline void step_batch(Game* games, const Move* moves_flat, int n) {
    for (int i = 0; i < n; ++i) games[i].step(moves_flat + i * kPodCount);
}

// Exact state equality vs another fast_physics Game (tests).
inline bool statesEqual(const Game& a, const Game& b, double ang_eps = 1e-12) {
    if (a.playerTimeout[0] != b.playerTimeout[0] || a.playerTimeout[1] != b.playerTimeout[1])
        return false;
    for (int i = 0; i < kPodCount; ++i) {
        const Pod& p = a.pods[i];
        const Pod& q = b.pods[i];
        if (p.px != q.px || p.py != q.py) return false;
        if (p.vx != q.vx || p.vy != q.vy) return false;
        if (p.next != q.next || p.shieldtimer != q.shieldtimer || p.boosted != q.boosted) return false;
        if (p.won != q.won || p.hasRotated != q.hasRotated) return false;
        double da = std::fabs(p.angle - q.angle);
        if (da > M_PI) da = kFullCircle - da;
        if (da > ang_eps) return false;
    }
    return true;
}

#undef CSB_FP_INLINE

}  // namespace fast_physics
}  // namespace csb
