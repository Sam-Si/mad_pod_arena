#!/usr/bin/env python3
"""
Build battles/golden_physics_battles/ — a fixed ~200-battle regression corpus.

Composition (default target 200):
  1. ALL leaderboard_physics_divergences battles  (expected_fail — regression tracking)
  2. Stratified sample from test_session_battles  (expected_pass — core gate material)
  3. Stratified sample from leaderboard_battles   (expected_pass — breadth; excludes
     divergence IDs, timeouts, and duplicates)

Copies JSON into golden_physics_battles/battles/ and writes manifest.csv + README.

Re-run anytime after adding divergences or changing sample counts:
  python3 battles/scripts/build_golden_corpus.py
  python3 battles/scripts/build_golden_corpus.py --target 200 --seed 42
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # mad_pod_arena/
BATTLES = ROOT / "battles"
OUT = BATTLES / "golden_physics_battles"
OUT_BATTLES = OUT / "battles"
MIN_ID = 870230019

BATTLE_RE = re.compile(r"battle_(\d+)\.json$")


def battle_id(path: Path) -> int | None:
    m = BATTLE_RE.search(path.name)
    return int(m.group(1)) if m else None


def list_battles(dir_path: Path) -> list[Path]:
    if not dir_path.is_dir():
        return []
    files = sorted(dir_path.rglob("battle_*.json"))
    # Only real battle JSON, not .divergence.json
    out = []
    for f in files:
        if f.name.endswith(".divergence.json"):
            continue
        bid = battle_id(f)
        if bid is None or bid <= MIN_ID:
            continue
        out.append(f)
    return out


def stable_bucket(path: Path, n_buckets: int = 20) -> int:
    """Deterministic bucket from filename (not content) for stratified sampling."""
    h = hashlib.sha1(path.name.encode()).hexdigest()
    return int(h[:8], 16) % n_buckets


def stratified_sample(paths: list[Path], k: int, seed: int, exclude: set[str]) -> list[Path]:
    """Sample k paths evenly across hash buckets; deterministic via seed in tie-break."""
    candidates = [p for p in paths if p.name not in exclude]
    if k <= 0 or not candidates:
        return []
    if k >= len(candidates):
        return sorted(candidates, key=lambda p: p.name)

    buckets: dict[int, list[Path]] = defaultdict(list)
    for p in candidates:
        buckets[stable_bucket(p)].append(p)
    for b in buckets:
        # Sort within bucket; rotate by seed for variety without randomness
        buckets[b].sort(key=lambda p: p.name)
        rot = seed % max(len(buckets[b]), 1)
        buckets[b] = buckets[b][rot:] + buckets[b][:rot]

    selected: list[Path] = []
    bucket_ids = sorted(buckets.keys())
    i = 0
    while len(selected) < k:
        progressed = False
        for bid in bucket_ids:
            if len(selected) >= k:
                break
            lst = buckets[bid]
            if not lst:
                continue
            selected.append(lst.pop(0))
            progressed = True
        if not progressed:
            break
        i += 1
        if i > k + 50:
            break
    return sorted(selected, key=lambda p: p.name)


def load_divergence_manifest() -> dict[str, dict]:
    man = BATTLES / "leaderboard_physics_divergences" / "manifest.csv"
    out: dict[str, dict] = {}
    if not man.exists():
        return out
    with man.open(newline="") as f:
        for row in csv.DictReader(f):
            out[row["battle_file"]] = row
    return out


def quick_valid_battle(path: Path) -> bool:
    """Cheap structural check — battle has frames/agents and is parseable JSON."""
    try:
        with path.open() as f:
            # Read only start — large files; full load if small
            size = path.stat().st_size
            if size < 50_000_000:
                data = json.load(f)
            else:
                head = f.read(500_000)
                if '"frames"' not in head:
                    return False
                f.seek(0)
                data = json.load(f)
        if not isinstance(data, dict):
            return False
        frames = data.get("frames")
        if not frames or not isinstance(frames, list) or len(frames) < 4:
            return False
        # At least one stdout action frame
        has_stdout = any(isinstance(fr, dict) and fr.get("stdout") for fr in frames[:20])
        return has_stdout or len(frames) > 2
    except Exception:
        return False


def copy_into_golden(src: Path, dst_dir: Path) -> Path:
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst = dst_dir / src.name
    if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
        shutil.copy2(src, dst)
    return dst


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target", type=int, default=200, help="Total battles in golden set")
    ap.add_argument("--seed", type=int, default=42, help="Deterministic sampling seed")
    ap.add_argument("--test-session-quota", type=int, default=None,
                    help="Override count from test_session_battles")
    ap.add_argument("--leaderboard-quota", type=int, default=None,
                    help="Override count from leaderboard_battles")
    ap.add_argument("--skip-validity-check", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    div_dir = BATTLES / "leaderboard_physics_divergences" / "battles"
    ts_dir = BATTLES / "test_session_battles"
    lb_dir = BATTLES / "leaderboard_battles"

    div_paths = list_battles(div_dir)
    # Prefer copies that are only .json not paired wrong; divergences folder has both
    div_paths = [p for p in div_paths if p.suffix == ".json"]
    div_manifest = load_divergence_manifest()
    div_names = {p.name for p in div_paths}

    n_div = len(div_paths)
    remaining = max(args.target - n_div, 0)

    # Split remaining ~55% test_session, ~45% leaderboard (test_session is the proven gate)
    if args.test_session_quota is not None:
        n_ts = args.test_session_quota
    else:
        n_ts = int(round(remaining * 0.55))
    if args.leaderboard_quota is not None:
        n_lb = args.leaderboard_quota
    else:
        n_lb = remaining - n_ts

    ts_all = list_battles(ts_dir)
    lb_all = list_battles(lb_dir)

    ts_sample = stratified_sample(ts_all, n_ts, args.seed, exclude=set())
    lb_sample = stratified_sample(lb_all, n_lb, args.seed + 1, exclude=div_names)

    # Validity filter (drop bad, top-up from same pool)
    def filter_valid(paths: list[Path], pool: list[Path], need: int, exclude: set[str]) -> list[Path]:
        if args.skip_validity_check:
            return paths[:need]
        good = [p for p in paths if quick_valid_battle(p)]
        if len(good) >= need:
            return good[:need]
        # top-up
        have = {p.name for p in good}
        for p in pool:
            if len(good) >= need:
                break
            if p.name in have or p.name in exclude:
                continue
            if quick_valid_battle(p):
                good.append(p)
                have.add(p.name)
        return good

    ts_sample = filter_valid(ts_sample, ts_all, n_ts, set())
    lb_sample = filter_valid(lb_sample, lb_all, n_lb, div_names)

    rows: list[dict] = []

    def add_row(path: Path, source: str, role: str, expected: str, extra: dict | None = None):
        bid = battle_id(path)
        r = {
            "battle_file": path.name,
            "battle_id": bid or "",
            "source_dir": source,
            "role": role,
            "expected_result": expected,  # pass | fail
            "first_fail_turn": "",
            "error_types": "",
            "notes": "",
        }
        if extra:
            r.update(extra)
        rows.append((path, r))

    for p in sorted(div_paths, key=lambda x: x.name):
        dm = div_manifest.get(p.name, {})
        add_row(
            p,
            source="leaderboard_physics_divergences",
            role="known_divergence",
            expected="fail",
            extra={
                "first_fail_turn": dm.get("first_fail_turn", ""),
                "error_types": dm.get("error_types", ""),
                "notes": (dm.get("error_summary") or "")[:120],
            },
        )

    for p in ts_sample:
        add_row(p, "test_session_battles", "golden_sample", "pass")

    for p in lb_sample:
        add_row(p, "leaderboard_battles", "golden_sample", "pass")

    # Dedupe by filename (divergences win)
    seen: set[str] = set()
    final: list[tuple[Path, dict]] = []
    for path, row in rows:
        if row["battle_file"] in seen:
            continue
        seen.add(row["battle_file"])
        final.append((path, row))

    pass_rows = [r for _, r in final if r["expected_result"] == "pass"]
    fail_rows = [r for _, r in final if r["expected_result"] == "fail"]

    print(f"Golden corpus plan (target={args.target}, seed={args.seed}):")
    print(f"  known_divergence (expected fail): {len(fail_rows)}")
    print(f"  test_session samples (expected pass): {sum(1 for _,r in final if r['source_dir']=='test_session_battles')}")
    print(f"  leaderboard samples (expected pass): {sum(1 for _,r in final if r['source_dir']=='leaderboard_battles')}")
    print(f"  total unique: {len(final)}")

    if args.dry_run:
        return 0

    # Rebuild output dir battles/ cleanly but keep README if we write after
    if OUT_BATTLES.exists():
        for old in OUT_BATTLES.glob("battle_*.json"):
            old.unlink()
    OUT_BATTLES.mkdir(parents=True, exist_ok=True)

    for path, row in final:
        copy_into_golden(path, OUT_BATTLES)

    man_path = OUT / "manifest.csv"
    fieldnames = [
        "battle_file", "battle_id", "source_dir", "role", "expected_result",
        "first_fail_turn", "error_types", "notes",
    ]
    with man_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for _, row in sorted(final, key=lambda x: (x[1]["expected_result"], x[1]["battle_file"])):
            w.writerow({k: row.get(k, "") for k in fieldnames})

    # expected_pass.txt / expected_fail.txt for easy CI filtering
    pass_list = OUT / "expected_pass.txt"
    fail_list = OUT / "expected_fail.txt"
    pass_list.write_text("\n".join(sorted(r["battle_file"] for _, r in final if r["expected_result"] == "pass")) + "\n")
    fail_list.write_text("\n".join(sorted(r["battle_file"] for _, r in final if r["expected_result"] == "fail")) + "\n")

    readme = OUT / "README.md"
    readme.write_text(f"""# Golden physics corpus (~{len(final)} battles)

Fixed regression set for referee physics (`src/physics/physics.h`).

Generated: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}  
Builder: `battles/scripts/build_golden_corpus.py` (seed={args.seed}, target={args.target})

## Why this set

| Tier | Count | `expected_result` | Purpose |
|------|------:|-------------------|---------|
| **known_divergence** | {len(fail_rows)} | `fail` | All battles from `leaderboard_physics_divergences/` — knife-edge cases we track but do **not** require 100% pass |
| **golden_sample** (test_session) | {sum(1 for _,r in final if r['source_dir']=='test_session_battles')} | `pass` | Stratified slice of the original CI gate corpus |
| **golden_sample** (leaderboard) | {sum(1 for _,r in final if r['source_dir']=='leaderboard_battles')} | `pass` | Breadth across post-cutoff leaderboard maps/playstyles |

Timeouts (`*_timeouts/`) are **excluded** — those are agent/runtime failures, not physics fidelity.

## Files

```
golden_physics_battles/
├── README.md
├── manifest.csv           # full index + expected_result + divergence metadata
├── expected_pass.txt      # filenames that must verify clean
├── expected_fail.txt      # known divergences (tracked, not gated)
└── battles/
    └── battle_*.json      # copies (sources unchanged)
```

## How to verify

```bash
# Only expected-pass subset (CI-style gate on this corpus)
python3 battles/scripts/verify_golden_corpus.py --tier pass

# Full report: pass tier must be 100%; fail tier reported but not fatal
python3 battles/scripts/verify_golden_corpus.py

# Rebuild this folder after changing quotas/divergences
python3 battles/scripts/build_golden_corpus.py --target 200 --seed 42
```

## CI recommendation

- **Hard gate:** `expected_pass` tier via `verify_golden_corpus.py --tier pass` (or keep using full `test_session_battles`)
- **Informational:** `expected_fail` tier count should stay stable; new unexpected fails on pass tier = regression

Do not require 100% on the whole folder — it intentionally includes known divergences.
""")

    print(f"Wrote {OUT}")
    print(f"  battles: {len(list(OUT_BATTLES.glob('battle_*.json')))}")
    print(f"  manifest: {man_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
