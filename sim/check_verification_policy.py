#!/usr/bin/env python3
"""Static enforcement for docs/VERIFICATION_TRUTH_POLICY.md. Run from repo root."""

from __future__ import annotations

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SIM = os.path.join(ROOT, "sim")
sys.path.insert(0, SIM)

EXPECTED_GATE = (5.0, 3.0, 1.0, 1)
FORBIDDEN_LEGACY = (
    "A battle PASSes only if **every turn** matches within rounding tolerance "
    "(pos ±1, vel ±1, angle ±1°)."
)


def fail(msg: str) -> None:
    print(f"POLICY FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    import tolerance_policy as tp

    got = (
        tp.GATE_POS_TOL,
        tp.GATE_VEL_TOL,
        tp.GATE_ANG_TOL_DEG,
        tp.GATE_TIMEOUT_TOL,
    )
    if got != EXPECTED_GATE:
        fail(f"GATE tuple {got!r} != {EXPECTED_GATE!r}")

    for rel in ("sim/tolerance_policy.py", "sim/compare_util.py"):
        if not os.path.isfile(os.path.join(ROOT, rel)):
            fail(f"missing {rel}")

    ci_path = os.path.join(ROOT, ".github/workflows/ci.yml")
    ci = open(ci_path, encoding="utf-8").read()
    for needle in (
        "physics-accuracy:",
        "sim/verify_battles.py --gate battles/test_session_battles",
        "verify_golden_corpus.py --tier pass",
        "MAD_POD_GATE_STRICT",
        "check_verification_policy.py",
    ):
        if needle not in ci:
            fail(f"ci.yml missing {needle!r}")

    if "src/physics:test_physics" not in ci and "//src/physics:test_physics" not in ci:
        fail("ci.yml missing test_physics reference")

    if "no Python required" in ci:
        fail("ci.yml still contains 'no Python required'")

    # Both gate Python steps must set STRICT in their run block (not only a comment)
    if ci.count("MAD_POD_GATE_STRICT=1") < 2 and ci.count("MAD_POD_GATE_STRICT: 1") < 2:
        # allow either inline export style twice
        if ci.count("MAD_POD_GATE_STRICT") < 2:
            fail("ci.yml must set MAD_POD_GATE_STRICT on both (A) and (B) steps (>=2 occurrences)")

    # No C++ corpus run in non-comment run lines
    for i, line in enumerate(ci.splitlines(), 1):
        stripped = line.lstrip()
        if stripped.startswith("#"):
            continue
        if re.search(r"verify_battles(\.cpp)?\s+.*--(dir|file)\b", line):
            fail(f"ci.yml line {i}: C++ corpus verify looks merge-blocking: {line.strip()}")
        if "bazel-bin/src/physics/verify_battles" in line and ("--dir" in line or "--file" in line):
            fail(f"ci.yml line {i}: C++ verify_battles corpus run forbidden: {line.strip()}")

    for title in (
        "Build physics targets (C++ diagnostic binary + tests + replay_driver)",
        "Gate (A): Python verify_battles --gate test_session",
        "Gate (B): golden corpus --tier pass",
        "Gate (U): test_physics",
    ):
        if title not in ci:
            fail(f"ci.yml missing required step name {title!r}")

    # Historical write-up lives under docs/archive/ (active law is VERIFICATION_TRUTH_POLICY).
    pv_path = os.path.join(ROOT, "docs/archive/physics-verification.md")
    if not os.path.isfile(pv_path):
        pv_path = os.path.join(ROOT, "docs/physics-verification.md")
    if os.path.isfile(pv_path):
        pv = open(pv_path, encoding="utf-8").read()
        if "sim/tolerance_policy" not in pv:
            fail(f"{pv_path} must contain 'sim/tolerance_policy'")
        if FORBIDDEN_LEGACY in pv:
            fail(f"{pv_path} still has forbidden ±1 gate sentence")

    readme = open(os.path.join(ROOT, "README.md"), encoding="utf-8").read()
    if "verify_battles.py --gate" not in readme and "MERGE_PHYSICS_OK" not in readme:
        fail("README.md must mention verify_battles.py --gate or MERGE_PHYSICS_OK")
    if "diagnostic" not in readme.lower() and "DIAGNOSTIC" not in readme:
        fail("README.md must mention diagnostic/DIAGNOSTIC for C++ path")

    gem = open(os.path.join(ROOT, "GEMINI.md"), encoding="utf-8").read()
    if "verify_battles.py --gate" not in gem and "MERGE_PHYSICS_OK" not in gem:
        fail("GEMINI.md must mention verify_battles.py --gate or MERGE_PHYSICS_OK")

    sim_readme = open(os.path.join(ROOT, "sim/README.md"), encoding="utf-8").read()
    if "--gate" not in sim_readme and "MAD_POD_GATE_STRICT" not in sim_readme:
        fail("sim/README.md must mention --gate or MAD_POD_GATE_STRICT")

    sched = open(os.path.join(ROOT, ".github/workflows/scheduled-tests.yml"), encoding="utf-8").read()
    if "Nightly C++ verify test_session (diagnostic)" not in sched:
        fail("scheduled-tests.yml missing renamed diagnostic step title")

    # No MAD_POD_CLAIM_GATE in scoped trees
    for dirpath, _, files in os.walk(ROOT):
        if any(
            x in dirpath
            for x in (
                "/.git",
                "/bazel-",
                "/node_modules",
                "/leaderboard_battles",
                "/test_session",
                "/golden_physics",
            )
        ):
            continue
        if not any(
            dirpath.startswith(os.path.join(ROOT, d))
            for d in ("sim", "battles/scripts", ".github/workflows")
        ) and dirpath != os.path.join(ROOT, "sim") and not dirpath.startswith(
            os.path.join(ROOT, "sim")
        ):
            # only scan sim, battles/scripts, .github/workflows
            if not (
                dirpath.startswith(os.path.join(ROOT, "sim"))
                or dirpath.startswith(os.path.join(ROOT, "battles", "scripts"))
                or dirpath.startswith(os.path.join(ROOT, ".github", "workflows"))
            ):
                continue
        for fn in files:
            if not fn.endswith((".py", ".yml", ".yaml", ".md", ".cpp", ".h")):
                continue
            fp = os.path.join(dirpath, fn)
            try:
                body = open(fp, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            # Allow this checker file to mention the forbidden symbol as a string to detect
            if "MAD_POD_CLAIM_GATE" in body and not fp.endswith(
                "check_verification_policy.py"
            ):
                fail(f"MAD_POD_CLAIM_GATE found in {fp}")

    # Fowler (1999): permanent tests/guards against design smells / dual owners.
    import check_ssot_policy

    check_ssot_policy.main()

    print("POLICY OK: verification truth policy static checks passed")


if __name__ == "__main__":
    main()
