#!/usr/bin/env bash
# =============================================================================
# setup.sh — Mad Pod Arena macOS build environment setup + launch
#
# Usage:
#   ./setup.sh                 # setup + build + run tournament benchmark
#   ./setup.sh --test          # setup + build + run differential tests
#   ./setup.sh --build-only    # setup + build (no tournament, no tests)
#
# Environment overrides:
#   REPO_DIR          — path to mad_pod_arena repo  (default: auto-detected)
#   BAZEL_DISK_CACHE  — bazel action cache dir      (default: .bazel/disk_cache)
#   BAZEL_REPO_CACHE  — bazel repository cache dir  (default: .bazel/repo_cache)
#   BAZEL_JOBS        — parallel build jobs         (default: auto-computed)
#   BAZEL_MEM_MB      — JVM max memory limit in MB  (default: auto-computed)
# =============================================================================

set -euo pipefail

# ── Colour helpers ────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ OK ]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
die()   { echo -e "${RED}[FAIL]${NC}  $*" >&2; exit 1; }
step()  { echo -e "\n${BOLD}${CYAN}━━━  $*  ━━━${NC}"; }
timer() { echo -e "${YELLOW}  ⏱  Elapsed: $(( $(date +%s) - SCRIPT_START ))s${NC}"; }

SCRIPT_START=$(date +%s)

# ── Parse flags ───────────────────────────────────────────────────────────────
MODE="tournament"          # tournament | test | build-only
for arg in "$@"; do
  case "$arg" in
    --test)       MODE="test" ;;
    --build-only) MODE="build-only" ;;
    --help|-h)
      sed -n '3,15p' "$0" | sed 's/^# \?//'
      exit 0 ;;
    *) die "Unknown argument: $arg" ;;
  esac
done

# Auto-detect REPO_DIR (directory of this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${REPO_DIR:-$SCRIPT_DIR}"

BAZEL_DISK_CACHE="${BAZEL_DISK_CACHE:-${REPO_DIR}/.bazel/disk_cache}"
BAZEL_REPO_CACHE="${BAZEL_REPO_CACHE:-${REPO_DIR}/.bazel/repo_cache}"

# ── Detect macOS hardware profile ─────────────────────────────────────────────
OS_NAME=$(uname -s)
[[ "$OS_NAME" == "Darwin" ]] || die "This setup script is optimized for macOS (Darwin). Current OS: ${OS_NAME}"

ARCH=$(uname -m)
info "macOS Architecture: ${ARCH}"

# Compute CPU core count & optimal build jobs
NCPUS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
BAZEL_JOBS="${BAZEL_JOBS:-$(( NCPUS * 3 / 2 ))}" # 1.5x core count

# Compute memory sizing dynamically
PHYS_MEM_BYTES=$(sysctl -n hw.memsize 2>/dev/null || echo 8589934592)
PHYS_MEM_GB=$(( PHYS_MEM_BYTES / 1024 / 1024 / 1024 ))
# Use up to 50% of memory for JVM heap, capped at 8GB, never less than 2GB
BAZEL_MEM_MB="${BAZEL_MEM_MB:-$(( PHYS_MEM_GB * 1024 / 2 ))}"
if [[ $BAZEL_MEM_MB -gt 8192 ]]; then
  BAZEL_MEM_MB=8192
elif [[ $BAZEL_MEM_MB -lt 2048 ]]; then
  BAZEL_MEM_MB=2048
fi

echo -e "${BOLD}${CYAN}"
echo "   __  ___         __  ___         __ ___                  "
echo "  /  |/  /__ _____/  |/  /__  ____/ // _ |_______ ___  ___ "
echo " / /|_/ / _ \`/ _  / /|_/ / _ \`/ _  / // __ / __/ -_) _ \\/ _ \\"
echo "/_/  /_/\\_,_/\\_,_/_/  /_/\\_,_/\\_,_/_//_/ |_/_/  \\__/_//_/_//_/"
echo -e "${NC}"
info "Workspace:   ${REPO_DIR}"
info "Mode:        ${MODE}"
info "CPUs / Jobs: ${NCPUS} / ${BAZEL_JOBS}"
info "Bazel Max Memory:   ${BAZEL_MEM_MB} MB JVM heap"
info "Disk Cache:  ${BAZEL_DISK_CACHE}"
info "Repo Cache:  ${BAZEL_REPO_CACHE}"

# ── Helper commands ───────────────────────────────────────────────────────────
cmd_exists() { command -v "$1" &>/dev/null; }

purge_sandbox_dirs() {
  local sandbox_dir="${REPO_DIR}/.bazel/output_base/sandbox"
  if [[ -d "$sandbox_dir" ]]; then
    rm -rf "$sandbox_dir"
  fi
}

# ─────────────────────────────────────────────────────────────────────────────
# STEP 1 — Pre-flight Checks & Dependency Setup (No sudo required)
# ─────────────────────────────────────────────────────────────────────────────
step "1/5  Pre-flight Checks & Dependency Verification"

# Verify Xcode Command Line Tools / Clang
if cmd_exists clang; then
  CLANG_VER=$(clang --version | head -n1)
  ok "Toolchain: ${CLANG_VER}"
else
  warn "Xcode Command Line Tools / Clang not found."
  info "Triggering installation dialog..."
  xcode-select --install || true
  die "Please follow the on-screen Xcode installer, then run this script again once complete."
fi

# Verify Homebrew is present
if cmd_exists brew; then
  ok "Homebrew version: $(brew --version | head -n1)"
else
  die "Homebrew is not installed. Please install it from https://brew.sh (no sudo required) to run this script."
fi

# Install dependencies using Homebrew
info "Verifying packages (go, bazelisk, python3)..."
BREW_DEPS=()
cmd_exists go || BREW_DEPS+=("go")
cmd_exists bazel || BREW_DEPS+=("bazelisk")
cmd_exists python3 || BREW_DEPS+=("python3")

if [[ ${#BREW_DEPS[@]} -gt 0 ]]; then
  info "Installing missing dependencies via Homebrew (no sudo): ${BREW_DEPS[*]}..."
  brew install "${BREW_DEPS[@]}"
  ok "Homebrew dependencies successfully installed!"
else
  ok "All dependencies (Go, Bazelisk, Python 3) are already available in your PATH."
fi

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 2 — Cache & Sandbox Setup
# ─────────────────────────────────────────────────────────────────────────────
step "2/5  Cache & Sandbox Setup"

mkdir -p "${BAZEL_DISK_CACHE}" "${BAZEL_REPO_CACHE}"
info "Disk Cache size: $(du -sh "${BAZEL_DISK_CACHE}" 2>/dev/null | cut -f1 || echo "0B")"
info "Repo Cache size: $(du -sh "${BAZEL_REPO_CACHE}" 2>/dev/null | cut -f1 || echo "0B")"

purge_sandbox_dirs
ok "Sandbox directories cleaned"

# ─────────────────────────────────────────────────────────────────────────────
# STEP 3 — Build Mad Pod Arena Targets
# ─────────────────────────────────────────────────────────────────────────────
step "3/5  Bazel Build (${BAZEL_JOBS} jobs)"
cd "${REPO_DIR}"

BAZEL_JVM_FLAGS=(
  "--host_jvm_args=-Xmx${BAZEL_MEM_MB}m"
  "--host_jvm_args=-XX:+UseG1GC"
  "--host_jvm_args=-XX:MaxGCPauseMillis=50"
)

BUILD_TARGETS=(
  "//src/cg:cg_bot"
  "//src/cg:cg_bot_standalone"
  "//src/tournament:benchmark_tournament"
  "//src/engine:test_physics"
)

info "Building targets: ${BUILD_TARGETS[*]}"
BUILD_START=$(date +%s)

bazel "${BAZEL_JVM_FLAGS[@]}" build \
  --jobs="${BAZEL_JOBS}" \
  --local_resources="cpu=${NCPUS}" \
  --local_resources="memory=${BAZEL_MEM_MB}" \
  "${BUILD_TARGETS[@]}"

BUILD_END=$(date +%s)
ok "Build completed successfully in $(( BUILD_END - BUILD_START ))s!"
timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 4 — Run Tests (optional)
# ─────────────────────────────────────────────────────────────────────────────
if [[ "$MODE" == "test" ]]; then
  step "4/5  Running Differential Physics Tests"
  info "Running C++ vs Go Referee differential physics testing..."
  
  set +e
  python3 src/engine/diff_test.py
  TEST_STATUS=$?
  set -e
  
  if [[ $TEST_STATUS -ne 0 ]]; then
    die "Differential tests failed. See output logs above for details."
  else
    ok "All differential tests completed successfully!"
  fi
  timer
fi

# ─────────────────────────────────────────────────────────────────────────────
# STEP 5 — Run Tournament Benchmark / Executing
# ─────────────────────────────────────────────────────────────────────────────
if [[ "$MODE" == "tournament" ]]; then
  step "5/5  Running Benchmark Tournament"
  info "Launching benchmark tournament (CGBot self-play)..."
  echo ""
  
  # Run the tournament benchmark directly through Bazel
  exec bazel "${BAZEL_JVM_FLAGS[@]}" run \
    --jobs="${BAZEL_JOBS}" \
    --local_resources="cpu=${NCPUS}" \
    --local_resources="memory=${BAZEL_MEM_MB}" \
    //src/tournament:benchmark_tournament
fi

if [[ "$MODE" == "build-only" ]]; then
  step "5/5  Execution complete (Build Only)"
  ok "Build only completed successfully. No tests or tournament executed."
fi

echo -e "\n${GREEN}${BOLD}Done.${NC} Total execution time: $(( $(date +%s) - SCRIPT_START ))s"
