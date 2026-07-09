#pragma once
// =============================================================================
// Pure bot scoring / action math (Feathers: Break Out pure units for harness).
// No I/O, no RNG, no threading. Numeric contracts match historical GA eval
// terms in ga_prelude_and_search.inc (characterization lock).
// Physics law (friction/collision) lives in core/constants + fidelity_* — not here.
// =============================================================================

#include <cmath>
#include <algorithm>

namespace ga_pure {

// CG max rotate per turn (degrees) — bot action clamp, not Fidelity rad path.
inline constexpr double kMaxRotateDeg = 18.0;
// Historical out-of-bounds penalty used in SimulateAndEvaluate state eval.
inline constexpr double kBoundsPenalty = -100000.0;
// Historical per-CP-crossed bonus in state eval.
inline constexpr double kCpCrossedPoints = 15000.0;
// Arena-ish bounds used by historical check_bounds lambda.
inline constexpr double kBoundMinX = -1000.0;
inline constexpr double kBoundMaxX = 17000.0;
inline constexpr double kBoundMinY = -1000.0;
inline constexpr double kBoundMaxY = 10000.0;

// --- Angle (degrees) — same algorithm as GameEngine for bot product ---

inline double NormalizeAngleDeg(double a) {
    while (a >= 360.0) a -= 360.0;
    while (a < 0.0) a += 360.0;
    return a;
}

inline double ShortestAngleDiffDeg(double current, double target) {
    double diff = target - current;
    while (diff > 180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;
    return diff;
}

inline double RadToDeg(double radians) { return radians * (180.0 / 3.14159265358979323846); }

/** Clamp GA angle shift to [-max_rot, +max_rot] (historical MakeGoToTarget / proxy). */
inline double ClampAngleShiftDeg(double shift, double max_rot = kMaxRotateDeg) {
    return std::max(-max_rot, std::min(max_rot, shift));
}

/**
 * Desired turn toward (tx,ty) from pod pose — pure action primitive used by MakeGoToTarget.
 * Returns clamped angle shift in degrees.
 */
inline double AngleShiftTowardTarget(double pod_angle_deg, double px, double py,
                                     double tx, double ty,
                                     double max_rot = kMaxRotateDeg) {
    const double desired = RadToDeg(std::atan2(ty - py, tx - px));
    const double shift = ShortestAngleDiffDeg(pod_angle_deg, desired);
    return ClampAngleShiftDeg(shift, max_rot);
}

// --- State-eval scoring terms (historical evaluate_state formulas) ---

inline double BoundsPenalty(double x, double y) {
    if (x < kBoundMinX || x > kBoundMaxX || y < kBoundMinY || y > kBoundMaxY) {
        return kBoundsPenalty;
    }
    return 0.0;
}

inline double CpCrossedBonus(int cps_crossed) {
    if (cps_crossed <= 0) return 0.0;
    return static_cast<double>(cps_crossed) * kCpCrossedPoints;
}

/** Term subtracted for remaining race distance (score -= DistWeightTerm(...)). */
inline double DistWeightTerm(double remain, double dist_weight) {
    return remain * dist_weight;
}

/** Velocity alignment reward along unit direction (nx,ny). */
inline double AlignTerm(double vx, double vy, double nx, double ny, double align_weight) {
    return (vx * nx + vy * ny) * align_weight;
}

/** Lateral drift magnitude * weight (score -= LateralTerm(...)). */
inline double LateralTerm(double vx, double vy, double nx, double ny, double lateral_penalty) {
    const double lateral = vx * ny - vy * nx;
    return std::fabs(lateral) * lateral_penalty;
}

/** Absolute angle error * weight (score -= AngleErrorTerm(...)). */
inline double AngleErrorTerm(double abs_angle_err_deg, double angle_penalty) {
    return abs_angle_err_deg * angle_penalty;
}

/** Speed magnitude * weight (score += SpeedTerm(...)). */
inline double SpeedTerm(double vx, double vy, double speed_bonus) {
    return std::sqrt(vx * vx + vy * vy) * speed_bonus;
}

/**
 * Free-flight velocity commit used in GA heuristics (trunc(v * friction)).
 * Default friction matches csb_constants::kFriction / kCgFriction (0.85).
 * Not the Fidelity world-step path — characterization of bot-only free-flight.
 */
inline double FreeFlightFrictionVel(double v, double friction = 0.85) {
    return std::trunc(v * friction);
}

/**
 * Historical runner fast-activation penalty term: score -= FastActPenalty(r_act, weight).
 * Default weight matches RUNNER_FAST_ACT_PENALTY (1000) in the GA module.
 */
inline double FastActPenalty(double activation_turns, double weight = 1000.0) {
    return weight * activation_turns;
}

/**
 * Compose the historical runner kinematics score block for one state
 * (align + lateral + angle_error + speed) given entry-point delta and weights.
 * Used by production evaluate_state and unit characterization.
 */
inline double RunnerKinematicsScore(double px, double py, double vx, double vy, double angle_deg,
                                    double epx, double epy,
                                    double align_weight, double lateral_penalty,
                                    double angle_penalty, double speed_bonus) {
    double score = 0.0;
    const double dx = epx - px;
    const double dy = epy - py;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d > 0.0) {
        const double nx = dx / d;
        const double ny = dy / d;
        score += AlignTerm(vx, vy, nx, ny, align_weight);
        score -= LateralTerm(vx, vy, nx, ny, lateral_penalty);
        const double target_angle = RadToDeg(std::atan2(dy, dx));
        const double angle_err = std::fabs(ShortestAngleDiffDeg(angle_deg, target_angle));
        score -= AngleErrorTerm(angle_err, angle_penalty);
    }
    score += SpeedTerm(vx, vy, speed_bonus);
    return score;
}

}  // namespace ga_pure
