#!/usr/bin/env python3
"""EXACT Fidelity vs fast_physics over every battle_*.json under battles/.

Default: prepare all usable games and run the C++ checker **once** (single
stdin payload, single process). No batching.

  python3 tools/validate_fp_all_battles.py
  python3 tools/validate_fp_all_battles.py --include-timeouts
  python3 tools/validate_fp_all_battles.py --by-content   # one path per distinct bytes
"""
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sim"))
from validate_fast_physics_corpus import dump_battle  # noqa: E402

DEFAULT_VALIDATORS = [
    ROOT / "sim" / "validate_fast_physics_battles",
    ROOT / "bazel-bin" / "src" / "physics" / "validate_fast_physics_battles",
]


def find_validator() -> Path:
    env = os.environ.get("MAD_POD_FP_VALIDATOR")
    if env:
        p = Path(env)
        if p.is_file() and os.access(p, os.X_OK):
            return p
    for p in DEFAULT_VALIDATORS:
        if p.is_file() and os.access(p, os.X_OK):
            return p
    out = ROOT / "sim" / "validate_fast_physics_battles"
    src = ROOT / "src" / "physics" / "validate_fast_physics_battles.cpp"
    cmd = [
        "c++", "-std=c++17", "-O3", "-DNDEBUG", "-ffp-contract=off", "-fno-math-errno",
        f"-I{ROOT / 'src' / 'physics'}", str(src), "-o", str(out),
    ]
    print("Building", out, flush=True)
    subprocess.check_call(cmd)
    out.chmod(out.stat().st_mode | 0o111)
    return out


def collect_all_files() -> list[Path]:
    return sorted(
        p for p in (ROOT / "battles").rglob("battle_*.json")
        if ".divergence." not in p.name
    )


def select_paths(files: list[Path], *, by_content: bool, include_timeouts: bool) -> list[Path]:
    if not include_timeouts:
        files = [
            p for p in files
            if "timeouts" not in p.parts
        ]
    if not by_content:
        return files
    seen: dict[str, Path] = {}
    for p in files:
        h = hashlib.sha1(p.read_bytes()).hexdigest()
        seen.setdefault(h, p)
    return sorted(seen.values(), key=lambda p: str(p))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--include-timeouts", action="store_true",
                    help="Include *_timeouts directories (default: skip them)")
    ap.add_argument("--by-content", action="store_true",
                    help="Dedup identical file bytes (still one shot; fewer games)")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--log", type=Path, default=ROOT / "logs" / "fp_all_battles_exact.log")
    args = ap.parse_args()

    print("role=DIAGNOSTIC exact Fidelity vs fast_physics — single-shot run", flush=True)
    validator = find_validator()
    print(f"Validator: {validator}", flush=True)

    all_files = collect_all_files()
    paths = select_paths(
        all_files, by_content=args.by_content, include_timeouts=args.include_timeouts
    )
    if args.limit > 0:
        paths = paths[: args.limit]
    print(
        f"files_on_disk={len(all_files)} selected_paths={len(paths)} "
        f"(by_content={args.by_content} include_timeouts={args.include_timeouts})",
        flush=True,
    )

    args.log.parent.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    chunks: list[str] = []
    skipped = 0
    for i, p in enumerate(paths):
        d = dump_battle(p)
        if d is None:
            skipped += 1
            continue
        chunks.append(d)
        if (i + 1) % 2000 == 0:
            print(f"  prepared {i+1}/{len(paths)} usable={len(chunks)} skipped={skipped}", flush=True)

    payload = "".join(chunks)
    print(
        f"Running ONE validator process on {len(chunks)} games "
        f"(skipped {skipped} unusable) payload_bytes={len(payload):,} ...",
        flush=True,
    )
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
    elapsed = time.time() - t0
    sys.stdout.write(r.stdout)
    if r.stderr:
        sys.stderr.write(r.stderr)

    summary = (
        f"\nDONE in {elapsed:.1f}s selected={len(paths)} usable={len(chunks)} "
        f"skipped={skipped} rc={r.returncode}\n"
    )
    print(summary, flush=True)
    args.log.write_text(
        f"single-shot validate\nselected={len(paths)}\nusable={len(chunks)}\n"
        f"skipped={skipped}\nrc={r.returncode}\nelapsed_s={elapsed:.1f}\n"
        f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}\n"
    )
    if r.returncode != 0:
        print("CORPUS EXACT FAIL", file=sys.stderr)
        return r.returncode
    print("CORPUS EXACT PASS: fast_physics == Fidelity on all usable games in one run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
