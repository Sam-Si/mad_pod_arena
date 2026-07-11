#!/usr/bin/env python3
"""
Extract pre-thrust seeds for Fidelity ULP unit tests from battle JSON + CppPhysics.

For each battle id, steps the Fidelity driver until the first EXACT velocity
mismatch (or extracts a forced --turn/--pod). Prints C++ snippets with
harness-measured doubles: pre-thrust (vx, vy), angle after rotate (GT face),
thrust, and post-friction GT velocities that appear in EXACT logs.

Usage (repo root):
  python3 tools/extract_thrust_seed.py \\
    --ids 895340085,895345570,895429566,895515899,895564994,895612448,895637720 \\
    --dir battles/latest_battles

  # Forced turn (e.g. when WIP lattice makes the battle EXACT-perfect):
  python3 tools/extract_thrust_seed.py --ids 895340085 --dir battles/latest_battles --turn 87 --pod 1
"""

from __future__ import annotations

import argparse
import math
import os
import sys
from typing import List, Optional, Tuple

_REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(_REPO, "sim"))

from battle_parser import load_battle  # noqa: E402
from compare_battle import _apply_turn  # noqa: E402
from compare_util import compare_pods_exact  # noqa: E402
from physics_driver import CppPhysics  # noqa: E402
from tolerance_policy import EXACT_ANG_EPS_RAD  # noqa: E402

# Forensics first-EXACT (β ULP free-flight). Used when driver is already fixed.
_FORENSICS_FIRST_EXACT = {
    895340085: (87, 1),
    895345570: (95, 1),
    895429566: (81, 1),
    895515899: (51, 3),
    895564994: (34, 0),
    895612448: (233, 1),
    895637720: (290, 0),
    885922662: (94, 0),  # golden pole +20 |o|~541 plain
}


def _actions(ta):
    return [ta.p0_pod0, ta.p0_pod1, ta.p1_pod0, ta.p1_pod1]


def _find_battle_path(battle_dir: str, bid: int) -> str:
    candidates = [
        os.path.join(battle_dir, f"battle_{bid}.json"),
        os.path.join(battle_dir, f"{bid}.json"),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    # recursive basename search under dir
    for root, _, files in os.walk(battle_dir):
        for fn in files:
            if fn == f"battle_{bid}.json" or fn == f"{bid}.json":
                return os.path.join(root, fn)
    raise FileNotFoundError(f"battle {bid} not found under {battle_dir}")


def _emit_seed(
    bid: int,
    t: int,
    pod: int,
    pre,
    post,
    act,
    source: str,
) -> None:
    thr_s = act.thrust
    try:
        thr = int(thr_s)
    except (TypeError, ValueError):
        thr = thr_s
    pre_p = pre.pods[pod]
    post_p = post.pods[pod]
    ang = post_p.angle if post_p.angle is not None else 0.0
    print(f"// Battle {bid} — EXACT t{t} pod{pod} ({source})")
    print(
        f"// Numbers from extract_thrust_seed / GT keyframe t-1 + post face "
        f"(measured)."
    )
    print(f"// thr={thr!r} target=({act.target_x},{act.target_y})")
    print(
        f"// pre pos=({pre_p.x},{pre_p.y}) v=({pre_p.vx},{pre_p.vy}) "
        f"pre_ang={pre_p.angle}"
    )
    print(
        f"// post_gt v=({post_p.vx},{post_p.vy}) ang_rad={ang!r} "
        f"deg={math.degrees(ang)!r}"
    )
    print(f"// cos={math.cos(ang)!r} sin={math.sin(ang)!r}")
    print(
        f"static void test_latest_{bid}_from_isolation() {{"
        if bid != 885922662
        else "static void test_regression_pole_pos20_other541_plain() {"
    )
    # Always print portable assignment form for copy-paste either way:
    print(f"    double vx = {float(pre_p.vx)};")
    print(f"    double vy = {float(pre_p.vy)};")
    # Prefer exact angle helpers when face is cardinal / 3-4-5 / pole.
    deg = math.degrees(ang)
    if abs(deg - 90.0) < 1e-9:
        print("    const double ang = M_PI / 2.0;  // pure N")
    elif abs(deg + 90.0) < 1e-9:
        print("    const double ang = -M_PI / 2.0;  // pure S")
    elif abs(deg - 53.13010235415598) < 1e-9:
        print("    const double ang = std::atan2(0.8, 0.6);  // face 53.1301°")
    elif abs(deg - 126.86989764584402) < 1e-9:
        print("    const double ang = std::atan2(0.8, -0.6);  // face 126.8699°")
    elif abs(deg + 126.86989764584402) < 1e-9:
        print("    const double ang = std::atan2(-0.8, -0.6);  // face -126.8699°")
    elif abs(abs(deg) - 16.26020470831196) < 1e-9:
        sign = "-" if deg < 0 else ""
        print(
            f"    const double ang = {sign}16.26020470831196 * (M_PI / 180.0);  "
            f"// pole 0.96/0.28"
        )
    elif abs(deg - 163.73979529168807) < 1e-9:
        print("    const double ang = std::atan2(0.28, -0.96);  // pole face ~163.74°")
    elif abs(deg + 163.73979529168807) < 1e-9:
        print("    const double ang = std::atan2(-0.28, -0.96);  // pole face ~-163.74°")
    else:
        print(f"    const double ang = {ang!r};  // measured face rad")
    print(f"    const int thr = {thr};")
    print("    csb::applyFidelityThrust(vx, vy, ang, thr);")
    print("    vx = csb::frictionTrunc(vx);")
    print("    vy = csb::frictionTrunc(vy);")
    print(f"    EXPECT_EQ_D(vx, {float(post_p.vx)});")
    print(f"    EXPECT_EQ_D(vy, {float(post_p.vy)});")
    print(
        f'    std::cout << "latest_{bid}_from_isolation: ok\\n";'
        if bid != 885922662
        else '    std::cout << "regression_pole_pos20_other541_plain: ok\\n";'
    )
    print("}")
    print()


def extract_one(
    path: str,
    bid: int,
    forced_turn: Optional[int],
    forced_pod: Optional[int],
    use_forensics_fallback: bool,
) -> None:
    log = load_battle(path)
    n_cp = len(log.checkpoints)
    phys = CppPhysics()
    phys.init_battle(log.checkpoints, laps=log.laps)
    init = log.initial_state
    for i, p in enumerate(init.pods):
        ang = p.angle if p.angle is not None else -0.0174533
        phys.set_pod(
            i, p.x, p.y, p.vx, p.vy, ang, p.next_cp, p.shield_active, p.boosted
        )
    phys.set_timeouts(init.timeout_p0, init.timeout_p1)

    found: Optional[Tuple[int, int, str]] = None  # t, pod, source
    for t, ta in enumerate(log.turns):
        pre = log.initial_state if t == 0 else log.keyframes[t - 1]
        _apply_turn(phys, ta)
        sim = phys.step()
        gt = log.keyframes[t]
        errs, _ = compare_pods_exact(
            sim["pods"],
            gt.pods,
            sim["timeouts"],
            (gt.timeout_p0, gt.timeout_p1),
            n_checkpoints=n_cp,
            ang_eps_rad=EXACT_ANG_EPS_RAD,
        )
        if forced_turn is not None and t == forced_turn:
            pod = forced_pod if forced_pod is not None else 0
            if forced_pod is None:
                for i in range(4):
                    if any(f"pod{i}" in e and "vel" in e for e in errs):
                        pod = i
                        break
            found = (t, pod, "forced --turn")
            break
        if forced_turn is None and errs:
            for e in errs:
                if "vel" not in e:
                    continue
                for i in range(4):
                    if f"pod{i}" in e:
                        found = (t, i, "first EXACT vel miss")
                        break
                if found:
                    break
            if found:
                break

    phys.close()

    if found is None and use_forensics_fallback and bid in _FORENSICS_FIRST_EXACT:
        t, pod = _FORENSICS_FIRST_EXACT[bid]
        found = (t, pod, "forensics first_exact fallback (driver EXACT-perfect)")

    if found is None:
        print(f"// {bid}: no EXACT vel miss and no forensics fallback", file=sys.stderr)
        return

    t, pod, source = found
    pre = log.initial_state if t == 0 else log.keyframes[t - 1]
    post = log.keyframes[t]
    act = _actions(log.turns[t])[pod]
    _emit_seed(bid, t, pod, pre, post, act, source)


def main(argv: Optional[List[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--ids",
        required=True,
        help="Comma-separated battle ids",
    )
    p.add_argument(
        "--dir",
        default="battles/latest_battles",
        help="Directory (or tree root) containing battle_*.json",
    )
    p.add_argument("--turn", type=int, default=None, help="Force extract at turn t")
    p.add_argument("--pod", type=int, default=None, help="Force pod index with --turn")
    p.add_argument(
        "--no-forensics-fallback",
        action="store_true",
        help="Do not fall back to known forensics first_exact when driver is perfect",
    )
    args = p.parse_args(argv)

    battle_dir = args.dir
    if not os.path.isabs(battle_dir):
        battle_dir = os.path.join(_REPO, battle_dir)

    ids = [int(x.strip()) for x in args.ids.split(",") if x.strip()]
    print(f"// extract_thrust_seed: ids={ids} dir={battle_dir}")
    print("// EXPECT values are GT keyframe velocities (appear in EXACT logs).")
    print()
    for bid in ids:
        path = _find_battle_path(battle_dir, bid)
        extract_one(
            path,
            bid,
            forced_turn=args.turn,
            forced_pod=args.pod,
            use_forensics_fallback=not args.no_forensics_fallback,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
