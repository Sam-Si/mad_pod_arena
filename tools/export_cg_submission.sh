#!/usr/bin/env bash
# CodinGame submission export — separate from physics merge gate.
# Rebuilds amalgam from shared sources (cg_bot.cpp + fast.h) and copies paste body.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT_DIR="${1:-$ROOT/dist}"
mkdir -p "$OUT_DIR"

echo "Building //src/cg:cg_bot_amalgam (+ compile smoke)..."
bazel build //src/cg:cg_bot_amalgam //src/cg:cg_bot_amalgam_bin
bazel test //src/cg:amalgam_fast_smoke_test --test_output=errors

# Locate generated amalgam (Bazel output path varies by config)
CANDIDATES=(
  "$ROOT/bazel-bin/src/cg/cg_bot_amalgam.cpp"
  "$ROOT/bazel-out/darwin_arm64-fastbuild/bin/src/cg/cg_bot_amalgam.cpp"
  "$ROOT/bazel-out/darwin_arm64-opt/bin/src/cg/cg_bot_amalgam.cpp"
  "$ROOT/bazel-out/k8-fastbuild/bin/src/cg/cg_bot_amalgam.cpp"
  "$ROOT/bazel-out/k8-opt/bin/src/cg/cg_bot_amalgam.cpp"
)
SRC=""
for c in "${CANDIDATES[@]}"; do
  if [[ -f "$c" ]]; then SRC="$c"; break; fi
done
if [[ -z "$SRC" ]]; then
  SRC="$(find "$ROOT/bazel-bin" "$ROOT/bazel-out" -name 'cg_bot_amalgam.cpp' 2>/dev/null | head -1 || true)"
fi
if [[ -z "$SRC" || ! -f "$SRC" ]]; then
  echo "ERROR: cg_bot_amalgam.cpp not found after build" >&2
  exit 1
fi

DEST="$OUT_DIR/cg_submission.cpp"
cp -f "$SRC" "$DEST"
BYTES=$(wc -c < "$DEST" | tr -d ' ')
if [[ "$BYTES" -lt 1000 ]]; then
  echo "ERROR: amalgam too small ($BYTES bytes)" >&2
  exit 1
fi
grep -q 'GENERATED' "$DEST" || { echo "ERROR: missing GENERATED marker" >&2; exit 1; }
grep -q 'CG_STANDALONE' "$DEST" || { echo "ERROR: missing CG_STANDALONE" >&2; exit 1; }
grep -q 'SimulateTurn' "$DEST" || { echo "ERROR: missing SimulateTurn from fast fragment" >&2; exit 1; }

echo "OK: CG submission ready"
echo "  source: $SRC"
echo "  paste:  $DEST"
echo "  bytes:  $BYTES"
