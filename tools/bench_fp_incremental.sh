#!/usr/bin/env bash
# Incremental EXACT+speed ladder for fast_physics.h opts (g++ direct for -D control).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src/physics"
OUT="$ROOT/logs/fp_incremental_bench"
mkdir -p "$OUT"
CXX="${CXX:-c++}"
COMMON=(-std=c++17 -O3 -DNDEBUG -fno-math-errno -fomit-frame-pointer -ffp-contract=off -I"$SRC")

build() {
  local tag="$1"; shift
  echo "=== BUILD $tag $* ==="
  "$CXX" "${COMMON[@]}" "$@" \
    "$SRC/bench_fast_physics.cpp" -o "$OUT/bench_$tag"
}

run() {
  local tag="$1"
  echo "=== RUN $tag ==="
  "$OUT/bench_$tag" --iters 7 --scenarios 200 --turns 500 --json \
    | tee "$OUT/result_$tag.json" | tail -3
  python3 - <<PY
import json
t=open("$OUT/result_$tag.json").read()
d=json.loads(t[t.find('{'):])
r=d["results"]
def m(k):
    v=r.get(k)
    return v["median_ns_per_turn"] if isinstance(v,dict) else v
print(f"TAG=$tag fid_step={m('fidelity_step'):.1f} fp_step={m('fast_physics_step'):.1f} "
      f"speedup={r.get('step_speedup')} search={m('search_batch'):.1f}")
PY
}

# Opt flags cumulatively enabled
# Cumulative ladder. On Apple, SINCOS stays off unless CSB_FP_FORCE_SINCOS=1.
OPTS=(
  "0_baseline|-DCSB_FP_OPT_ALL=0 -DCSB_FP_OPT_PAIR_REUSE=0"
  "1_trigcache|-DCSB_FP_OPT_ALL=0 -DCSB_FP_OPT_TRIG_CACHE=1 -DCSB_FP_OPT_PAIR_REUSE=0"
  "2_freeflight|-DCSB_FP_OPT_ALL=0 -DCSB_FP_OPT_TRIG_CACHE=1 -DCSB_FP_OPT_FREE_FLIGHT=1 -DCSB_FP_OPT_PAIR_REUSE=0"
  "3_cpbbox|-DCSB_FP_OPT_ALL=0 -DCSB_FP_OPT_TRIG_CACHE=1 -DCSB_FP_OPT_FREE_FLIGHT=1 -DCSB_FP_OPT_CP_BBOX=1 -DCSB_FP_OPT_PAIR_REUSE=0"
  "4_epilogue|-DCSB_FP_OPT_ALL=0 -DCSB_FP_OPT_TRIG_CACHE=1 -DCSB_FP_OPT_FREE_FLIGHT=1 -DCSB_FP_OPT_CP_BBOX=1 -DCSB_FP_OPT_FAST_EPILOGUE=1"
  "5_default|-DCSB_FP_OPT_ALL=1"
)

SUMMARY="$OUT/summary.tsv"
echo -e "tag\tfid_step\tfp_step\tspeedup\tsearch\texit" > "$SUMMARY"
for entry in "${OPTS[@]}"; do
  tag="${entry%%|*}"
  flags="${entry#*|}"
  # shellcheck disable=SC2086
  build "$tag" $flags
  set +e
  run "$tag"
  ec=$?
  set -e
  python3 - <<PY
import json
tag="$tag"
ec=$ec
t=open("$OUT/result_$tag.json").read()
try:
    d=json.loads(t[t.find('{'):])
    r=d["results"]
    def m(k):
        v=r.get(k)
        return v["median_ns_per_turn"] if isinstance(v,dict) else float("nan")
    line=f"{tag}\t{m('fidelity_step'):.2f}\t{m('fast_physics_step'):.2f}\t{r.get('step_speedup')}\t{m('search_batch'):.2f}\t{ec}\n"
except Exception as e:
    line=f"{tag}\tFAIL\tFAIL\tFAIL\tFAIL\t{ec}\n"
open("$SUMMARY","a").write(line)
print(line)
PY
  if [[ $ec -ne 0 ]]; then
    echo "FAIL at $tag — stopping ladder" >&2
    break
  fi
done
echo "Summary: $SUMMARY"
column -t -s $'\t' "$SUMMARY" || cat "$SUMMARY"
