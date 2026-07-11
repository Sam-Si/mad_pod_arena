#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLAN="$ROOT/docs/plans/2026-07-11-full-repo-refactoring-plan.md"
fail=0
grep -q 'Status:\*\* Active' "$PLAN" || { echo "FAIL: Status Active missing"; fail=1; }
grep -q 'ga_pure' "$PLAN" || { echo "FAIL: ga_pure not in plan"; fail=1; }
grep -q 'W1\.1' "$PLAN" || { echo "FAIL: W1.1 exit criteria missing"; fail=1; }
grep -q 'fidelity_world_step.h' "$PLAN" || { echo "FAIL: freeze list missing world step"; fail=1; }
grep -q 'Amalgam acceptance' "$PLAN" || { echo "FAIL: amalgam gate section missing"; fail=1; }
grep -q 'Active | Superseded' "$ROOT/docs/plans/README.md" || { echo "FAIL: plans README status convention"; fail=1; }
grep -q 'structure sequencing' "$ROOT/docs/README.md" || grep -qi 'structure waves' "$ROOT/docs/README.md" || { echo "FAIL: docs README structure pointer"; fail=1; }
if [[ "$fail" -ne 0 ]]; then exit 1; fi
echo "hardening checks PASS"
