#!/usr/bin/env bash
# Multi-package first-party branch coverage (reproducible).
# Instruments SHIPPED sources under src/{physics,core,engine,cg} via suites that
# call the same headers/TUs product code uses. Also runs real Bazel unit targets
# (behavioral proof; not required for llvm-cov percentages).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="${1:-$ROOT/docs/coverage_out}"
mkdir -p "$OUT"
COVFLAGS=(-std=c++17 -O0 -g -fprofile-instr-generate -fcoverage-mapping -DNDEBUG)
INCS=(-I. -Isrc -Isrc/physics -Isrc/cg -Isrc/core -Isrc/engine)

echo "=== 1. Physics/core/cg-pure suite (headers + world step + façades) ==="
clang++ "${COVFLAGS[@]}" "${INCS[@]}" \
  src/coverage/physics_branch_suite.cpp -o "$OUT/physics_branch_suite"
export LLVM_PROFILE_FILE="$OUT/phys.profraw"
"$OUT/physics_branch_suite" | tee "$OUT/physics_suite_run.txt"

echo "=== 2. Engine + Arena suite (engine.cpp + arena.cpp) ==="
clang++ "${COVFLAGS[@]}" "${INCS[@]}" \
  src/coverage/engine_arena_branch_suite.cpp src/engine/engine.cpp src/engine/arena.cpp \
  -o "$OUT/engine_arena_suite"
export LLVM_PROFILE_FILE="$OUT/eng.profraw"
"$OUT/engine_arena_suite" >"$OUT/engine_suite_run.txt" 2>&1
# strip verbose turn spam for log size — already completed
tail -5 "$OUT/engine_suite_run.txt" || true
grep -E 'ok|FAIL|fails' "$OUT/engine_suite_run.txt" || true

echo "=== 3. ga_pure unit test (same as //src/cg:ga_pure_test source) ==="
clang++ "${COVFLAGS[@]}" "${INCS[@]}" \
  src/cg/ga_pure_test.cpp -o "$OUT/ga_pure_test_cov"
export LLVM_PROFILE_FILE="$OUT/gapure.profraw"
"$OUT/ga_pure_test_cov" | tee "$OUT/ga_pure_test_run.txt"

echo "=== 4. llvm-cov reports (per binary — product files only) ==="
xcrun llvm-profdata merge -sparse "$OUT/phys.profraw" -o "$OUT/phys.profdata"
xcrun llvm-cov report "$OUT/physics_branch_suite" -instr-profile="$OUT/phys.profdata" \
  | tee "$OUT/coverage_physics_package.txt"

xcrun llvm-profdata merge -sparse "$OUT/eng.profraw" -o "$OUT/eng.profdata"
xcrun llvm-cov report "$OUT/engine_arena_suite" -instr-profile="$OUT/eng.profdata" \
  | tee "$OUT/coverage_engine_package.txt"

xcrun llvm-profdata merge -sparse "$OUT/gapure.profraw" -o "$OUT/gapure.profdata"
xcrun llvm-cov report "$OUT/ga_pure_test_cov" -instr-profile="$OUT/gapure.profdata" \
  | tee "$OUT/coverage_ga_pure_package.txt"

# Combined human table for docs
{
  echo "# Multi-package coverage snapshot"
  echo
  echo "## Physics / core / cg pure (physics_branch_suite)"
  echo '```'
  cat "$OUT/coverage_physics_package.txt"
  echo '```'
  echo
  echo "## Engine + Arena (engine_arena_suite)"
  echo '```'
  cat "$OUT/coverage_engine_package.txt"
  echo '```'
  echo
  echo "## ga_pure unit test binary"
  echo '```'
  cat "$OUT/coverage_ga_pure_package.txt"
  echo '```'
} | tee "$OUT/coverage_full_combined.md"

# Copy primary artifacts to docs/ for user visibility
cp -f "$OUT/coverage_physics_package.txt" "$ROOT/docs/coverage_phys_latest.txt"
cp -f "$OUT/coverage_engine_package.txt" "$ROOT/docs/coverage_engine_latest.txt"
cp -f "$OUT/coverage_ga_pure_package.txt" "$ROOT/docs/coverage_ga_pure_latest.txt"
cp -f "$OUT/coverage_full_combined.md" "$ROOT/docs/coverage_full_combined.md"

echo "=== 5. Real Bazel unit targets (behavioral — must PASS) ==="
bazel test //src/physics:test_physics //src/cg:ga_pure_test //src/cg:amalgam_fast_smoke_test \
  //src/engine:arena_fidelity_trace_test --test_output=errors \
  | tee "$OUT/bazel_unit_targets.txt"

echo "DONE. Reports under $OUT and docs/coverage_*_latest.txt"
