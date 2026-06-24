#!/usr/bin/env python3
"""Enforce battles/RETENTION.md — only battle ids > MIN_BATTLE_ID may exist."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

MIN_BATTLE_ID = 870230019  # exclusive: keep strictly greater than this

REPO_BATTLES = Path(__file__).resolve().parents[1]
SCAN_DIRS = (
    "leaderboard_battles",
    "leaderboard_timeouts",
    "test_session_battles",
    "test_session_timeouts",
)


def battle_id(path: Path) -> int | None:
    m = re.search(r"battle_(\d+)\.json$", path.name)
    return int(m.group(1)) if m else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--delete", action="store_true", help="Hard-delete offenders")
    ap.add_argument(
        "--root",
        type=Path,
        default=REPO_BATTLES,
        help="battles/ directory (default: repo battles/)",
    )
    args = ap.parse_args()

    offenders: list[Path] = []
    kept = 0
    for sub in SCAN_DIRS:
        d = args.root / sub
        if not d.is_dir():
            continue
        for p in d.rglob("battle_*.json"):
            bid = battle_id(p)
            if bid is None:
                continue
            if bid <= MIN_BATTLE_ID:
                offenders.append(p)
            else:
                kept += 1

    print(f"MIN_BATTLE_ID (exclusive) = {MIN_BATTLE_ID}")
    print(f"kept (id > cutoff): {kept}")
    print(f"offenders (id <= cutoff): {len(offenders)}")

    if not offenders:
        print("OK — retention policy satisfied.")
        return 0

    for p in sorted(offenders)[:20]:
        print(f"  OFFEND: {p.relative_to(args.root.parent) if args.root.parent in p.parents else p}")
    if len(offenders) > 20:
        print(f"  ... and {len(offenders) - 20} more")

    if args.delete:
        for p in offenders:
            p.unlink(missing_ok=True)
        print(f"Deleted {len(offenders)} files.")
        return 0

    print("Run with --delete to prune, or fix scrapers.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
