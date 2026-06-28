#!/usr/bin/env python3
"""
Full battle replay validator (DIAGNOSTIC — not MERGE_PHYSICS_OK alone).

Usage (from repo root):
    python3 sim/compare_battle.py battles/test_session_battles/battle_XXX.json
    python3 sim/compare_battle.py battles/.../battle_XXX.json --max-turns 20
    python3 sim/compare_battle.py battles/.../battle_XXX.json --gate-tolerances

Legacy: second positional digit still accepted as max turns (pre-argparse compat).
"""

from __future__ import annotations

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from battle_parser import load_battle
from compare_util import angle_close, is_invalid_thrust, pos_close, vel_close
from physics_driver import CppPhysics
from tolerance_policy import (
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


def _parse_args(argv=None):
    # Legacy: argv[2] digit as max turns without --max-turns
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
    args = p.parse_args(argv)
    if legacy_max is not None and args.max_turns is None:
        args.max_turns = legacy_max
    if args.max_turns is None:
        args.max_turns = 999999
    return args


def main(argv=None):
    print("role=DIAGNOSTIC", file=sys.stderr)
    args = _parse_args(argv)

    if args.gate_tolerances:
        pos_tol, vel_tol = GATE_POS_TOL, GATE_VEL_TOL
        ang_tol, to_tol = GATE_ANG_TOL_DEG, GATE_TIMEOUT_TOL
    else:
        pos_tol, vel_tol = EXPLORE_POS_TOL, EXPLORE_VEL_TOL
        ang_tol, to_tol = EXPLORE_ANG_TOL_DEG, EXPLORE_TIMEOUT_TOL

    path = args.battle_json
    log = load_battle(path)
    print(f"Validating {path}")
    print(f"  {len(log.turns)} turns, {len(log.checkpoints)} checkpoints")
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

    first_mismatch = None
    for t, ta in enumerate(log.turns):
        if t >= args.max_turns:
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
        gt = log.keyframes[t]

        errs = compare_one_turn(
            t,
            sim["pods"],
            gt.pods,
            sim["timeouts"],
            (gt.timeout_p0, gt.timeout_p1),
            n_checkpoints=len(log.checkpoints),
            pos_tol=pos_tol,
            vel_tol=vel_tol,
            ang_tol_deg=ang_tol,
            timeout_tol=to_tol,
        )

        if errs:
            if first_mismatch is None:
                first_mismatch = t
            print(f"\n!!! MISMATCH after turn {t}")
            for e in errs:
                print("   ", e)
            print("   (stopping at first divergence)")
            break

        if t % 20 == 0:
            print(f"  turn {t:3d} OK (timeouts {sim['timeouts']})")

    if first_mismatch is None:
        print(f"\n=== PERFECT MATCH through all {len(log.turns)} turns ===")
        print("Your physics engine produces identical states to the referee on this battle.")
    else:
        print(f"\nFirst divergence at turn {first_mismatch}")
        print(
            "Use this + the collision/shield/boost turns reported by validate.py "
            "to debug physics.h"
        )

    phys.close()


if __name__ == "__main__":
    main()
