#!/usr/bin/env bash
# =============================================================================
# setup-ubuntu.sh — Mad Pod Arena system-wide Ubuntu build environment setup
#
# Usage:
#   ./setup-ubuntu.sh                 # setup + build + run tournament benchmark
#   ./setup-ubuntu.sh --test          # setup + build + run differential tests
#   ./setup-ubuntu.sh --build-only    # setup + build (no tournament, no tests)
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

# ── Check Root & OS ───────────────────────────────────────────────────────────
[[ $EUID -eq 0 ]] || die "This system-wide setup script must be run as root (or inside a root container)."
[[ "$(uname -s)" == "Linux" ]] || die "This script is designed for Linux (Ubuntu)."

# ── Parse flags ───────────────────────────────────────────────────────────────
MODE="tournament"          # tournament | test | build-only
for arg in "$@"; do
  case "$arg" in
    --test)       MODE="test" ;;
    --build-only) MODE="build-only" ;;
    --help|-h)
      sed -n '3,10p' "$0" | sed 's/^# \?//'
      exit 0 ;;
    *) die "Unknown argument: $arg" ;;
  esac
done

# Auto-detect REPO_DIR (directory of this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${REPO_DIR:-$SCRIPT_DIR}"

BAZEL_DISK_CACHE="${BAZEL_DISK_CACHE:-${REPO_DIR}/.bazel/disk_cache}"
BAZEL_REPO_CACHE="${BAZEL_REPO_CACHE:-${REPO_DIR}/.bazel/repo_cache}"

# Detect system profile
ARCH=$(uname -m)
case "$ARCH" in
  x86_64)  BAZEL_ARCH="amd64" ;;
  aarch64) BAZEL_ARCH="arm64" ;;
  *)       BAZEL_ARCH="amd64" ;;
esac

NCPUS=$(nproc 2>/dev/null || echo 4)
BAZEL_JOBS="${BAZEL_JOBS:-$(( NCPUS * 3 / 2 ))}"

# Cap JVM heap dynamically
PHYS_MEM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
PHYS_MEM_GB=$(( PHYS_MEM_KB / 1024 / 1024 ))
BAZEL_MEM_MB="${BAZEL_MEM_MB:-$(( PHYS_MEM_GB * 1024 / 2 ))}"
if [[ $BAZEL_MEM_MB -gt 8192 ]]; then
  BAZEL_MEM_MB=8192
elif [[ $BAZEL_MEM_MB -lt 2048 ]]; then
  BAZEL_MEM_MB=2048
fi

echo -e "${BOLD}${GREEN}"
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

# Clear command hashing cache to prevent old binary path locks
hash -r

cmd_exists() { command -v "$1" &>/dev/null; }

purge_sandbox_dirs() {
  local sandbox_dir="${REPO_DIR}/.bazel/output_base/sandbox"
  if [[ -d "$sandbox_dir" ]]; then
    rm -rf "$sandbox_dir"
  fi
}

# ─────────────────────────────────────────────────────────────────────────────
# STEP 1 — System dependencies
# ─────────────────────────────────────────────────────────────────────────────
step "1/6  System Package Verification"

info "Updating package lists and installing core dependencies..."
apt-get update -qq
apt-get install -y --no-install-recommends \
  wget curl gnupg ca-certificates git \
  python3 python3-pip python3-venv \
  binutils lsb-release software-properties-common \
  >/dev/null
ok "Core system dependencies verified"

# ── LLVM (clang + lld) ───────────────────────────────────────────────────────
if cmd_exists "clang-18" && cmd_exists "lld-18"; then
  ok "LLVM 18 already installed — skipping apt installation"
else
  info "Setting up official LLVM 18 repository..."
  curl -fsSL https://apt.llvm.org/llvm.sh | bash -s -- 18 >/dev/null
  ok "LLVM 18 package installation complete"
fi

# Ensure global symlinks exist
info "Configuring global compiler symlinks..."
ln -sf /usr/bin/clang-18 /usr/bin/clang
ln -sf /usr/bin/clang++-18 /usr/bin/clang++
ln -sf /usr/bin/lld-18 /usr/bin/lld
ln -sf /usr/bin/lld-18 /usr/bin/ld.lld
ok "Global symlinks configured: clang -> clang-18, lld -> lld-18"

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 2 — Go & Bazelisk & grpcurl Setup
# ─────────────────────────────────────────────────────────────────────────────
step "2/6  Development Tools Installation (Go, Bazel, grpcurl)"

# ── Bazelisk ─────────────────────────────────────────────────────────────────
if cmd_exists bazel; then
  ok "Bazel/Bazelisk already installed: $(bazel version --gnu_format 2>/dev/null | grep -oE 'Build label: .*' || echo 'bazelisk')"
else
  info "Downloading Bazelisk globally..."
  curl -fsSL "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BAZEL_ARCH}" -o /usr/local/bin/bazel
  chmod +x /usr/local/bin/bazel
  ok "Bazelisk registered at /usr/local/bin/bazel"
fi

# ── Go ───────────────────────────────────────────────────────────────────────
if cmd_exists go && go version | grep -q '1.22'; then
  ok "Go 1.22.x already installed system-wide"
else
  rm -rf /usr/local/go
  GO_VERSION="1.22.5"
  if [ "$BAZEL_ARCH" = "amd64" ]; then
    GO_URL="https://golang.org/dl/go${GO_VERSION}.linux-amd64.tar.gz"
  else
    GO_URL="https://golang.org/dl/go${GO_VERSION}.linux-arm64.tar.gz"
  fi
  info "Downloading Go ${GO_VERSION}..."
  curl -fsSL "$GO_URL" | tar -xz -C /usr/local
  
  if ! grep -q '/usr/local/go/bin' /etc/profile; then
    echo 'export PATH="/usr/local/go/bin:$PATH"' >> /etc/profile
  fi
  export PATH="/usr/local/go/bin:$PATH"
  ok "Go ${GO_VERSION} registered globally at /usr/local/go"
fi

# ── grpcurl ──────────────────────────────────────────────────────────────────
if cmd_exists grpcurl; then
  ok "grpcurl already installed system-wide"
else
  info "Downloading grpcurl globally..."
  GRPCURL_ARCH="x86_64"
  [[ "$BAZEL_ARCH" == "arm64" ]] && GRPCURL_ARCH="arm64"
  curl -fsSL "https://github.com/fullstorydev/grpcurl/releases/download/v1.9.3/grpcurl_1.9.3_linux_${GRPCURL_ARCH}.tar.gz" | tar -xz -C /usr/local/bin grpcurl
  chmod +x /usr/local/bin/grpcurl
  ok "grpcurl registered at /usr/local/bin/grpcurl"
fi

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 3 — Python + gRPC bindings
# ─────────────────────────────────────────────────────────────────────────────
step "3/6  Python gRPC Tooling Setup"

info "Installing Python grpcio-tools system-wide..."
pip3 install -q --break-system-packages grpcio-tools
ok "Python gRPC tools installed"

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 4 — Cache Setup
# ─────────────────────────────────────────────────────────────────────────────
step "4/6  Incremental Cache Configuration"

mkdir -p "${BAZEL_DISK_CACHE}" "${BAZEL_REPO_CACHE}"
info "Disk Cache size: $(du -sh "${BAZEL_DISK_CACHE}" 2>/dev/null | cut -f1 || echo "0B")"
info "Repo Cache size: $(du -sh "${BAZEL_REPO_CACHE}" 2>/dev/null | cut -f1 || echo "0B")"

purge_sandbox_dirs
ok "Sandbox directories cleaned"

# ─────────────────────────────────────────────────────────────────────────────
# STEP 5 — Bazel Build
# ─────────────────────────────────────────────────────────────────────────────
step "5/6  Bazel Build (${BAZEL_JOBS} jobs)"
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
# STEP 6 — Run Mode
# ─────────────────────────────────────────────────────────────────────────────
if [[ "$MODE" == "test" ]]; then
  step "6/6  Running Differential Physics Tests"
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

if [[ "$MODE" == "tournament" ]]; then
  step "6/6  Running Benchmark Tournament"
  info "Launching benchmark tournament (CGBot self-play)..."
  echo ""
  
  exec bazel "${BAZEL_JVM_FLAGS[@]}" run \
    --jobs="${BAZEL_JOBS}" \
    --local_resources="cpu=${NCPUS}" \
    --local_resources="memory=${BAZEL_MEM_MB}" \
    //src/tournament:benchmark_tournament
fi

if [[ "$MODE" == "build-only" ]]; then
  step "6/6  Execution complete (Build Only)"
  ok "Build only completed successfully. No tests or tournament executed."
fi

echo -e "\n${GREEN}${BOLD}Done.${NC} Total execution time: $(( $(date +%s) - SCRIPT_START ))s"
