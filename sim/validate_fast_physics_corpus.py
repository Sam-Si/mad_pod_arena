#!/usr/bin/env python3
"""
EXACT parity: Fidelity (physics.h) vs fast_physics.h on real CG battle corpora.

Does NOT compromise accuracy — every turn must match (pos/vel/next/timeouts;
angle within 1e-12 rad in the C++ checker).

Default corpora (all files unless --sample on large dirs):
  - battles/test_session_battles
  - battles/golden_physics_battles/battles
  - battles/leaderboard_physics_divergences/battles (battle_*.json only)
  - battles/copy_pasted_battles
  - battles/quick_physics_accuracy/battles
  - battles/leaderboard_battles (SAMPLED by default — huge)

Usage (repo root):
  g++ -std=c++17 -O3 -DNDEBUG -I src/physics \\
      src/physics/validate_fast_physics_battles.cpp -o sim/validate_fast_physics_battles
  python3 sim/validate_fast_physics_corpus.py
  python3 sim/validate_fast_physics_corpus.py --leaderboard-sample 500 --seed 42
  python3 sim/validate_fast_physics_corpus.py --all-leaderboard   # slow, full LB root
"""
from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from battle_parser import load_battle  # noqa: E402

VALIDATOR_CANDIDATES = [
    ROOT / "sim" / "validate_fast_physics_battles",
    Path("/tmp/validate_fast_physics_battles"),
]


def find_validator() -> Path:
    for p in VALIDATOR_CANDIDATES:
        if p.is_file() and os.access(p, os.X_OK):
            return p
    # build
    out = ROOT / "sim" / "validate_fast_physics_battles"
    src = ROOT / "src" / "physics" / "validate_fast_physics_battles.cpp"
    cmd = [
        "g++",
        "-std=c++17",
        "-O3",
        "-DNDEBUG",
        "-ffp-contract=off",
        "-fno-math-errno",
        f"-I{ROOT / 'src' / 'physics'}",
        str(src),
        "-o",
        str(out),
    ]
    print("Building", out, "...", flush=True)
    subprocess.check_call(cmd)
    return out


def battle_paths(
    *,
    leaderboard_sample: int,
    all_leaderboard: bool,
    seed: int,
    extra_dirs: list[str],
) -> list[Path]:
    specs: list[tuple[Path, int | None]] = [
        (ROOT / "battles" / "test_session_battles", None),
        (ROOT / "battles" / "golden_physics_battles" / "battles", None),
        (ROOT / "battles" / "leaderboard_physics_divergences" / "battles", None),
        (ROOT / "battles" / "copy_pasted_battles", None),
        (ROOT / "battles" / "quick_physics_accuracy" / "battles", None),
    ]
    for d in extra_dirs:
        specs.append((Path(d), None))

    lb = ROOT / "battles" / "leaderboard_battles"
    if all_leaderboard:
        specs.append((lb, None))
    elif leaderboard_sample > 0 and lb.is_dir():
        specs.append((lb, leaderboard_sample))

    rng = random.Random(seed)
    out: list[Path] = []
    seen: set[str] = set()

    for directory, sample_n in specs:
        if not directory.is_dir():
            print(f"  skip missing {directory}", flush=True)
            continue
        files = sorted(directory.glob("battle_*.json"))
        # divergences folder also has *.divergence.json — glob battle_*.json may include only battles
        files = [f for f in files if f.name.endswith(".json") and ".divergence." not in f.name]
        if sample_n is not None and len(files) > sample_n:
            files = rng.sample(files, sample_n)
            files.sort()
            print(f"  {directory}: sampled {sample_n} / available", flush=True)
        else:
            print(f"  {directory}: {len(files)} battles", flush=True)
        for f in files:
            key = f.name
            if key in seen:
                continue
            seen.add(key)
            out.append(f)
    return out


def thrust_mode(thr) -> tuple[int, int]:
    if thr == "SHIELD":
        return 1, 0
    if thr == "BOOST":
        return 2, 0
    try:
        v = int(thr)
    except (TypeError, ValueError):
        return 3, 999
    if v < 0 or v > 200:
        return 3, v
    return 0, v


def dump_battle(path: Path) -> str | None:
    try:
        b = load_battle(str(path))
    except Exception as e:
        print(f"  WARN parse fail {path.name}: {e}", flush=True)
        return None
    if not b.checkpoints or not b.turns:
        print(f"  WARN empty {path.name} turns={len(b.turns)}", flush=True)
        return None
    if len(b.checkpoints) > 8:
        print(f"  WARN skip {path.name}: n_cp={len(b.checkpoints)} > 8 (fast_physics limit)", flush=True)
        return None
    lines: list[str] = [f"{len(b.checkpoints)} {b.laps}"]
    for x, y in b.checkpoints:
        lines.append(f"{x} {y}")
    for p in b.initial_state.pods:
        ang = p.angle if p.angle is not None else -0.0174533
        lines.append(
            f"{p.x} {p.y} {p.vx} {p.vy} {ang} {p.next_cp} {p.shield_active} {p.boosted}"
        )
    lines.append(f"{b.initial_state.timeout_p0} {b.initial_state.timeout_p1}")
    lines.append(str(len(b.turns)))
    for ta in b.turns:
        for a in (ta.p0_pod0, ta.p0_pod1, ta.p1_pod0, ta.p1_pod1):
            mode, v = thrust_mode(a.thrust)
            lines.append(f"{a.target_x} {a.target_y} {mode} {v}")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--leaderboard-sample", type=int, default=400,
                    help="Random sample size from leaderboard_battles root (0=skip LB). Default 400")
    ap.add_argument("--all-leaderboard", action="store_true",
                    help="Use ALL leaderboard_battles root JSONs (very slow)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--dir", action="append", default=[], help="Extra battle directory")
    ap.add_argument("--limit", type=int, default=0, help="Max battles after collection (0=all)")
    args = ap.parse_args()

    print("role=DIAGNOSTIC exact Fidelity vs fast_physics on corpora", flush=True)
    print("Collecting battles...", flush=True)
    paths = battle_paths(
        leaderboard_sample=0 if args.all_leaderboard else args.leaderboard_sample,
        all_leaderboard=args.all_leaderboard,
        seed=args.seed,
        extra_dirs=args.dir,
    )
    if args.limit > 0:
        paths = paths[: args.limit]
    print(f"Total unique battles: {len(paths)}", flush=True)
    if not paths:
        print("No battles found", file=sys.stderr)
        return 2

    validator = find_validator()
    print(f"Validator: {validator}", flush=True)

    # Single-shot: prepare all games, one validator process
    chunks: list[str] = []
    skipped = 0
    turn_est = 0
    for i, p in enumerate(paths):
        d = dump_battle(p)
        if d is None:
            skipped += 1
            continue
        chunks.append(d)
        # rough turns from dump
        # optional progress
        if (i + 1) % 200 == 0:
            print(f"  prepared {i+1}/{len(paths)} ...", flush=True)

    payload = "".join(chunks)
    print(f"Running ONE validator process on {len(chunks)} battles (skipped {skipped})...", flush=True)
    env = os.environ.copy()
    env["CSB_FP_QUIET"] = "1"
    r = subprocess.run(
        [str(validator)],
        input=payload,
        text=True,
        capture_output=True,
        cwd=str(ROOT),
        env=env,
    )
    sys.stdout.write(r.stdout)
    if r.stderr:
        sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print(f"FAILED rc={r.returncode}", file=sys.stderr)
        return r.returncode
    print("CORPUS EXACT PASS: fast_physics == Fidelity on all usable battles (single-shot)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
