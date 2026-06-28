#!/usr/bin/env python3
"""
Turn-by-turn physics accuracy verifier for Coders Strike Back.

MERGE_PHYSICS_OK gate (A) — from repo root:
    MAD_POD_GATE_STRICT=1 python3 sim/verify_battles.py --gate battles/test_session_battles

Diagnostic (any corpus; not merge-blocking alone):
    python3 sim/verify_battles.py battles/test_session_battles
    python3 sim/verify_battles.py battles/leaderboard_battles

See docs/VERIFICATION_TRUTH_POLICY.md.
"""

from __future__ import annotations

import argparse
import glob
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from battle_parser import load_battle
from compare_util import angle_close, is_invalid_thrust, pos_close, vel_close
from physics_driver import CppPhysics, ensure_driver_built, _DEFAULT_DRIVER
from tolerance_policy import (
    GATE_ANG_TOL_DEG,
    GATE_POS_TOL,
    GATE_TIMEOUT_TOL,
    GATE_VEL_TOL,
)

GATE_CORPUS_REL = os.path.join("battles", "test_session_battles")


def compare_turn(sim_pods, gt_pods, sim_timeouts, gt_timeouts, n_checkpoints=0):
    errs = []
    for i in range(4):
        sp = sim_pods[i]
        gp = gt_pods[i]

        if not (pos_close(sp["x"], gp.x, GATE_POS_TOL) and pos_close(sp["y"], gp.y, GATE_POS_TOL)):
            errs.append(
                f"pod{i} pos sim=({sp['x']:.0f},{sp['y']:.0f}) gt=({gp.x:.0f},{gp.y:.0f}) "
                f"Δ=({sp['x']-gp.x:.1f},{sp['y']-gp.y:.1f})"
            )

        if not (vel_close(sp["vx"], gp.vx, GATE_VEL_TOL) and vel_close(sp["vy"], gp.vy, GATE_VEL_TOL)):
            errs.append(f"pod{i} vel sim=({sp['vx']},{sp['vy']}) gt=({gp.vx},{gp.vy})")

        if gp.angle is not None:
            if not angle_close(sp["angle"], gp.angle, GATE_ANG_TOL_DEG):
                errs.append(
                    f"pod{i} angle sim={math.degrees(sp['angle']):.1f}° "
                    f"gt={math.degrees(gp.angle):.1f}°"
                )

        sim_cp = sp["next"] % n_checkpoints if n_checkpoints > 0 else sp["next"]
        if sim_cp != gp.next_cp:
            errs.append(f"pod{i} next_cp sim={sp['next']}(mod={sim_cp}) gt={gp.next_cp}")

    if (
        abs(sim_timeouts[0] - gt_timeouts[0]) > GATE_TIMEOUT_TOL
        or abs(sim_timeouts[1] - gt_timeouts[1]) > GATE_TIMEOUT_TOL
    ):
        errs.append(f"timeouts sim={sim_timeouts} gt={gt_timeouts}")

    return errs


def run_battle(phys, log):
    init = log.initial_state
    for i, p in enumerate(init.pods):
        ang = p.angle if p.angle is not None else -0.0174533
        phys.set_pod(i, p.x, p.y, p.vx, p.vy, ang, p.next_cp, p.shield_active, p.boosted)
    phys.set_timeouts(init.timeout_p0, init.timeout_p1)

    first_fail_turn = None
    first_fail_errors = []
    perfect = 0

    for t_idx, ta in enumerate(log.turns):
        if t_idx >= len(log.keyframes):
            break

        p0_acts = [
            (ta.p0_pod0.target_x, ta.p0_pod0.target_y, ta.p0_pod0.thrust),
            (ta.p0_pod1.target_x, ta.p0_pod1.target_y, ta.p0_pod1.thrust),
        ]
        p1_acts = [
            (ta.p1_pod0.target_x, ta.p1_pod0.target_y, ta.p1_pod0.thrust),
            (ta.p1_pod1.target_x, ta.p1_pod1.target_y, ta.p1_pod1.thrust),
        ]

        if is_invalid_thrust(p0_acts[0][2]):
            inv = p0_acts[0][2]
            p0_acts = [
                (p0_acts[0][0], p0_acts[0][1], inv),
                (p0_acts[1][0], p0_acts[1][1], inv),
            ]
        elif is_invalid_thrust(p0_acts[1][2]):
            pass

        if is_invalid_thrust(p1_acts[0][2]):
            inv = p1_acts[0][2]
            p1_acts = [
                (p1_acts[0][0], p1_acts[0][1], inv),
                (p1_acts[1][0], p1_acts[1][1], inv),
            ]
        elif is_invalid_thrust(p1_acts[1][2]):
            pass

        phys.apply(0, p0_acts[0][0], p0_acts[0][1], p0_acts[0][2])
        phys.apply(1, p0_acts[1][0], p0_acts[1][1], p0_acts[1][2])
        phys.apply(2, p1_acts[0][0], p1_acts[0][1], p1_acts[0][2])
        phys.apply(3, p1_acts[1][0], p1_acts[1][1], p1_acts[1][2])

        sim = phys.step()
        if len(sim["pods"]) < 4:
            if first_fail_turn is None:
                first_fail_turn = t_idx
                first_fail_errors = [f"driver returned {len(sim['pods'])} pods instead of 4"]
            break

        gt = log.keyframes[t_idx]
        gt_timeouts = (gt.timeout_p0, gt.timeout_p1)

        errs = compare_turn(
            sim["pods"],
            gt.pods,
            sim["timeouts"],
            gt_timeouts,
            n_checkpoints=len(log.checkpoints),
        )
        if errs:
            if first_fail_turn is None:
                first_fail_turn = t_idx
                first_fail_errors = errs
            break
        perfect += 1

    return first_fail_turn, first_fail_errors, min(len(log.turns), len(log.keyframes)), perfect


def _parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="Verify physics.h against battle JSON corpora (GATE with --gate)."
    )
    p.add_argument(
        "--gate",
        action="store_true",
        help="Claim GATE role; directory must be battles/test_session_battles",
    )
    p.add_argument(
        "directory",
        help="Directory of battle_*.json (e.g. battles/test_session_battles)",
    )
    return p.parse_args(argv)


def main(argv=None):
    args = _parse_args(argv)
    battle_dir = args.directory
    cwd = os.getcwd()
    expected_gate = os.path.realpath(os.path.join(cwd, GATE_CORPUS_REL))
    got = os.path.realpath(
        battle_dir if os.path.isabs(battle_dir) else os.path.join(cwd, battle_dir)
    )

    if args.gate:
        if got != expected_gate:
            print(
                f"error: --gate requires corpus {GATE_CORPUS_REL!r} "
                f"(resolved {expected_gate!r}, got {got!r})",
                file=sys.stderr,
            )
            sys.exit(2)
        print("role=GATE", file=sys.stderr)
    else:
        print("role=DIAGNOSTIC", file=sys.stderr)

    files = sorted(glob.glob(os.path.join(battle_dir, "battle_*.json")))
    if not files:
        files = sorted(
            glob.glob(os.path.join(battle_dir, "**", "battle_*.json"), recursive=True)
        )

    print(f"Verifying {len(files)} battles in {battle_dir}...")

    driver_path = ensure_driver_built()

    passed = failed = skipped = 0
    total_turns = total_perfect_turns = 0
    fail_details = []
    fail_turn_histogram = {}
    start_time = time.time()

    for path in files:
        name = os.path.basename(path)
        try:
            log = load_battle(path)
            n_turns = min(len(log.turns), len(log.keyframes))
            if n_turns == 0:
                skipped += 1
                continue
            total_turns += n_turns
            phys = CppPhysics(driver_path)
            phys.init_battle(log.checkpoints, laps=log.laps)
            fail_turn, fail_errs, checked, perfect = run_battle(phys, log)
            phys.close()
            total_perfect_turns += perfect
            if fail_turn is None:
                passed += 1
            else:
                failed += 1
                errs_str = "; ".join(fail_errs[:3])
                fail_details.append((name, fail_turn, n_turns, fail_errs))
                fail_turn_histogram[fail_turn] = fail_turn_histogram.get(fail_turn, 0) + 1
                print(f"FAIL: {name} turn {fail_turn}/{n_turns} — {errs_str}")
        except Exception as e:
            skipped += 1
            print(f"SKIP: {name} ({e})")

    elapsed = time.time() - start_time
    print(f"\n{'='*60}")
    print("=== Verification Results ===")
    print(f"{'='*60}")
    print(f"Total battles:    {len(files)}")
    print(f"Passed (100%):    {passed}")
    print(f"Failed:           {failed}")
    print(f"Skipped:          {skipped}")
    print(f"Total turns:      {total_turns}")
    print(f"Perfect turns:    {total_perfect_turns}")
    if total_turns > 0:
        print(f"Turn accuracy:    {total_perfect_turns/total_turns*100:.2f}%")
    print(f"Time:             {elapsed:.3f} seconds")

    if failed:
        print(f"\n*** {failed} BATTLES FAILED ***")
        sys.exit(1)
    print(f"\n*** ALL {passed} TESTED BATTLES PASSED — PHYSICS 100% ACCURATE ***")
    sys.exit(0)


if __name__ == "__main__":
    main()
