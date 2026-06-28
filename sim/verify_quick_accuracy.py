#!/usr/bin/env python3
"""Quick physics accuracy harness (battles/quick_physics_accuracy).

Reports:
  - TURN-PERFECT at given tolerances (default ±1 pos/vel)
  - OUTCOME: first pod to complete laps (team wins), vs CG ranks
  - CG outcome uses ranks[i]==0 as winner; physics uses first finish / timeout

Usage:
  python3 sim/verify_quick_accuracy.py
  python3 sim/verify_quick_accuracy.py --gate
  python3 sim/verify_quick_accuracy.py --dir battles/leaderboard_battles --recursive --limit 500
"""
from __future__ import annotations
import argparse, glob, gc, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from battle_parser import load_battle
from compare_util import angle_close, is_invalid_thrust, pos_close, vel_close
from physics_driver import CppPhysics
from tolerance_policy import (
    EXPLORE_ANG_TOL_DEG, EXPLORE_POS_TOL, EXPLORE_VEL_TOL,
    GATE_ANG_TOL_DEG, GATE_POS_TOL, GATE_TIMEOUT_TOL, GATE_VEL_TOL,
)

def apply_turn(phys, ta):
    p0 = [(ta.p0_pod0.target_x, ta.p0_pod0.target_y, ta.p0_pod0.thrust),
          (ta.p0_pod1.target_x, ta.p0_pod1.target_y, ta.p0_pod1.thrust)]
    p1 = [(ta.p1_pod0.target_x, ta.p1_pod0.target_y, ta.p1_pod0.thrust),
          (ta.p1_pod1.target_x, ta.p1_pod1.target_y, ta.p1_pod1.thrust)]
    if is_invalid_thrust(p0[0][2]):
        inv = p0[0][2]; p0 = [(p0[0][0], p0[0][1], inv), (p0[1][0], p0[1][1], inv)]
    if is_invalid_thrust(p1[0][2]):
        inv = p1[0][2]; p1 = [(p1[0][0], p1[0][1], inv), (p1[1][0], p1[1][1], inv)]
    phys.apply(0, *p0[0]); phys.apply(1, *p0[1]); phys.apply(2, *p1[0]); phys.apply(3, *p1[1])
    return phys.step()

def run_battle(path, pos_tol, vel_tol, ang_tol):
    log = load_battle(path)
    phys = CppPhysics()
    try:
        phys.init_battle(log.checkpoints, log.laps)
        init = log.initial_state
        for i, p in enumerate(init.pods):
            ang = p.angle if p.angle is not None else -0.0174533
            phys.set_pod(i, p.x, p.y, p.vx, p.vy, ang, p.next_cp, p.shield_active, p.boosted)
        phys.set_timeouts(init.timeout_p0, init.timeout_p1)
        n_cp = len(log.checkpoints)
        finish_at = log.laps * n_cp
        n_turns = min(len(log.turns), len(log.keyframes))
        first_fail = None
        turns_ok = 0
        first_pod_finish = None
        last = None
        for t_idx in range(n_turns):
            sim = apply_turn(phys, log.turns[t_idx])
            last = sim
            if len(sim.get("pods", [])) < 4:
                if first_fail is None: first_fail = t_idx
                continue
            for i, sp in enumerate(sim["pods"]):
                if sp["next"] >= finish_at and first_pod_finish is None:
                    first_pod_finish = (t_idx, i)
            gt = log.keyframes[t_idx]
            bad = False
            for i in range(4):
                sp, gp = sim["pods"][i], gt.pods[i]
                if not (pos_close(sp["x"], gp.x, pos_tol) and pos_close(sp["y"], gp.y, pos_tol)): bad = True
                if not (vel_close(sp["vx"], gp.vx, vel_tol) and vel_close(sp["vy"], gp.vy, vel_tol)): bad = True
                if gp.angle is not None and not angle_close(sp["angle"], gp.angle, ang_tol): bad = True
                if (sp["next"] % n_cp if n_cp else sp["next"]) != gp.next_cp: bad = True
            if (abs(sim["timeouts"][0] - gt.timeout_p0) > GATE_TIMEOUT_TOL or
                    abs(sim["timeouts"][1] - gt.timeout_p1) > GATE_TIMEOUT_TOL):
                bad = True
            if bad:
                if first_fail is None: first_fail = t_idx
            else:
                turns_ok += 1
        # Physics outcome: first pod completing all laps; else timeout / best progress
        phys_winner = -1
        if first_pod_finish is not None:
            phys_winner = 0 if first_pod_finish[1] < 2 else 1
        elif last and len(last["pods"]) == 4:
            t0, t1 = last["timeouts"]
            if t0 <= 0 and t1 > 0: phys_winner = 1
            elif t1 <= 0 and t0 > 0: phys_winner = 0
            else:
                best = [0, 0]
                for i, sp in enumerate(last["pods"]):
                    best[0 if i < 2 else 1] = max(best[0 if i < 2 else 1], sp["next"])
                if best[0] != best[1]:
                    phys_winner = 0 if best[0] > best[1] else 1
        cg_winner = log.winner
        return {
            "name": os.path.basename(path),
            "n_turns": n_turns,
            "turns_ok": turns_ok,
            "perfect": first_fail is None,
            "first_fail": first_fail,
            "phys_winner": phys_winner,
            "cg_winner": cg_winner,
            "outcome_ok": phys_winner == cg_winner and cg_winner >= 0,
            "ranks": log.ranks,
        }
    finally:
        phys.close()
        gc.collect()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="battles/quick_physics_accuracy/battles")
    ap.add_argument("--recursive", action="store_true")
    ap.add_argument("--pos-tol", type=float, default=EXPLORE_POS_TOL)
    ap.add_argument("--vel-tol", type=float, default=EXPLORE_VEL_TOL)
    ap.add_argument("--ang-tol", type=float, default=EXPLORE_ANG_TOL_DEG)
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    if args.gate:
        args.pos_tol, args.vel_tol, args.ang_tol = GATE_POS_TOL, GATE_VEL_TOL, GATE_ANG_TOL_DEG
    pat = os.path.join(args.dir, "**", "battle_[0-9]*.json") if args.recursive else os.path.join(args.dir, "battle_[0-9]*.json")
    files = sorted(glob.glob(pat, recursive=args.recursive))
    # dedupe realpath, skip .divergence
    seen, uniq = set(), []
    for f in files:
        if ".divergence." in f: continue
        rp = os.path.realpath(f)
        if rp in seen: continue
        seen.add(rp); uniq.append(f)
    files = uniq
    if args.limit: files = files[:args.limit]
    print(f"n={len(files)} tol=({args.pos_tol},{args.vel_tol},{args.ang_tol}) dir={args.dir}")
    t0 = time.time()
    out_ok = perf = 0
    tok = ttot = 0
    ofail, tfail = [], []
    for i, f in enumerate(files):
        try:
            r = run_battle(f, args.pos_tol, args.vel_tol, args.ang_tol)
        except Exception as e:
            ofail.append((os.path.basename(f), f"exc:{e}")); tfail.append((os.path.basename(f), -1)); continue
        tok += r["turns_ok"]; ttot += r["n_turns"]
        if r["outcome_ok"]: out_ok += 1
        else: ofail.append((r["name"], r["phys_winner"], r["cg_winner"], r["ranks"]))
        if r["perfect"]: perf += 1
        else: tfail.append((r["name"], r["first_fail"], r["n_turns"]))
        if (i+1) % 100 == 0: print(f"  … {i+1}/{len(files)}")
    n = len(files)
    print("=" * 60)
    print(f"OUTCOME (physics first-finish vs CG ranks): {out_ok}/{n} = {100*out_ok/max(1,n):.2f}%")
    print(f"TURN-PERFECT battles: {perf}/{n} = {100*perf/max(1,n):.2f}%")
    print(f"TURN accuracy (matched turns / all turns): {tok}/{ttot} = {100*tok/max(1,ttot):.4f}%")
    print(f"Time {time.time()-t0:.1f}s")
    if ofail:
        print(f"Outcome fails ({len(ofail)}):")
        for x in ofail[:20]: print(" ", x)
    if tfail:
        print(f"Turn fails ({len(tfail)}):")
        for x in tfail[:20]: print(" ", x)
    if out_ok == n and perf == n:
        print("\n*** 100% OUTCOMES + 100% TURN-PERFECT ***"); return 0
    if perf == n:
        print("\n*** 100% TURN-PERFECT (outcomes may use different CG rank mapping) ***"); return 2
    return 1

if __name__ == "__main__":
    sys.exit(main() or 0)
