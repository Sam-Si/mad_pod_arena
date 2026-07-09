#!/usr/bin/env bash
# =============================================================================
# TRUTH SUITE — sole behavioral source of truth for mad_pod_arena
# =============================================================================
# Fowler (Refactoring, 2nd ed. 2018): refactoring preserves *observable
# behavior*. Observable behavior here is defined ONLY by the tests/checks
# in this script—not by docs, comments, or untested code paths.
#
# Exit 0  => all contracts hold; safe to refactor structure.
# Exit !=0 => behavior or ownership contract broken; do not ship.
#
# Usage (repo root):
#   ./tools/run_truth_suite.sh
#   ./tools/run_truth_suite.sh --quick    # skip full gate A (faster local loop)
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
QUICK=0
[[ "${1:-}" == "--quick" ]] && QUICK=1

pass() { echo "  PASS  $*"; }
fail() { echo "  FAIL  $*" >&2; exit 1; }
section() { echo ""; echo "=== $* ==="; }

section "1. Structural / ownership guards (design rules as tests)"
python3 sim/check_ssot_policy.py || fail "check_ssot_policy"
pass "check_ssot_policy"
python3 sim/check_verification_policy.py || fail "check_verification_policy"
pass "check_verification_policy"

section "2. C++ unit / golden contracts"
bazel test --config=ci //src/physics:test_physics //src/cg:amalgam_fast_smoke_test //src/cg:ga_pure_test //src/engine:arena_fidelity_trace_test \
  --test_output=errors || fail "bazel unit tests"
pass "test_physics + amalgam_smoke + ga_pure + arena_fidelity_trace"

section "3. Exact-rollout == Fidelity (shared world-step must not diverge)"
if [[ ! -x sim/validate_fast_physics_battles ]]; then
  c++ -std=c++17 -O3 -DNDEBUG -ffp-contract=off -fno-math-errno -I src/physics \
    src/physics/validate_fast_physics_battles.cpp -o sim/validate_fast_physics_battles \
    || fail "build validate_fast_physics_battles"
fi
python3 sim/validate_fast_physics_corpus.py --limit 100 --leaderboard-sample 0 \
  || fail "exact-fp corpus"
pass "exact-fp 100 battles"

section "4. CG paste artifact (generated from shared sources only)"
./tools/export_cg_submission.sh "$ROOT/dist" || fail "export_cg_submission"
test -f dist/cg_submission.cpp || fail "missing dist/cg_submission.cpp"
BYTES=$(wc -c < dist/cg_submission.cpp | tr -d ' ')
[[ "$BYTES" -gt 1000 ]] || fail "amalgam too small"
grep -q GENERATED dist/cg_submission.cpp || fail "no GENERATED marker"
grep -q SimulateTurn dist/cg_submission.cpp || fail "no SimulateTurn"
pass "CG amalgam export ($BYTES bytes)"

if [[ "$QUICK" -eq 0 ]]; then
  section "5. Merge-gate physics (observable Fidelity vs CG battles)"
  bazel build --config=ci //src/physics:replay_driver || fail "replay_driver build"
  cp -f bazel-bin/src/physics/replay_driver sim/replay_driver && chmod +x sim/replay_driver
  MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles \
    || fail "gate A test_session"
  pass "gate A 312 battles"
  MAD_POD_GATE_STRICT=1 python3 battles/scripts/verify_golden_corpus.py --tier pass \
    || fail "gate B golden pass"
  pass "gate B pass tier"
else
  section "5. Merge-gate physics SKIPPED (--quick)"
fi

echo ""
echo "=============================================="
echo " TRUTH SUITE GREEN — observable behavior OK"
echo "=============================================="
if [[ "$QUICK" -eq 1 ]]; then
  echo "(quick mode: full gate A/B not run)"
fi
exit 0
