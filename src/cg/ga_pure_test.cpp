// Characterization / unit tests for ga_pure — drives shipped helpers only.
// Feathers: sub-second pure suite for bot island (no battle JSON, no FS physics).
#include "ga_pure.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static int g_fails = 0;

static void expect_near(const char* name, double got, double want, double tol) {
    if (std::fabs(got - want) > tol) {
        std::cerr << "FAIL " << name << ": got=" << got << " want=" << want << "\n";
        ++g_fails;
    } else {
        std::cout << "PASS " << name << "\n";
    }
}

static void expect_true(const char* name, bool cond) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_fails;
    } else {
        std::cout << "PASS " << name << "\n";
    }
}

int main() {
    using namespace ga_pure;

    // --- ClampAngleShiftDeg (historical MakeGoToTarget / proxy clamp) ---
    expect_near("clamp_over", ClampAngleShiftDeg(30.0), 18.0, 1e-12);
    expect_near("clamp_under", ClampAngleShiftDeg(-25.0), -18.0, 1e-12);
    expect_near("clamp_mid", ClampAngleShiftDeg(5.0), 5.0, 1e-12);
    expect_near("clamp_edge_pos", ClampAngleShiftDeg(18.0), 18.0, 1e-12);
    expect_near("clamp_edge_neg", ClampAngleShiftDeg(-18.0), -18.0, 1e-12);

    // --- AngleShiftTowardTarget: fixed pose, target due east from origin ---
    // pod at (0,0) angle 0 looking east, target (1000,0) => shift 0
    expect_near("shift_already_aligned",
                AngleShiftTowardTarget(0.0, 0.0, 0.0, 1000.0, 0.0), 0.0, 1e-9);
    // pod angle 0, target north (0,1000) => want +90 but clamp to +18
    expect_near("shift_north_clamped",
                AngleShiftTowardTarget(0.0, 0.0, 0.0, 0.0, 1000.0), 18.0, 1e-9);
    // pod angle 0, target south (0,-1000) => want -90 clamp to -18
    expect_near("shift_south_clamped",
                AngleShiftTowardTarget(0.0, 0.0, 0.0, 0.0, -1000.0), -18.0, 1e-9);
    // small correction within band: angle 0, target slightly north-east
    {
        const double shift = AngleShiftTowardTarget(0.0, 0.0, 0.0, 1000.0, 100.0);
        expect_true("shift_small_positive", shift > 0.0 && shift <= 18.0);
        // Unclamped would be atan2(100,1000) in deg ~ 5.71
        expect_near("shift_small_value", shift, RadToDeg(std::atan2(100.0, 1000.0)), 1e-9);
    }

    // --- BoundsPenalty (historical check_bounds) ---
    expect_near("bounds_ok_center", BoundsPenalty(8000.0, 4500.0), 0.0, 0.0);
    expect_near("bounds_low_x", BoundsPenalty(-1001.0, 4500.0), kBoundsPenalty, 0.0);
    expect_near("bounds_high_y", BoundsPenalty(8000.0, 10001.0), kBoundsPenalty, 0.0);
    expect_near("bounds_edge_ok", BoundsPenalty(-1000.0, -1000.0), 0.0, 0.0);

    // --- CpCrossedBonus ---
    expect_near("cp_zero", CpCrossedBonus(0), 0.0, 0.0);
    expect_near("cp_neg", CpCrossedBonus(-1), 0.0, 0.0);
    expect_near("cp_two", CpCrossedBonus(2), 30000.0, 0.0);

    // --- Dist / align / lateral / speed / angle terms ---
    expect_near("dist_term", DistWeightTerm(1000.0, 1.5), 1500.0, 1e-12);
    expect_near("align_full", AlignTerm(100.0, 0.0, 1.0, 0.0, 3.0), 300.0, 1e-12);
    expect_near("lateral_full", LateralTerm(0.0, 50.0, 1.0, 0.0, 0.5), 25.0, 1e-12);
    expect_near("speed_term", SpeedTerm(3.0, 4.0, 0.5), 2.5, 1e-12);
    expect_near("angle_err_term", AngleErrorTerm(10.0, 25.0), 250.0, 1e-12);

    // --- RunnerKinematicsScore: pure composition used by production evaluate_state ---
    // At entry point, d==0 branch: only speed term
    {
        const double s = RunnerKinematicsScore(
            100.0, 200.0, 30.0, 40.0, 0.0,
            100.0, 200.0,
            3.0, 0.5, 25.0, 0.5);
        // speed = 50, * 0.5 = 25
        expect_near("kin_at_ep_speed_only", s, 25.0, 1e-9);
    }
    // Far east of EP with vel fully aligned east
    {
        const double s = RunnerKinematicsScore(
            0.0, 0.0, 100.0, 0.0, 0.0,
            1000.0, 0.0,
            1.5, 0.5, 25.0, 0.5);
        // align 100*1.5=150, lateral 0, angle_err 0, speed 100*0.5=50 => 200
        expect_near("kin_aligned_east", s, 200.0, 1e-9);
    }

    // --- ShortestAngleDiffDeg wrap characterization ---
    expect_near("diff_wrap", ShortestAngleDiffDeg(350.0, 10.0), 20.0, 1e-12);
    expect_near("diff_neg_wrap", ShortestAngleDiffDeg(10.0, 350.0), -20.0, 1e-12);
    expect_near("norm_neg", NormalizeAngleDeg(-10.0), 350.0, 1e-12);

    // --- Free-flight friction (bot heuristics; not Fidelity world-step) ---
    expect_near("ff_friction_200", FreeFlightFrictionVel(200.0), 170.0, 0.0);  // trunc(200*0.85)
    expect_near("ff_friction_neg", FreeFlightFrictionVel(-200.0), -170.0, 0.0);
    expect_near("ff_friction_custom", FreeFlightFrictionVel(100.0, 0.5), 50.0, 0.0);

    // --- Fast activation penalty term ---
    expect_near("fast_act_2", FastActPenalty(2.0), 2000.0, 0.0);
    expect_near("fast_act_0", FastActPenalty(0.0), 0.0, 0.0);

    if (g_fails) {
        std::cerr << "ga_pure_test: " << g_fails << " failure(s)\n";
        return 1;
    }
    std::cout << "ga_pure_test: all ok\n";
    return 0;
}
