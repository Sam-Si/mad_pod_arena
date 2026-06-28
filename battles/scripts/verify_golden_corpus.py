#!/usr/bin/env python3
"""
Verify battles/golden_physics_battles/ against physics engine.

Tiers from manifest.csv / expected_pass.txt / expected_fail.txt:
  pass tier  — must be 100% (exit 1 on any fail)
  fail tier  — known divergences; reported, exit 0 unless --strict-fail-tier

Usage:
  python3 battles/scripts/verify_golden_corpus.py
  python3 battles/scripts/verify_golden_corpus.py --tier pass
  python3 battles/scripts/verify_golden_corpus.py --tier fail
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "battles" / "golden_physics_battles"
BATTLES_DIR = GOLDEN / "battles"

sys.path.insert(0, str(ROOT / "sim"))
from verify_battles import run_battle  # noqa: E402
from battle_parser import load_battle  # noqa: E402
from physics_driver import CppPhysics, ensure_driver_built  # noqa: E402


def load_manifest():
    man = GOLDEN / "manifest.csv"
    rows = []
    if man.exists():
        with man.open(newline="") as f:
            rows = list(csv.DictReader(f))
    return rows


def tier_files(tier: str) -> list[tuple[str, str]]:
    """Return list of (filename, expected_result)."""
    rows = load_manifest()
    if not rows:
        # fallback: all json in battles/ as pass
        return [(p.name, "pass") for p in sorted(BATTLES_DIR.glob("battle_*.json"))]
    out = []
    for r in rows:
        er = r.get("expected_result", "pass")
        if tier == "all" or tier == er:
            out.append((r["battle_file"], er))
    return out


def main():
    print("role=GATE_COMPONENT", file=sys.stderr)
    ap = argparse.ArgumentParser()
    ap.add_argument("--tier", choices=["pass", "fail", "all"], default="all")
    ap.add_argument("--strict-fail-tier", action="store_true",
                    help="Also exit non-zero if known_divergence battles unexpectedly PASS")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    if not BATTLES_DIR.is_dir():
        print(f"Missing {BATTLES_DIR}; run build_golden_corpus.py first", file=sys.stderr)
        return 2

    items = tier_files(args.tier)
    if args.limit:
        items = items[: args.limit]

    driver = ensure_driver_built()
    pass_ok = pass_bad = 0
    fail_ok = fail_bad = 0  # fail_ok = still diverges (expected); fail_bad = unexpectedly passes
    details_bad_pass = []
    details_unexpected_pass = []

    print(f"Golden verify tier={args.tier}  n={len(items)}  dir={BATTLES_DIR}")

    for i, (name, expected) in enumerate(items):
        path = BATTLES_DIR / name
        if not path.exists():
            print(f"MISSING: {name}")
            if expected == "pass":
                pass_bad += 1
            continue
        try:
            log = load_battle(str(path))
            phys = CppPhysics(driver)
            phys.init_battle(log.checkpoints, laps=log.laps)
            ft, errs, n_turns, perfect = run_battle(phys, log)
            phys.close()
            ok = ft is None
        except Exception as e:
            ok = False
            ft, errs = -1, [str(e)]

        if expected == "pass":
            if ok:
                pass_ok += 1
            else:
                pass_bad += 1
                details_bad_pass.append((name, ft, errs[0] if errs else ""))
                print(f"REGRESSION (expected pass): {name} turn {ft} — {errs[0] if errs else ''}")
        else:
            if ok:
                fail_bad += 1
                details_unexpected_pass.append(name)
                print(f"NOTE (divergence now PASSES): {name}")
            else:
                fail_ok += 1

        if (i + 1) % 40 == 0:
            print(f"  … {i+1}/{len(items)}")

    print()
    print("=" * 60)
    print("Golden corpus results")
    print("=" * 60)
    if args.tier in ("pass", "all"):
        print(f"  expected_pass:  {pass_ok} ok,  {pass_bad} FAIL (must be 0)")
    if args.tier in ("fail", "all"):
        print(f"  expected_fail:  {fail_ok} still diverge,  {fail_bad} now pass")
    print()

    if pass_bad:
        print("*** REGRESSIONS on pass tier ***")
        for name, ft, err in details_bad_pass[:20]:
            print(f"  {name} t={ft}: {err}")
        return 1

    if args.strict_fail_tier and fail_bad:
        print("*** strict-fail-tier: some known divergences now pass (update manifest?) ***")
        return 1

    if args.tier == "pass":
        print(f"*** ALL {pass_ok} PASS-TIER BATTLES OK ***")
    else:
        print("*** PASS TIER CLEAN ***" if pass_ok or args.tier == "fail" else "done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
