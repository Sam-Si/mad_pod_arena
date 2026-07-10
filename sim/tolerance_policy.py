"""GATE_* = merge components (A) and (B). EXPLORE_* = compare_battle defaults.
EXACT_* = coordinate-level fidelity (edge-case hunting); not merge gate.

Changing GATE_* requires same-PR updates to: this file, docs/VERIFICATION_TRUTH_POLICY.md,
docs/archive/physics-verification.md, and EXPECTED_GATE in sim/check_verification_policy.py.
"""

GATE_POS_TOL = 5.0
GATE_VEL_TOL = 3.0
GATE_ANG_TOL_DEG = 1.0
GATE_TIMEOUT_TOL = 1

EXPLORE_POS_TOL = 1.0
EXPLORE_VEL_TOL = 1.0
EXPLORE_ANG_TOL_DEG = 1.0
EXPLORE_TIMEOUT_TOL = 1

# Exact mode: pos/vel/timeout/next_cp must match with zero tolerance.
# Angle allows a tiny ε for CG view-string ↔ C++ double (not a physics budget).
EXACT_POS_TOL = 0.0
EXACT_VEL_TOL = 0.0
EXACT_TIMEOUT_TOL = 0
# ~1e-12 rad ≪ any GATE degree; catches real ang bugs while ignoring ulp noise.
EXACT_ANG_EPS_RAD = 1e-12
EXACT_ANG_TOL_DEG = EXACT_ANG_EPS_RAD * (180.0 / 3.141592653589793)  # for angle_close
