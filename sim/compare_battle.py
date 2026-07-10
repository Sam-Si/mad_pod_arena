#!/usr/bin/env python3
"""
Full battle replay validator (DIAGNOSTIC — not MERGE_PHYSICS_OK alone).

Usage (from repo root):
    python3 sim/compare_battle.py battles/.../battle_XXX.json
    python3 sim/compare_battle.py battles/.../battle_XXX.json --gate-tolerances
    python3 sim/compare_battle.py battles/.../battle_XXX.json --exact
    python3 sim/compare_battle.py battles/.../battle_XXX.json --exact --continue-on-fail

--exact: zero-tol pos/vel/timeout/next_cp; angle within EXACT_ANG_EPS_RAD (text/double noise).
         Prints max |Δ| over the battle so you can see if physics drifted at all.
Legacy: second positional digit still accepted as max turns (pre-argparse compat).
"""

from __future__ import annotations

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from battle_parser import load_battle
from compare_util import (
    angle_close,
    compare_pods_exact,
    is_invalid_thrust,
    merge_maxd,
    pos_close,
    vel_close,
)
from physics_driver import CppPhysics
from tolerance_policy import (
    EXACT_ANG_EPS_RAD,
    EXPLORE_ANG_TOL_DEG,
    EXPLORE_POS_TOL,
    EXPLORE_TIMEOUT_TOL,
    EXPLORE_VEL_TOL,
    GATE_ANG_TOL_DEG,
    GATE_POS_TOL,
    GATE_TIMEOUT_TOL,
    GATE_VEL_TOL,
)


def compare_one_turn(
    turn: int,
    sim_pods: list,
    gt_pods: list,
    sim_timeouts,
    gt_timeouts,
    n_checkpoints=0,
    *,
    pos_tol,
    vel_tol,
    ang_tol_deg,
    timeout_tol,
) -> list[str]:
    errs = []
    for i in range(4):
        sp = sim_pods[i]
        gp = gt_pods[i]

        if not (pos_close(sp["x"], gp.x, pos_tol) and pos_close(sp["y"], gp.y, pos_tol)):
            errs.append(
                f"pod{i} pos: sim=({sp['x']:.1f},{sp['y']:.1f}) gt=({gp.x:.1f},{gp.y:.1f})"
            )

        if not (vel_close(sp["vx"], gp.vx, vel_tol) and vel_close(sp["vy"], gp.vy, vel_tol)):
            errs.append(f"pod{i} vel: sim=({sp['vx']},{sp['vy']}) gt=({gp.vx},{gp.vy})")

        if not angle_close(sp["angle"], gp.angle or 0.0, ang_tol_deg):
            errs.append(
                f"pod{i} angle: sim={math.degrees(sp['angle']):.1f}° "
                f"gt={math.degrees(gp.angle or 0):.1f}°"
            )

        sim_cp = sp["next"] % n_checkpoints if n_checkpoints > 0 else sp["next"]
        if sim_cp != gp.next_cp:
            errs.append(f"pod{i} next_cp: sim={sp['next']}(mod={sim_cp}) gt={gp.next_cp}")

    if (
        abs(sim_timeouts[0] - gt_timeouts[0]) > timeout_tol
        or abs(sim_timeouts[1] - gt_timeouts[1]) > timeout_tol
    ):
        errs.append(f"timeouts: sim={sim_timeouts} gt={gt_timeouts}")

    return errs


def _apply_turn(phys: CppPhysics, ta) -> None:
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
    if is_invalid_thrust(p1_acts[0][2]):
        inv = p1_acts[0][2]
        p1_acts = [
            (p1_acts[0][0], p1_acts[0][1], inv),
            (p1_acts[1][0], p1_acts[1][1], inv),
        ]

    phys.apply(0, p0_acts[0][0], p0_acts[0][1], p0_acts[0][2])
    phys.apply(1, p0_acts[1][0], p0_acts[1][1], p0_acts[1][2])
    phys.apply(2, p1_acts[0][0], p1_acts[0][1], p1_acts[0][2])
    phys.apply(3, p1_acts[1][0], p1_acts[1][1], p1_acts[1][2])


def _parse_args(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    legacy_max = None
    if len(argv) >= 2 and argv[1].isdigit() and not argv[1].startswith("-"):
        legacy_max = int(argv[1])
        argv = [argv[0]] + argv[2:]

    p = argparse.ArgumentParser(description="DIAGNOSTIC single-battle physics compare.")
    p.add_argument("battle_json", help="Path to battle_*.json")
    p.add_argument("--max-turns", type=int, default=None, help="Stop after N turns")
    p.add_argument(
        "--gate-tolerances",
        action="store_true",
        help="Use GATE_* tolerances instead of EXPLORE_*",
    )
    p.add_argument(
        "--exact",
        action="store_true",
        help=(
            "Coordinate-exact fidelity: pos/vel/timeout/next_cp must match with Δ=0; "
            f"angle within {EXACT_ANG_EPS_RAD} rad (CG text vs C++ double). "
            "Use this to hunt physics edge cases — not the merge gate."
        ),
    )
    p.add_argument(
        "--continue-on-fail",
        action="store_true",
        help="With --exact, do not stop at first miss; report all bad turns + max |Δ|.",
    )
    p.add_argument(
        "--ang-eps",
        type=float,
        default=None,
        help=f"Override EXACT_ANG_EPS_RAD (default {EXACT_ANG_EPS_RAD})",
    )
    args = p.parse_args(argv)
    if legacy_max is not None and args.max_turns is None:
        args.max_turns = legacy_max
    if args.max_turns is None:
        args.max_turns = 999999
    if args.exact and args.gate_tolerances:
        p.error("use only one of --exact or --gate-tolerances")
    return args


def main(argv=None):
    print("role=DIAGNOSTIC", file=sys.stderr)
    args = _parse_args(argv)

    path = args.battle_json
    log = load_battle(path)
    n_cp = len(log.checkpoints)
    mode = "EXACT" if args.exact else ("GATE" if args.gate_tolerances else "EXPLORE")
    ang_eps = args.ang_eps if args.ang_eps is not None else EXACT_ANG_EPS_RAD

    print(f"Validating {path}")
    print(f"  mode={mode}  turns={len(log.turns)}  checkpoints={n_cp}")
    if args.exact:
        print(
            f"  EXACT rules: pos Δ==0, vel Δ==0, next_cp exact, timeout Δ==0, "
            f"angle |Δ|≤{ang_eps} rad (~{math.degrees(ang_eps):.3e}°)"
        )
    print(
        f"  Final referee outcome: ranks={log.ranks}, timeouts p0/p1 = "
        f"{log.keyframes[-1].timeout_p0}/{log.keyframes[-1].timeout_p1}"
    )

    phys = CppPhysics()
    phys.init_battle(log.checkpoints, laps=log.laps)

    init = log.initial_state
    for i, p in enumerate(init.pods):
        ang = p.angle if p.angle is not None else -0.0174533
        phys.set_pod(i, p.x, p.y, p.vx, p.vy, ang, p.next_cp, p.shield_active, p.boosted)
    phys.set_timeouts(init.timeout_p0, init.timeout_p1)

    if not args.exact:
        pos_tol, vel_tol = (
            (GATE_POS_TOL, GATE_VEL_TOL)
            if args.gate_tolerances
            else (EXPLORE_POS_TOL, EXPLORE_VEL_TOL)
        )
        ang_tol = GATE_ANG_TOL_DEG if args.gate_tolerances else EXPLORE_ANG_TOL_DEG
        to_tol = GATE_TIMEOUT_TOL if args.gate_tolerances else EXPLORE_TIMEOUT_TOL

    first_mismatch = None
    bad_turns = 0
    turns_run = 0
    maxd_all = {
        "dx": 0.0,
        "dy": 0.0,
        "dvx": 0.0,
        "dvy": 0.0,
        "dang_rad": 0.0,
        "dang_deg": 0.0,
        "dtimeout": 0.0,
    }
    # Keep a few sample misses for logs
    sample_errs: list[tuple[int, list[str]]] = []

    for t, ta in enumerate(log.turns):
        if t >= args.max_turns:
            break
        turns_run += 1

        _apply_turn(phys, ta)
        sim = phys.step()
        gt = log.keyframes[t]
        gt_to = (gt.timeout_p0, gt.timeout_p1)

        if args.exact:
            errs, maxd = compare_pods_exact(
                sim["pods"],
                gt.pods,
                sim["timeouts"],
                gt_to,
                n_checkpoints=n_cp,
                ang_eps_rad=ang_eps,
            )
            merge_maxd(maxd_all, maxd)
        else:
            errs = compare_one_turn(
                t,
                sim["pods"],
                gt.pods,
                sim["timeouts"],
                gt_to,
                n_checkpoints=n_cp,
                pos_tol=pos_tol,
                vel_tol=vel_tol,
                ang_tol_deg=ang_tol,
                timeout_tol=to_tol,
            )

        if errs:
            bad_turns += 1
            if first_mismatch is None:
                first_mismatch = t
            if len(sample_errs) < 20:
                sample_errs.append((t, list(errs)))
            if not args.exact or not args.continue_on_fail:
                print(f"\n!!! MISMATCH after turn {t}  mode={mode}")
                for e in errs:
                    print("   ", e)
                if args.exact:
                    print(
                        f"   running max|Δ| so far: "
                        f"pos=({maxd_all['dx']},{maxd_all['dy']}) "
                        f"vel=({maxd_all['dvx']},{maxd_all['dvy']}) "
                        f"ang_rad={maxd_all['dang_rad']:.3e} "
                        f"timeout={maxd_all['dtimeout']}"
                    )
                print("   (stopping at first divergence; use --exact --continue-on-fail for full scan)")
                break
        elif not args.exact and t % 20 == 0:
            print(f"  turn {t:3d} OK (timeouts {sim['timeouts']})")
        elif args.exact and t % 50 == 0:
            print(
                f"  turn {t:3d} EXACT OK  "
                f"max|Δ|pos=({maxd_all['dx']},{maxd_all['dy']}) "
                f"vel=({maxd_all['dvx']},{maxd_all['dvy']}) "
                f"ang_rad={maxd_all['dang_rad']:.3e}"
            )

    print()
    if args.exact:
        print("=== EXACT fidelity summary ===")
        print(f"  turns checked: {turns_run}/{len(log.turns)}")
        print(f"  turns with any EXACT miss: {bad_turns}")
        print(
            f"  max |Δ| over battle: "
            f"dx={maxd_all['dx']} dy={maxd_all['dy']} "
            f"dvx={maxd_all['dvx']} dvy={maxd_all['dvy']} "
            f"dang_rad={maxd_all['dang_rad']:.6e} dang_deg={maxd_all['dang_deg']:.6e} "
            f"dtimeout={maxd_all['dtimeout']}"
        )
        if bad_turns == 0 and turns_run == len(log.turns):
            print(
                f"\n=== EXACT MATCH through all {turns_run} turns ===\n"
                "pos/vel/next_cp/timeouts identical; angles within ε (text/double only)."
            )
            rc = 0
        elif bad_turns == 0:
            print(f"\n=== EXACT MATCH through {turns_run} turns (truncated) ===")
            rc = 0
        else:
            print(f"\nFirst EXACT miss at turn {first_mismatch}")
            for t, errs in sample_errs[:5]:
                print(f"  --- turn {t} ---")
                for e in errs:
                    print(f"    {e}")
            print(
                "Real physics edge case if pos/vel/next_cp/timeout Δ≠0. "
                "Angle-only over ε is also a bug (raise --ang-eps only for diagnosis)."
            )
            rc = 1
    else:
        if first_mismatch is None:
            print(f"=== PERFECT MATCH ({mode}) through all {len(log.turns)} turns ===")
            print("Within tolerance — not the same as --exact coordinate identity.")
            rc = 0
        else:
            print(f"First divergence at turn {first_mismatch} mode={mode}")
            rc = 1

    phys.close()
    sys.exit(rc)


if __name__ == "__main__":
    main()
