#pragma once
// Shared numeric constants for Mad Pod Arena (SSOT PR-2).
// Fidelity physics may keep aliases in physics.h; do not invent a second set of values.

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace csb_constants {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kPodRadius = 400.0;
inline constexpr double kPodCollisionRsq = 800.0 * 800.0;  // 640000
inline constexpr double kCpRadius = 600.0;
inline constexpr double kCpRsq = 600.0 * 600.0;            // 360000
inline constexpr int kPodCount = 4;
inline constexpr double kMinImpulse = 120.0;
inline constexpr double kFriction = 0.85;
inline constexpr double kMaxRotateDeg = 18.0;
inline constexpr double kMaxRotateRad = 18.0 * (kPi / 180.0);
inline constexpr double kEpsilon = 0.00001;
inline constexpr int kDefaultLaps = 3;
inline constexpr int kTimeoutLimit = 100;
inline constexpr int kMaxThrust = 200;
inline constexpr int kBoostThrust = 650;
inline constexpr int kMaxGameTurns = 500;  // CG / Fidelity; legacy arena used 1000
inline constexpr int kShieldTimerActivate = 4;

// Fidelity shield inverse-mass factor (activation frame)
inline constexpr double kShieldMassFactorFidelity = 0.1;
// Fast / GAPhysicsSimulator Mass() on activation frame
inline constexpr double kShieldMassFast = 10.0;
inline constexpr double kNormalMassFast = 1.0;

// GA Fast GetCollisionTime early-out: c > (2000^2 - r^2) style threshold used in engine
inline constexpr double kGaFarApartC = 3360000.0;

}  // namespace csb_constants
