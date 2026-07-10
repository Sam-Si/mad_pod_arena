#!/usr/bin/env python3
"""Enforce battles/RETENTION.md — id cutoff and optional truncated-replay purge."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

MIN_BATTLE_ID = 870230019  # exclusive: keep strictly greater than this
# n_frames > 1 + 2*n_turns + MARGIN → incomplete action stream (invalid for physics)
TRUNCATED_FRAME_MARGIN = 4

REPO_BATTLES = Path(__file__).resolve().parents[1]
SCAN_DIRS = (
    "leaderboard_battles",
    "leaderboard_timeouts",
    "test_session_battles",
    "test_session_timeouts",
    "golden_physics_battles/battles",
    "leaderboard_physics_divergences/battles",
    "quick_physics_accuracy/battles",
)


def battle_id(path: Path) -> int | None:
    m = re.search(r"battle_(\d+)\.json$", path.name)
    return int(m.group(1)) if m else None


def count_complete_turns(frames: list) -> int:
    """Mirror sim/battle_parser.py: need both players' 2-line stdout + 4-pod keyframe."""
    t = 0
    while True:
        f_p0_idx = 2 * t + 1
        f_p1_idx = 2 * t + 2
        if f_p1_idx >= len(frames):
            break
        f_p0 = frames[f_p0_idx]
        f_p1 = frames[f_p1_idx]
        for fr in (f_p0, f_p1):
            stdout = (fr.get("stdout") or "").strip()
            lines = [ln for ln in stdout.splitlines() if ln.strip()]
            if len(lines) < 2:
                return t
        view = f_p1.get("view") or ""
        # crude: need 4 pod state blocks (lines starting with digit/dot after frame header)
        pod_lines = 0
        for ln in view.splitlines():
            s = ln.strip()
            if not s:
                continue
            if s[0].isdigit() or s[0] in "-.":
                parts = s.split()
                if len(parts) >= 12:
                    pod_lines += 1
        if pod_lines < 4:
            return t
        t += 1
    return t


def is_truncated_replay(path: Path) -> bool:
    if ".divergence." in path.name:
        return False
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return False
    frames = raw.get("frames") or []
    n_turns = count_complete_turns(frames)
    n_frames = len(frames)
    return n_frames > 1 + 2 * n_turns + TRUNCATED_FRAME_MARGIN


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--delete", action="store_true", help="Hard-delete offenders")
    ap.add_argument(
        "--truncated",
        action="store_true",
        help="Also scan/remove truncated action streams (frames >> turns)",
    )
    ap.add_argument(
        "--root",
        type=Path,
        default=REPO_BATTLES,
        help="battles/ directory (default: repo battles/)",
    )
    args = ap.parse_args()

    offenders: list[Path] = []
    truncated: list[Path] = []
    kept = 0
    for sub in SCAN_DIRS:
        d = args.root / sub
        if not d.is_dir():
            continue
        for p in d.rglob("battle_*.json"):
            if ".divergence." in p.name:
                continue
            bid = battle_id(p)
            if bid is None:
                continue
            if bid <= MIN_BATTLE_ID:
                offenders.append(p)
            else:
                kept += 1
            if args.truncated and is_truncated_replay(p):
                truncated.append(p)

    print(f"MIN_BATTLE_ID (exclusive) = {MIN_BATTLE_ID}")
    print(f"kept (id > cutoff, id scan): {kept}")
    print(f"offenders (id <= cutoff): {len(offenders)}")
    if args.truncated:
        print(f"truncated action streams: {len(truncated)}")

    bad = list(offenders)
    if args.truncated:
        # unique paths
        seen = {p.resolve() for p in bad}
        for p in truncated:
            if p.resolve() not in seen:
                bad.append(p)
                seen.add(p.resolve())

    if not bad and not (args.truncated and truncated):
        if not offenders and not (args.truncated and truncated):
            print("OK — retention policy satisfied.")
            return 0

    if not offenders and args.truncated and not truncated:
        print("OK — retention + no truncated replays.")
        return 0

    if offenders:
        print("ID cutoff offenders:")
        for p in sorted(offenders)[:20]:
            print(f"  OFFEND: {p}")
        if len(offenders) > 20:
            print(f"  ... and {len(offenders) - 20} more")
    if args.truncated and truncated:
        print("Truncated replays:")
        for p in sorted(truncated)[:20]:
            print(f"  TRUNC: {p}")
        if len(truncated) > 20:
            print(f"  ... and {len(truncated) - 20} more")

    to_delete = list(offenders)
    if args.truncated:
        seen = {p.resolve() for p in to_delete}
        for p in truncated:
            if p.resolve() not in seen:
                to_delete.append(p)

    if not to_delete:
        print("OK — retention policy satisfied.")
        return 0

    if args.delete:
        for p in to_delete:
            p.unlink(missing_ok=True)
        print(f"Deleted {len(to_delete)} files.")
        return 0

    print("Run with --delete to prune, or fix scrapers.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
