"""
Python wrapper that drives the C++ physics/replay_driver via stdin/stdout text protocol.

Designed to be used from any working directory. Resolves the driver binary
relative to this file's location (always at <project>/sim/physics_driver.py).
"""

from __future__ import annotations
import subprocess
import os
import sys
from typing import List, Dict, Tuple, Optional

# Resolve project root once at import time
_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.normpath(os.path.join(_THIS_DIR, ".."))
_DRIVER_SRC = os.path.join(_PROJECT_ROOT, "src", "physics", "replay_driver.cpp")
# Build output goes under sim/ (local, gitignored via *.out / binaries elsewhere)
_DEFAULT_DRIVER = os.path.join(_THIS_DIR, "replay_driver")
# physics.h must be on the include path when compiling the driver
_PHYSICS_HDR_DIR = os.path.join(_PROJECT_ROOT, "src", "physics")


_driver_logged = False


def ensure_driver_built(driver_path: str = _DEFAULT_DRIVER) -> str:
    """Resolve replay_driver per docs/VERIFICATION_TRUTH_POLICY.md. Returns abs path.

    Priority:
      1. MAD_POD_REPLAY_DRIVER env (cwd-relative if not absolute) — wins over driver_path
      2. If candidate path exists and is executable — use it (no mtime rebuild)
      3. Else if MAD_POD_GATE_STRICT!=1 — ad-hoc g++ with WARN
      4. Else fail closed
    """
    global _driver_logged
    strict = os.environ.get("MAD_POD_GATE_STRICT", "") == "1"
    env = os.environ.get("MAD_POD_REPLAY_DRIVER")

    if env:
        path = os.path.abspath(env)
        if not (os.path.isfile(path) and os.access(path, os.X_OK)):
            raise RuntimeError(
                f"MAD_POD_REPLAY_DRIVER={env!r} -> {path!r} missing or not executable"
            )
        return path

    candidate = os.path.abspath(driver_path)
    if os.path.isfile(candidate):
        if not os.access(candidate, os.X_OK):
            raise RuntimeError(
                f"Driver exists but is not executable: {candidate}"
            )
        if not _driver_logged:
            print(f"driver={candidate}", file=sys.stderr)
            _driver_logged = True
        return candidate

    if strict:
        raise RuntimeError(
            f"No replay_driver at {candidate} and MAD_POD_GATE_STRICT=1 "
            "(fail-closed; copy Bazel binary first)"
        )

    ret = subprocess.run(
        [
            "g++", "-std=c++17", "-O2",
            f"-I{_PHYSICS_HDR_DIR}",
            "-o", candidate,
            _DRIVER_SRC,
        ],
        capture_output=True, text=True,
    )
    if ret.returncode != 0:
        raise RuntimeError(f"Failed to build driver:\n{ret.stderr}")
    print(
        "WARN: ad-hoc g++ driver; CI uses Bazel //src/physics:replay_driver",
        file=sys.stderr,
    )
    return candidate


class CppPhysics:
    def __init__(self, driver_path: str = _DEFAULT_DRIVER):
        self.driver_path = ensure_driver_built(driver_path)
        self.proc: Optional[subprocess.Popen] = None
        self._start()

    def _start(self):
        self.proc = subprocess.Popen(
            [self.driver_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

    def _readline(self) -> str:
        """Read one line, raise if driver died."""
        ln = self.proc.stdout.readline()
        if not ln:
            # Grab stderr for diagnostics
            err = ""
            try:
                err = self.proc.stderr.read(2000)
            except Exception:
                pass
            raise RuntimeError(f"Driver process died. stderr: {err[:500]}")
        return ln.rstrip("\n")

    def _send_cmd(self, line: str):
        """Send a command and consume the 'OK ...' + 'READY' acknowledgement.
        Skips any non-protocol lines (DEBUG, etc.) that the engine may emit."""
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        # Read until we see both OK and READY (skip debug/noise lines)
        got_ok = False
        got_ready = False
        for _ in range(20):  # safety limit
            ln = self._readline()
            if ln.startswith("OK"):
                got_ok = True
            elif ln == "READY":
                got_ready = True
            # else: skip noise (DEBUG_ROTATE, etc.)
            if got_ok and got_ready:
                break

    def init_battle(self, checkpoints: List[Tuple[int, int]], laps: int = 3):
        n = len(checkpoints)
        flat = " ".join(f"{x} {y}" for x, y in checkpoints)
        self._send_cmd(f"INIT {n} {flat} {laps}")

    def set_pod(self, idx: int, x: float, y: float, vx: float, vy: float,
                angle: float, next_cp: int, shield: int, boosted: int):
        self._send_cmd(f"SET_POD {idx} {x} {y} {vx} {vy} {angle} {next_cp} {shield} {boosted}")

    def set_timeouts(self, t0: int, t1: int):
        self._send_cmd(f"SET_TIMEOUTS {t0} {t1}")

    def apply(self, pod: int, tx: int, ty: int, thrust: str):
        self._send_cmd(f"ACTION {pod} {tx} {ty} {thrust}")

    def step(self) -> Dict:
        """Execute one turn. Returns {'pods': [4 dicts], 'timeouts': (t0,t1)}."""
        self.proc.stdin.write("STEP\n")
        self.proc.stdin.flush()

        pods = []
        timeouts = (0, 0)
        # Read until STEP_DONE, skipping any noise lines
        for _ in range(20):  # safety limit
            ln = self._readline()
            if ln == "STEP_DONE":
                break
            elif ln.startswith("TIMEOUTS"):
                parts = ln.split()
                timeouts = (int(parts[1]), int(parts[2]))
            elif ln and (ln[0].isdigit() or ln[0] == '-'):
                parts = ln.split()
                if len(parts) >= 9:
                    pods.append({
                        "idx": int(parts[0]),
                        "x": float(parts[1]),
                        "y": float(parts[2]),
                        "vx": int(float(parts[3])),
                        "vy": int(float(parts[4])),
                        "angle": float(parts[5]),
                        "next": int(parts[6]),
                        "shield": int(parts[7]),
                        "boosted": int(parts[8]),
                    })
            # else: skip noise lines (DEBUG, etc.)
        return {"pods": pods, "timeouts": timeouts}

    def close(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.stdin.write("QUIT\n")
                self.proc.stdin.flush()
            except Exception:
                pass
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()
        self.proc = None

    def __del__(self):
        self.close()
