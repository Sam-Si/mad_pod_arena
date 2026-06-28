"""GATE_* = merge components (A) and (B). EXPLORE_* = compare_battle defaults.

Changing GATE_* requires same-PR updates to: this file, docs/VERIFICATION_TRUTH_POLICY.md,
docs/physics-verification.md, and EXPECTED_GATE in sim/check_verification_policy.py.
"""

GATE_POS_TOL = 5.0
GATE_VEL_TOL = 3.0
GATE_ANG_TOL_DEG = 1.0
GATE_TIMEOUT_TOL = 1

EXPLORE_POS_TOL = 1.0
EXPLORE_VEL_TOL = 1.0
EXPLORE_ANG_TOL_DEG = 1.0
EXPLORE_TIMEOUT_TOL = 1
